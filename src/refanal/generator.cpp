#include "refanal/generator.h"

#include "semanal/types.h"
#include "variant.h"

namespace acu::refanal {

class FuncGenerator {
    const semanal::AnalyzedModule* amod_;
    const semanal::AnalyzedFunc* afunc_;
    const ::acu::ir::Func* sfunc_;
    ir::Func rfunc_;

    ir::BlockRef current_block_ {};

    struct LoopTargets {
        ir::BlockRef continue_target;
        ir::BlockRef break_target;
    };
    std::vector<LoopTargets> loops_;
    std::vector<ir::InstRef> inst_map_;

public:
    FuncGenerator(
        const semanal::AnalyzedModule& amod,
        const semanal::AnalyzedFunc& afunc,
        const ::acu::ir::Func& sfunc
    )
        : amod_(&amod), afunc_(&afunc), sfunc_(&sfunc), rfunc_(sfunc.name()) {}

    ir::Func generate() {
        const auto& sparams = sfunc_->params();
        std::vector<ir::Param> rparams;
        for (const auto& sp : sparams) {
            rparams.push_back(ir::Param {sp.name, sp.type});
        }
        rfunc_.set_type(rparams, sfunc_->return_type());

        inst_map_.assign(sfunc_->insts().size(), ir::InstRef {~0u});

        current_block_ = rfunc_.add_block(ir::Block {});

        if (!sfunc_->insts().empty()) {
            ::acu::ir::Block main_block {
                .start = ::acu::ir::InstRef {0}, .end = sfunc_->last_inst()
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

        return std::move(rfunc_);
    }

    void visit_block(::acu::ir::Block sblock) {
        if (sblock.end.index < sblock.start.index) return;

        for (uint32_t i = sblock.start.index; i <= sblock.end.index; ++i) {
            uint32_t jump_idx = visit_inst(::acu::ir::InstRef {i});
            i = jump_idx;
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

        ir::InstRef jump_ref = rfunc_.add(jump);
        rfunc_.block(current_block_).insts.push_back(jump_ref);
    }

    ir::InstRef get_mapped(::acu::ir::InstRef sref) {
        return inst_map_[sref.index];
    }

    ir::InstRef get_mapped(std::optional<::acu::ir::InstRef> sref) {
        if (!sref) return ir::InstRef {~0u};
        return inst_map_[sref->index];
    }

    uint32_t visit_inst(::acu::ir::InstRef sref) {
        const auto& sinst = sfunc_->inst(sref);
        const auto& type = afunc_->inst_types[sref.index];

        auto emit = [&](auto data) -> ir::InstRef {
            ir::Inst rinst = {
                .data = std::move(data),
                .type = type,
                .location = sinst.location,
            };
            ir::InstRef rref = rfunc_.add(rinst);
            if (!is_terminated()) {
                rfunc_.block(current_block_).insts.push_back(rref);
            }
            inst_map_[sref.index] = rref;
            return rref;
        };

        return sinst.data.visit(
            [&](const ::acu::ir::Inst::Const& inst) -> uint32_t {
                ir::Inst::Const::Value v = inst.value.visit(
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
                return sref.index;
            },
            [&](const ::acu::ir::Inst::VarDecl& inst) -> uint32_t {
                emit(ir::Inst::VarDecl {inst.name});
                return sref.index;
            },
            [&](const ::acu::ir::Inst::LoadVar& inst) -> uint32_t {
                emit(ir::Inst::LoadVar {get_mapped(inst.var)});
                return sref.index;
            },
            [&](const ::acu::ir::Inst::LoadParam& inst) -> uint32_t {
                emit(ir::Inst::LoadParam {ir::ParamRef {inst.param.index}});
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Store& inst) -> uint32_t {
                emit(
                    ir::Inst::Store {
                        .var = get_mapped(inst.var),
                        .value = get_mapped(inst.value)
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Binary& inst) -> uint32_t {
                emit(
                    ir::Inst::Binary {
                        .left = get_mapped(inst.left),
                        .right = get_mapped(inst.right),
                        .op = static_cast<ir::Inst::BinaryOp>(inst.op)
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Unary& inst) -> uint32_t {
                emit(
                    ir::Inst::Unary {
                        .value = get_mapped(inst.value),
                        .op = static_cast<ir::Inst::UnaryOp>(inst.op)
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Comparison& inst) -> uint32_t {
                auto comparators = sfunc_->comparators(inst.comparators);
                if (comparators.empty()) return sref.index;

                ir::InstRef current_left = get_mapped(inst.left);
                uint32_t skip_idx = sref.index;

                ir::BlockRef merge_block {~0u};
                if (comparators.size() > 1) {
                    merge_block = rfunc_.add_block(ir::Block {});
                }

                for (size_t i = 0; i < comparators.size(); ++i) {
                    auto& comp = comparators[i];

                    visit_block(comp.value);
                    ir::InstRef right_ref = get_mapped({comp.value.end.index});

                    ir::Inst comp_inst {
                        .data =
                            ir::Inst::Comparison {
                                .left = current_left,
                                .right = right_ref,
                                .op =
                                    static_cast<ir::Inst::ComparisonOp>(comp.op)
                            },
                        .type = afunc_->inst_types[sref.index],
                        .location = sinst.location
                    };

                    ir::InstRef comp_ref = rfunc_.add(comp_inst);
                    if (!is_terminated()) {
                        rfunc_.block(current_block_).insts.push_back(comp_ref);
                    }

                    if (i == 0) {
                        inst_map_[sref.index] = comp_ref;
                    }

                    if (comp.value.start.index == skip_idx + 1) {
                        skip_idx = std::max(skip_idx, comp.value.end.index);
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
                            .type = afunc_->inst_types[sref.index],
                            .location = sinst.location
                        };
                        ir::InstRef branch_ref = rfunc_.add(branch);
                        if (!is_terminated()) {
                            rfunc_.block(current_block_)
                                .insts.push_back(branch_ref);
                        }
                        if (i == 0) {
                            inst_map_[sref.index] = branch_ref;
                        }

                        current_block_ = right_block;
                        current_left = right_ref;
                    }
                }

                if (comparators.size() > 1) {
                    jump_to(merge_block, sinst.location);
                    current_block_ = merge_block;
                }

                return skip_idx;
            },
            [&](const ::acu::ir::Inst::Call& inst) -> uint32_t {
                auto s_args = sfunc_->inst_refs(inst.args);
                std::vector<ir::InstRef> r_args;
                for (auto s_arg : s_args) r_args.push_back(get_mapped(s_arg));
                ir::InstRefs refs = rfunc_.add(r_args);

                const auto& callee_inst = sfunc_->inst(inst.value);
                if (callee_inst.data.is<::acu::ir::Inst::Const>()) {
                    const auto& const_val =
                        callee_inst.data.get<::acu::ir::Inst::Const>();
                    if (const_val.value.is<types::TypeId>()) {
                        auto struct_type = const_val.value.get<types::TypeId>();
                        emit(
                            ir::Inst::CreateStruct {
                                .struct_type = struct_type, .args = refs
                            }
                        );
                        return sref.index;
                    }
                }

                emit(
                    ir::Inst::Call {
                        .value = get_mapped(inst.value), .args = refs
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::If& inst) -> uint32_t {
                auto true_block = rfunc_.add_block(ir::Block {});
                ir::BlockRef false_block {~0u};
                if (inst.else_block) {
                    false_block = rfunc_.add_block(ir::Block {});
                }
                auto merge_block = rfunc_.add_block(ir::Block {});

                ir::Inst branch {
                    .data =
                        ir::Inst::Branch {
                            .condition = get_mapped(inst.value),
                            .true_target = true_block,
                            .false_target =
                                inst.else_block ? false_block : merge_block
                        },
                    .type = afunc_->inst_types[sref.index],
                    .location = sinst.location,
                };
                ir::InstRef branch_ref = rfunc_.add(branch);
                if (!is_terminated())
                    rfunc_.block(current_block_).insts.push_back(branch_ref);
                inst_map_[sref.index] = branch_ref;

                current_block_ = true_block;
                visit_block(inst.then_block);
                jump_to(merge_block, sinst.location);

                uint32_t skip_idx = sref.index;
                if (inst.then_block.start.index == sref.index + 1) {
                    skip_idx = std::max(skip_idx, inst.then_block.end.index);
                }

                if (inst.else_block) {
                    current_block_ = false_block;
                    visit_block(*inst.else_block);
                    jump_to(merge_block, sinst.location);
                    if (inst.else_block->start.index == skip_idx + 1) {
                        skip_idx =
                            std::max(skip_idx, inst.else_block->end.index);
                    }
                }

                current_block_ = merge_block;
                return skip_idx;
            },
            [&](const ::acu::ir::Inst::Loop& inst) -> uint32_t {
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
                uint32_t skip_idx = sref.index;
                if (inst.block.start.index == sref.index + 1) {
                    skip_idx = std::max(skip_idx, inst.block.end.index);
                }
                return skip_idx;
            },
            [&](const ::acu::ir::Inst::Return& inst) -> uint32_t {
                std::optional<ir::InstRef> val;
                if (inst.value) val = get_mapped(*inst.value);
                emit(ir::Inst::Return {val});
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Break& inst) -> uint32_t {
                if (!loops_.empty()) {
                    jump_to(loops_.back().break_target, sinst.location);
                }
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Continue& inst) -> uint32_t {
                if (!loops_.empty()) {
                    jump_to(loops_.back().continue_target, sinst.location);
                }
                return sref.index;
            },
            [&](const ::acu::ir::Inst::GetAttr& inst) -> uint32_t {
                auto base_ref = get_mapped(inst.value);
                auto base_type = afunc_->inst_types[inst.value.index].type;

                const auto& defined_struct =
                    amod_->ir_module.types()
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
                return sref.index;
            },
            [&](const ::acu::ir::Inst::SetAttr& inst) -> uint32_t {
                auto base_ref = get_mapped(inst.var);
                auto base_type = afunc_->inst_types[inst.var.index].type;

                const auto& defined_struct =
                    amod_->ir_module.types()
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
                    ir::Inst::SetField {
                        .var = base_ref,
                        .index = field_idx,
                        .value = get_mapped(inst.value)
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::GetItem& inst) -> uint32_t {
                emit(
                    ir::Inst::GetItem {
                        .value = get_mapped(inst.value),
                        .index = get_mapped(inst.index)
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::SetItem& inst) -> uint32_t {
                emit(
                    ir::Inst::SetItem {
                        .var = get_mapped(inst.var),
                        .index = get_mapped(inst.index),
                        .value = get_mapped(inst.value)
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::As& inst) -> uint32_t {
                emit(
                    ir::Inst::Cast {
                        .value = get_mapped(inst.value),
                        .target_type = inst.type
                    }
                );
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Logical& inst) -> uint32_t {
                // Short-circuit logical operation like `If`
                auto right_block = rfunc_.add_block(ir::Block {});
                auto merge_block = rfunc_.add_block(ir::Block {});

                ir::Inst branch;
                branch.type = afunc_->inst_types[sref.index];
                branch.location = sinst.location;
                if (inst.op == ::acu::ir::Inst::LogicalOp::And) {
                    branch.data = ir::Inst::Branch {
                        .condition = get_mapped(inst.left),
                        .true_target = right_block,
                        .false_target = merge_block
                    };
                } else {
                    branch.data = ir::Inst::Branch {
                        .condition = get_mapped(inst.left),
                        .true_target = merge_block,
                        .false_target = right_block
                    };
                }
                ir::InstRef branch_ref = rfunc_.add(branch);
                if (!is_terminated())
                    rfunc_.block(current_block_).insts.push_back(branch_ref);
                inst_map_[sref.index] =
                    branch_ref;  // Maybe we need a PHI here? Since this is
                                 // refanal IR, we might need a PHI node later,
                                 // but for now we just map it.

                current_block_ = right_block;
                visit_block(inst.right);
                jump_to(merge_block, sinst.location);

                current_block_ = merge_block;
                uint32_t skip_idx = sref.index;
                if (inst.right.start.index == sref.index + 1) {
                    skip_idx = std::max(skip_idx, inst.right.end.index);
                }
                return skip_idx;
            },
            [&](const ::acu::ir::Inst::Array& inst) -> uint32_t {
                auto s_args =
                    sfunc_->inst_refs(inst.items);  // wait, it's InstRefs
                std::vector<ir::InstRef> r_args;
                for (auto s_arg : s_args) r_args.push_back(get_mapped(s_arg));
                ir::InstRefs refs = rfunc_.add(r_args);
                emit(ir::Inst::Array {refs});
                return sref.index;
            },
            [&](const ::acu::ir::Inst::AddressOf& inst) -> uint32_t {
                emit(ir::Inst::AddressOf {get_mapped(inst.value)});
                return sref.index;
            },
            [&](const ::acu::ir::Inst::Deref& inst) -> uint32_t {
                emit(ir::Inst::Deref {get_mapped(inst.value)});
                return sref.index;
            }
        );
    }
};

ir::Module generate(const semanal::AnalyzedModule& analyzed_module) {
    ir::Module rmod;
    for (const auto& afunc : analyzed_module.analyzed_funcs) {
        const auto& sfunc = analyzed_module.ir_module.func(afunc.ref);
        FuncGenerator fg(analyzed_module, afunc, sfunc);
        rmod.add(fg.generate());
    }
    return rmod;
}

}  // namespace acu::refanal
