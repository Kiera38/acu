#include "refanal/generator.h"

#include <algorithm>

#include "ir.h"
#include "semanal/ir.h"
#include "semanal/types.h"
#include "variant.h"

namespace acu::refanal {

using SemInstRef = acu::ir::InstRef;
using SemInst = acu::ir::Inst;

class FuncGenerator {
    const acu::ir::AnalyzedPackage* apackage_;
    const ::acu::ir::Func* sfunc_;
    ir::Func rfunc_;

    ir::BlockRef current_block_ {};

    struct LoopTargets {
        ir::BlockRef continue_target;
        ir::BlockRef break_target;
    };
    std::vector<LoopTargets> loops_;
    IndexMap<SemInstRef, ir::InstRef> inst_map_;
    std::uint32_t current_inst_ = 0;

public:
    FuncGenerator(
        const acu::ir::AnalyzedPackage& apackage, const ::acu::ir::Func& sfunc
    )
        : apackage_(&apackage),
          sfunc_(&sfunc),
          rfunc_(sfunc.name, *sfunc.source, sfunc.location, sfunc.is_extern),
          inst_map_(sfunc.insts, ir::InstRef {~0u}) {}

    ir::Func generate() {
        const auto& sparams = sfunc_->params;
        std::vector<ir::Param> rparams;
        rparams.reserve(sparams.size);
        for (auto i : sparams) {
            const auto& param = apackage_->ir_package->param(i);
            rparams.push_back(
                ir::Param {.name = param.name, .type = param.type}
            );
        }
        rfunc_.set_type(rparams, sfunc_->return_type);

        current_block_ = rfunc_.add_block(ir::Block {});

        if (!sfunc_->insts.empty()) {
            current_inst_ = sfunc_->insts.start;
            ::acu::ir::Block main_block {
                .end = {sfunc_->insts.start + sfunc_->insts.size-1}
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
        while (current_inst_ <= sblock.end.index) {
            visit_inst(SemInstRef {current_inst_++});
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
        if (sref.index >= apackage_->inst_types.size()) return rref;
        auto actual_type = apackage_->inst_types[sref];
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

    void visit_inst(SemInstRef sref) {
        const auto& sinst = apackage_->ir_package->inst(sref);
        const auto& type = apackage_->inst_types[sref];

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

        sinst.data.visit(
            [&](const SemInst::Const& inst) {
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
                    [&](::acu::ir::UsedFuncRef v) -> ir::Inst::Const::Value {
                        return ir::UsedFuncRef {.index = v.index};
                    },
                    [&](types::TypeId v) -> ir::Inst::Const::Value {
                        return false;  // TypeId is handled in Call for
                                       // CreateStruct
                    }
                );
                emit(ir::Inst::Const {v});
            },
            [&](const SemInst::VarDecl& inst) {
                emit(ir::Inst::VarDecl {inst.name});
            },
            [&](const SemInst::LoadVar& inst) {
                emit(ir::Inst::LoadVar {get_mapped(inst.var)});
            },
            [&](const SemInst::LoadParam& inst) {
                emit(ir::Inst::LoadParam {ir::ParamRef {inst.param.index}});
            },
            [&](const SemInst::Store& inst) {
                auto var_type = apackage_->inst_types[inst.var];
                emit(
                    ir::Inst::Store {
                        .var = get_mapped(inst.var),
                        .value = get_cast(inst.value, var_type, sinst.location)
                    }
                );
            },
            [&](const SemInst::Binary& inst) {
                emit(
                    ir::Inst::Binary {
                        .left = get_cast(inst.left, type, sinst.location),
                        .right = get_cast(inst.right, type, sinst.location),
                        .op = static_cast<ir::Inst::BinaryOp>(inst.op)
                    }
                );
            },
            [&](const SemInst::Unary& inst) {
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
            },
            [&](const SemInst::Comparison& inst) {
                auto comparators =
                    apackage_->ir_package->comparators(inst.comparators);
                if (comparators.empty()) return;

                auto current_left_sref = inst.left;

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
                        .type = apackage_->inst_types[sref],
                        .location = sinst.location
                    };

                    ir::InstRef comp_ref = rfunc_.add(comp_inst);
                    if (!is_terminated()) {
                        rfunc_.block(current_block_).insts.push_back(comp_ref);
                    }

                    if (i == 0) {
                        inst_map_[sref] = comp_ref;
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
                            .type = apackage_->inst_types[sref],
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
            },
            [&](const SemInst::Call& inst) {
                auto func_s_type = apackage_->inst_types[inst.value].type;
                const types::Type::Func* defined_func = nullptr;
                if (func_s_type != types::None) {
                    const auto& f_type =
                        apackage_->ir_package->types().get(func_s_type);
                    defined_func = f_type.data.get_if<types::Type::Func>();
                }

                const auto& callee_inst =
                    apackage_->ir_package->inst(inst.value);
                if (callee_inst.data.is<::acu::ir::Inst::Const>()) {
                    const auto& const_val =
                        callee_inst.data.get<::acu::ir::Inst::Const>();
                    if (const_val.value.is<types::TypeId>()) {
                        auto struct_type = const_val.value.get<types::TypeId>();
                        const auto& struct_def =
                            apackage_->ir_package->types()
                                .get(struct_type)
                                .data.get<types::Type::Struct>();

                        auto s_args =
                            apackage_->ir_package->inst_refs(inst.args);
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
                    }
                }

                auto s_args = apackage_->ir_package->inst_refs(inst.args);
                auto nargs = apackage_->ir_package->call_args(inst.named_args);
                std::vector<ir::InstRef> r_args;
                r_args.reserve(s_args.size() + nargs.size());
                for (size_t i = 0; i < s_args.size(); ++i) {
                    r_args.push_back(get_cast(
                        s_args[i], defined_func->params[i].type, sinst.location
                    ));
                }
                for (const auto& param :
                     std::span(defined_func->params).subspan(s_args.size())) {
                    auto result =
                        std::ranges::find_if(nargs, [&](const auto& arg) {
                            return arg.name == param.name;
                        });
                    if (result != nargs.end()) {
                        r_args.push_back(
                            get_cast(result->value, param.type, sinst.location)
                        );
                    }
                }
                ir::InstRefs refs = rfunc_.add(r_args);

                emit(
                    ir::Inst::Call {
                        .value = get_mapped(inst.value), .args = refs
                    }
                );
            },
            [&](const SemInst::If& inst) {
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
                    .type = apackage_->inst_types[sref],
                    .location = sinst.location,
                };
                ir::InstRef branch_ref = rfunc_.add(branch);
                if (!is_terminated())
                    rfunc_.block(current_block_).insts.push_back(branch_ref);
                inst_map_[sref] = branch_ref;

                current_block_ = true_block;
                visit_block(inst.then_block);
                jump_to(merge_block, sinst.location);

                if (inst.else_block) {
                    current_block_ = false_block;
                    visit_block(*inst.else_block);
                    jump_to(merge_block, sinst.location);
                }

                current_block_ = merge_block;
            },
            [&](const SemInst::Loop& inst) {
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
            },
            [&](const SemInst::Return& inst) {
                std::optional<ir::InstRef> val;
                if (inst.value) {
                    val = get_cast(
                        *inst.value, sfunc_->return_type, sinst.location
                    );
                }
                emit(ir::Inst::Return {val});
            },
            [&](const SemInst::Break& inst) {
                if (!loops_.empty()) {
                    jump_to(loops_.back().break_target, sinst.location);
                }
            },
            [&](const SemInst::Continue& inst) {
                if (!loops_.empty()) {
                    jump_to(loops_.back().continue_target, sinst.location);
                }
            },
            [&](const SemInst::GetAttr& inst) {
                auto base_ref = get_mapped(inst.value);
                auto base_type = apackage_->inst_types[inst.value].type;

                const auto& defined_struct =
                    apackage_->ir_package->types()
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
            },
            [&](const SemInst::SetAttr& inst) {
                auto base_ref = get_mapped(inst.var);
                auto base_type = apackage_->inst_types[inst.var].type;

                const auto& defined_struct =
                    apackage_->ir_package->types()
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
            },
            [&](const SemInst::GetItem& inst) {
                emit(
                    ir::Inst::GetItem {
                        .value = get_mapped(inst.value),
                        .index = get_mapped(inst.index)
                    }
                );
            },
            [&](const SemInst::SetItem& inst) {
                auto base_type = apackage_->inst_types[inst.var].type;
                types::TypeId item_type = types::None;
                if (base_type != types::None) {
                    const auto& defined_type =
                        apackage_->ir_package->types().get(base_type);
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
            },
            [&](const SemInst::As& inst) {
                emit(
                    ir::Inst::Cast {
                        .value = get_mapped(inst.value),
                    }
                );
            },
            [&](const SemInst::Logical& inst) {
                auto right_block = rfunc_.add_block(ir::Block {});
                auto merge_block = rfunc_.add_block(ir::Block {});

                ir::Inst branch;
                branch.type = apackage_->inst_types[sref];
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
            },
            [&](const SemInst::Array& inst) {
                auto s_args = apackage_->ir_package->inst_refs(inst.items);
                auto array_type = apackage_->inst_types[sref].type;
                types::TypeId item_type = types::None;
                if (array_type != types::None) {
                    const auto& defined_type =
                        apackage_->ir_package->types().get(array_type);
                    if (auto at =
                            defined_type.data.get_if<types::Type::Array>()) {
                        item_type = at->item.type;
                    }
                }

                std::vector<ir::InstRef> r_args;
                r_args.reserve(s_args.size());
                for (auto s_arg : s_args) {
                    r_args.push_back(get_mapped(s_arg));
                }
                ir::InstRefs refs = rfunc_.add(r_args);
                emit(ir::Inst::Array {refs});
            },
            [&](const SemInst::AddressOf& inst) {
                emit(ir::Inst::AddressOf {get_mapped(inst.value)});
            },
            [&](const SemInst::Deref& inst) {
                emit(ir::Inst::Deref {get_mapped(inst.value)});
            }
        );
    }
};

ir::Module generate(acu::ir::AnalyzedPackage& analyzed_package) {
    ir::Module rmod(
        analyzed_package.ir_package->name(),
        analyzed_package.ir_package->types()
    );
    for (const auto& sfunc : analyzed_package.ir_package->funcs()) {
        FuncGenerator fg(analyzed_package, sfunc);
        rmod.add(fg.generate());
    }
    for (const auto& ufunc : analyzed_package.ir_package->used_funcs()) {
        rmod.add(
            ir::UsedFunc {
                .module = ir::ModuleRef {ufunc.package.index},
                .func = ir::FuncRef {.index = ufunc.func.index},
                .type = ufunc.type
            }
        );
    }
    return rmod;
}

}  // namespace acu::refanal
