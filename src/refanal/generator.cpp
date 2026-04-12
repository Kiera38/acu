#include "refanal/generator.h"

#include "semanal/semanal.h"
#include "semanal/types.h"
#include "variant.h"

namespace acu::refanal {

using SemInstRef = acu::ir::InstRef;
using SemInst = acu::ir::Inst;

class FuncGenerator {
    const semanal::AnalyzedPackage* apackage_;
    const semanal::AnalyzedFunc* afunc_;
    const ::acu::ir::Func* sfunc_;
    ir::Func rfunc_;

    ir::BlockRef current_block_ {};

    struct LoopTargets {
        ir::BlockRef continue_target;
        ir::BlockRef break_target;
    };
    std::vector<LoopTargets> loops_;
    IndexVector<ir::InstRef, SemInstRef> inst_map_;

public:
    FuncGenerator(
        const semanal::AnalyzedPackage& apackage,
        const semanal::AnalyzedFunc& afunc,
        const ::acu::ir::Func& sfunc
    )
        : apackage_(&apackage),
          afunc_(&afunc),
          sfunc_(&sfunc),
          rfunc_(sfunc.name(), sfunc.source(), sfunc.location(), sfunc.is_extern()) {}

    ir::Func generate() {
        const auto& sparams = sfunc_->params();
        std::vector<ir::Param> rparams;
        rparams.reserve(sparams.size());
        for (auto i : sparams.indices()) {
            rparams.push_back(
                ir::Param {.name = sparams[i].name, .type = sparams[i].type}
            );
        }
        rfunc_.set_type(rparams, sfunc_->return_type());

        inst_map_.resize(sfunc_->insts().size(), ir::InstRef {~0u});

        current_block_ = rfunc_.add_block(ir::Block {});

        if (!sfunc_->insts().empty()) {
            ::acu::ir::Block main_block {
                .start = SemInstRef {0}, .end = sfunc_->last_inst()
            };
            visit_block(main_block);
        }

        if (!is_terminated() && current_block_.index != ~0u) {
            ir::Inst ret;
            ret.type = rfunc_.return_type();
            ret.location = Location {};
            ret.data = ir::Inst::Return {std::nullopt};
            ir::InstRef ret_ref = rfunc_.add(ret);
            rfunc_.block(current_block_).insts.push_back(ret_ref);
        }

        rfunc_.rebuild_cfg();
        return std::move(rfunc_);
    }

    void visit_block(acu::ir::Block sblock) {
        if (sblock.end.index < sblock.start.index) return;

        auto current = sblock.start;
        while (current.index <= sblock.end.index) {
            current = visit_inst(current);
            current = SemInstRef {current.index + 1};
        }
    }

    [[nodiscard]] bool is_terminated() const {
        const auto& block = rfunc_.block(current_block_);
        if (block.insts.empty()) return false;
        auto last_inst_ref = block.insts.back();
        auto last_inst = rfunc_.inst(last_inst_ref);
        return last_inst.data.is<ir::Inst::Jump>() ||
               last_inst.data.is<ir::Inst::Branch>() ||
               last_inst.data.is<ir::Inst::Return>();
    }

    void jump_to(ir::BlockRef target, Location loc) {
        if (is_terminated()) return;

        ir::Inst jump = {
            .data = ir::Inst::Jump {target},
            .type =
                types::SpecType {
                    .type = types::None, .specifier = types::Specifier::None
                },
            .location = loc,
        };

        emit_inst(jump);
    }

    ir::InstRef get_mapped(::acu::ir::InstRef sref) { return inst_map_[sref]; }

    ir::InstRef get_mapped(std::optional<::acu::ir::InstRef> sref) {
        if (!sref) return ir::InstRef {~0u};
        return inst_map_[*sref];
    }

    ir::InstRef get_cast(
        ::acu::ir::InstRef sref, types::SpecType expected_type, Location loc
    ) {
        auto rref = get_mapped(sref);
        if (sref.index >= afunc_->inst_types.size()) return rref;
        auto actual_type = afunc_->inst_types[sref];
        if (actual_type.type != expected_type.type) {
            ir::Inst cast_inst {
                .data = ir::Inst::Cast {.value = rref},
                .type = expected_type,
                .location = loc,
            };
            return emit_inst(cast_inst);
        }
        return rref;
    }

    ir::InstRef get_cast(
        SemInstRef sref, types::TypeId expected_type, Location loc
    ) {
        return get_cast(
            sref,
            types::SpecType {
                .type = expected_type, .specifier = types::Specifier::None
            },
            loc
        );
    }

    ir::InstRef emit_inst(ir::Inst inst) {
        ir::InstRef rref = rfunc_.add(inst);
        if (!is_terminated()) {
            rfunc_.block(current_block_).insts.push_back(rref);
        }
        return rref;
    }

    SemInstRef visit_inst(SemInstRef sref) {
        const auto& sinst = sfunc_->inst(sref);
        const auto& type = afunc_->inst_types[sref];

        auto emit = [&](auto data) -> ir::InstRef {
            ir::Inst rinst = {
                .data = std::move(data),
                .type = type,
                .location = sinst.location,
            };
            ir::InstRef ref = emit_inst(rinst);
            inst_map_[sref] = ref;
            return ref;
        };

        return sinst.data.visit(
            [&](const SemInst::Const& inst) -> SemInstRef {
                auto v = inst.value.visit(
                    [&](bool v) -> ir::Inst::Const::Value { return v; },
                    [&](std::int64_t v) -> ir::Inst::Const::Value { return v; },
                    [&](double v) -> ir::Inst::Const::Value { return v; },
                    [&](char32_t v) -> ir::Inst::Const::Value { return v; },
                    [&](std::string_view v) -> ir::Inst::Const::Value {
                        return v;
                    },
                    [&](::acu::ir::FuncRef v) -> ir::Inst::Const::Value {
                        return ir::FuncRef {v.index};
                    },
                    [&](types::TypeId v) -> ir::Inst::Const::Value {
                        return false;  // TypeId is handled in Call for
                                       // CreateStruct
                    }
                );
                emit(ir::Inst::Const {v});
                return sref;
            },
            [&](const SemInst::VarDecl& inst) -> SemInstRef {
                emit(ir::Inst::VarDecl {inst.name});
                return sref;
            },
            [&](const SemInst::LoadVar& inst) -> SemInstRef {
                emit(ir::Inst::LoadVar {get_mapped(inst.var)});
                return sref;
            },
            [&](const SemInst::LoadParam& inst) -> SemInstRef {
                emit(ir::Inst::LoadParam {ir::ParamRef {inst.param.index}});
                return sref;
            },
            [&](const SemInst::Store& inst) -> SemInstRef {
                auto var_type = afunc_->inst_types[inst.var];
                emit(
                    ir::Inst::Store {
                        .var = get_mapped(inst.var),
                        .value = get_cast(inst.value, var_type, sinst.location)
                    }
                );
                return sref;
            },
            [&](const SemInst::Binary& inst) -> SemInstRef {
                emit(
                    ir::Inst::Binary {
                        .left = get_cast(inst.left, type, sinst.location),
                        .right = get_cast(inst.right, type, sinst.location),
                        .op = static_cast<ir::Inst::BinaryOp>(inst.op)
                    }
                );
                return sref;
            },
            [&](const SemInst::Unary& inst) -> SemInstRef {
                ir::InstRef val = get_mapped(inst.value);
                if (inst.op == SemInst::UnaryOp::Not) {
                    val = get_cast(inst.value, types::Bool, sinst.location);
                }
                emit(
                    ir::Inst::Unary {
                        .value = val,
                        .op = static_cast<ir::Inst::UnaryOp>(inst.op)
                    }
                );
                return sref;
            },
            [&](const SemInst::Comparison& inst) -> SemInstRef {
                auto comparators = sfunc_->comparators(inst.comparators);
                if (comparators.empty()) return sref;

                auto current_left_sref = inst.left;
                auto skip_idx = sref;

                ir::BlockRef merge_block {~0u};
                if (comparators.size() > 1) {
                    merge_block = rfunc_.add_block(ir::Block {});
                }

                for (size_t i = 0; i < comparators.size(); ++i) {
                    auto& comp = comparators[i];

                    visit_block(comp.value);
                    auto right_sref = comp.value.end;

                    ir::Inst comp_inst {
                        .data =
                            ir::Inst::Comparison {
                                .left = get_cast(
                                    current_left_sref, comp.type, sinst.location
                                ),
                                .right = get_cast(
                                    right_sref, comp.type, sinst.location
                                ),
                                .op =
                                    static_cast<ir::Inst::ComparisonOp>(comp.op)
                            },
                        .type = afunc_->inst_types[sref],
                        .location = sinst.location
                    };

                    ir::InstRef comp_ref = rfunc_.add(comp_inst);
                    if (!is_terminated()) {
                        rfunc_.block(current_block_).insts.push_back(comp_ref);
                    }

                    if (i == 0) {
                        inst_map_[sref] = comp_ref;
                    }

                    if (comp.value.start.index == skip_idx.index + 1) {
                        skip_idx = comp.value.end.index > skip_idx.index
                                       ? comp.value.end
                                       : skip_idx;
                    }

                    if (i < comparators.size() - 1) {
                        auto right_block = rfunc_.add_block(ir::Block {});

                        ir::Inst branch {
                            .data =
                                ir::Inst::Branch {
                                    .condition = comp_ref,
                                    .true_target = right_block,
                                    .false_target = merge_block
                                },
                            .type = afunc_->inst_types[sref],
                            .location = sinst.location
                        };
                        ir::InstRef branch_ref = rfunc_.add(branch);
                        if (!is_terminated()) {
                            rfunc_.block(current_block_)
                                .insts.push_back(branch_ref);
                        }
                        if (i == 0) {
                            inst_map_[sref] = branch_ref;
                        }

                        current_block_ = right_block;
                        current_left_sref = right_sref;
                    }
                }

                if (comparators.size() > 1) {
                    jump_to(merge_block, sinst.location);
                    current_block_ = merge_block;
                }

                return skip_idx;
            },
            [&](const SemInst::Call& inst) -> SemInstRef {
                auto func_s_type = afunc_->inst_types[inst.value].type;
                const types::Type::Func* defined_func = nullptr;
                if (func_s_type != types::None) {
                    const auto& f_type =
                        apackage_->ir_package.types().get(func_s_type);
                    defined_func = f_type.data.get_if<types::Type::Func>();
                }

                const auto& callee_inst = sfunc_->inst(inst.value);
                if (callee_inst.data.is<::acu::ir::Inst::Const>()) {
                    const auto& const_val =
                        callee_inst.data.get<::acu::ir::Inst::Const>();
                    if (const_val.value.is<types::TypeId>()) {
                        auto struct_type = const_val.value.get<types::TypeId>();
                        const auto& struct_def =
                            apackage_->ir_package.types()
                                .get(struct_type)
                                .data.get<types::Type::Struct>();

                        auto s_args = sfunc_->inst_refs(inst.args);
                        std::vector<ir::InstRef> r_args;
                        for (size_t i = 0; i < s_args.size(); ++i) {
                            if (i < struct_def.fields.size()) {
                                r_args.push_back(get_cast(
                                    s_args[i],
                                    struct_def.fields[i].type.type,
                                    sinst.location
                                ));
                            } else {
                                r_args.push_back(get_mapped(s_args[i]));
                            }
                        }
                        ir::InstRefs refs = rfunc_.add(r_args);

                        emit(
                            ir::Inst::CreateStruct {
                                .struct_type = struct_type, .args = refs
                            }
                        );
                        return sref;
                    }
                }

                auto s_args = sfunc_->inst_refs(inst.args);
                std::vector<ir::InstRef> r_args;
                for (size_t i = 0; i < s_args.size(); ++i) {
                    if (defined_func && i < defined_func->params.size()) {
                        r_args.push_back(get_cast(
                            s_args[i],
                            defined_func->params[i].type,
                            sinst.location
                        ));
                    } else {
                        r_args.push_back(get_mapped(s_args[i]));
                    }
                }
                ir::InstRefs refs = rfunc_.add(r_args);

                emit(
                    ir::Inst::Call {
                        .value = get_mapped(inst.value), .args = refs
                    }
                );
                return sref;
            },
            [&](const SemInst::If& inst) -> SemInstRef {
                auto true_block = rfunc_.add_block(ir::Block {});
                ir::BlockRef false_block {~0u};
                if (inst.else_block) {
                    false_block = rfunc_.add_block(ir::Block {});
                }
                auto merge_block = rfunc_.add_block(ir::Block {});

                ir::Inst branch {
                    .data =
                        ir::Inst::Branch {
                            .condition = get_cast(
                                inst.value, types::Bool, sinst.location
                            ),
                            .true_target = true_block,
                            .false_target =
                                inst.else_block ? false_block : merge_block
                        },
                    .type = afunc_->inst_types[sref],
                    .location = sinst.location,
                };
                ir::InstRef branch_ref = rfunc_.add(branch);
                if (!is_terminated())
                    rfunc_.block(current_block_).insts.push_back(branch_ref);
                inst_map_[sref] = branch_ref;

                current_block_ = true_block;
                visit_block(inst.then_block);
                jump_to(merge_block, sinst.location);

                SemInstRef skip_idx = sref;
                if (inst.then_block.start.index == sref.index + 1) {
                    skip_idx = inst.then_block.end.index > skip_idx.index
                                   ? inst.then_block.end
                                   : skip_idx;
                }

                if (inst.else_block) {
                    current_block_ = false_block;
                    visit_block(*inst.else_block);
                    jump_to(merge_block, sinst.location);
                    if (inst.else_block->start.index == skip_idx.index + 1) {
                        skip_idx = inst.else_block->end.index > skip_idx.index
                                       ? inst.else_block->end
                                       : skip_idx;
                    }
                }

                current_block_ = merge_block;
                return skip_idx;
            },
            [&](const SemInst::Loop& inst) -> SemInstRef {
                auto loop_body = rfunc_.add_block(ir::Block {});
                auto loop_merge = rfunc_.add_block(ir::Block {});

                jump_to(loop_body, sinst.location);

                loops_.push_back(
                    {.continue_target = loop_body, .break_target = loop_merge}
                );

                current_block_ = loop_body;
                visit_block(inst.block);
                jump_to(loop_body, sinst.location);

                loops_.pop_back();

                current_block_ = loop_merge;
                auto skip_idx = sref;
                if (inst.block.start.index == sref.index + 1) {
                    skip_idx = inst.block.end.index > skip_idx.index
                                   ? inst.block.end
                                   : skip_idx;
                }
                return skip_idx;
            },
            [&](const SemInst::Return& inst) -> SemInstRef {
                std::optional<ir::InstRef> val;
                if (inst.value) {
                    val = get_cast(
                        *inst.value, sfunc_->return_type(), sinst.location
                    );
                }
                emit(ir::Inst::Return {val});
                return sref;
            },
            [&](const SemInst::Break& inst) -> SemInstRef {
                if (!loops_.empty()) {
                    jump_to(loops_.back().break_target, sinst.location);
                }
                return sref;
            },
            [&](const SemInst::Continue& inst) -> SemInstRef {
                if (!loops_.empty()) {
                    jump_to(loops_.back().continue_target, sinst.location);
                }
                return sref;
            },
            [&](const SemInst::GetAttr& inst) -> SemInstRef {
                auto base_ref = get_mapped(inst.value);
                auto base_type = afunc_->inst_types[inst.value].type;

                const auto& defined_struct =
                    apackage_->ir_package.types()
                        .get(base_type)
                        .data.get<types::Type::Struct>();
                uint32_t field_idx = 0;
                for (size_t i = 0; i < defined_struct.fields.size(); ++i) {
                    if (defined_struct.fields[i].name == inst.name) {
                        field_idx = i;
                        break;
                    }
                }

                emit(
                    ir::Inst::GetField {.value = base_ref, .index = field_idx}
                );
                return sref;
            },
            [&](const SemInst::SetAttr& inst) -> SemInstRef {
                auto base_ref = get_mapped(inst.var);
                auto base_type = afunc_->inst_types[inst.var].type;

                const auto& defined_struct =
                    apackage_->ir_package.types()
                        .get(base_type)
                        .data.get<types::Type::Struct>();
                uint32_t field_idx = 0;
                ir::InstRef value_mapped = get_mapped(inst.value);
                for (size_t i = 0; i < defined_struct.fields.size(); ++i) {
                    if (defined_struct.fields[i].name == inst.name) {
                        field_idx = i;
                        value_mapped = get_cast(
                            inst.value,
                            defined_struct.fields[i].type.type,
                            sinst.location
                        );
                        break;
                    }
                }

                emit(
                    ir::Inst::SetField {
                        .var = base_ref,
                        .index = field_idx,
                        .value = value_mapped
                    }
                );
                return sref;
            },
            [&](const SemInst::GetItem& inst) -> SemInstRef {
                emit(
                    ir::Inst::GetItem {
                        .value = get_mapped(inst.value),
                        .index = get_mapped(inst.index)
                    }
                );
                return sref;
            },
            [&](const SemInst::SetItem& inst) -> SemInstRef {
                auto base_type = afunc_->inst_types[inst.var].type;
                types::TypeId item_type = types::None;
                if (base_type != types::None) {
                    const auto& defined_type =
                        apackage_->ir_package.types().get(base_type);
                    if (auto at =
                            defined_type.data.get_if<types::Type::Array>()) {
                        item_type = at->item.type;
                    } else if (auto pt = defined_type.data
                                             .get_if<types::Type::Ptr>()) {
                        item_type = pt->type.type;
                    }
                }
                ir::InstRef value_mapped = get_mapped(inst.value);
                if (item_type != types::None) {
                    value_mapped =
                        get_cast(inst.value, item_type, sinst.location);
                }

                emit(
                    ir::Inst::SetItem {
                        .var = get_mapped(inst.var),
                        .index = get_mapped(inst.index),
                        .value = value_mapped
                    }
                );
                return sref;
            },
            [&](const SemInst::As& inst) -> SemInstRef {
                emit(
                    ir::Inst::Cast {
                        .value = get_mapped(inst.value),
                    }
                );
                return sref;
            },
            [&](const SemInst::Logical& inst) -> SemInstRef {
                auto right_block = rfunc_.add_block(ir::Block {});
                auto merge_block = rfunc_.add_block(ir::Block {});

                ir::Inst branch;
                branch.type = afunc_->inst_types[sref];
                branch.location = sinst.location;
                if (inst.op == ::acu::ir::Inst::LogicalOp::And) {
                    branch.data = ir::Inst::Branch {
                        .condition =
                            get_cast(inst.left, types::Bool, sinst.location),
                        .true_target = right_block,
                        .false_target = merge_block
                    };
                } else {
                    branch.data = ir::Inst::Branch {
                        .condition =
                            get_cast(inst.left, types::Bool, sinst.location),
                        .true_target = merge_block,
                        .false_target = right_block
                    };
                }
                ir::InstRef branch_ref = emit_inst(branch);
                inst_map_[sref] = branch_ref;
                current_block_ = right_block;
                visit_block(inst.right);
                jump_to(merge_block, sinst.location);

                current_block_ = merge_block;
                ::acu::ir::InstRef skip_idx = sref;
                if (inst.right.start.index == sref.index + 1) {
                    skip_idx = inst.right.end.index > skip_idx.index
                                   ? inst.right.end
                                   : skip_idx;
                }
                return skip_idx;
            },
            [&](const SemInst::Array& inst) -> SemInstRef {
                auto s_args = sfunc_->inst_refs(inst.items);
                auto array_type = afunc_->inst_types[sref].type;
                types::TypeId item_type = types::None;
                if (array_type != types::None) {
                    const auto& defined_type =
                        apackage_->ir_package.types().get(array_type);
                    if (auto at =
                            defined_type.data.get_if<types::Type::Array>()) {
                        item_type = at->item.type;
                    }
                }

                std::vector<ir::InstRef> r_args;
                for (auto s_arg : s_args) {
                    r_args.push_back(get_mapped(s_arg));
                }
                ir::InstRefs refs = rfunc_.add(r_args);
                emit(ir::Inst::Array {refs});
                return sref;
            },
            [&](const SemInst::AddressOf& inst) -> SemInstRef {
                emit(ir::Inst::AddressOf {get_mapped(inst.value)});
                return sref;
            },
            [&](const SemInst::Deref& inst) -> SemInstRef {
                emit(ir::Inst::Deref {get_mapped(inst.value)});
                return sref;
            }
        );
    }
};

ir::Module generate(semanal::AnalyzedPackage& analyzed_package) {
    ir::Module rmod(analyzed_package.ir_package.types());
    for (const auto& afunc : analyzed_package.analyzed_funcs) {
        const auto& sfunc = analyzed_package.ir_package.func(afunc.ref);
        FuncGenerator fg(analyzed_package, afunc, sfunc);
        rmod.add(fg.generate());
    }
    return rmod;
}

}  // namespace acu::refanal
