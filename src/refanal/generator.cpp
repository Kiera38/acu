#include "refanal/generator.h"

#include <cassert>

#include "builder.h"
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
    ir::Builder builder_;

    struct LoopTargets {
        ir::BlockRef continue_target;
        ir::BlockRef break_target;
    };

    struct Value {
        utils::
            Variant<std::monostate, ir::Const, ir::LocalRef, ir::PlaceBuilder>
                value;

        Value() = default;
        Value(ir::Const c) : value(c) {}
        Value(ir::LocalRef lr) : value(lr) {}
        Value(ir::PlaceBuilder pb) : value(pb) {}

        ir::Operand as_operand(ir::Builder& builder, Location loc) const {
            return value.visit(
                [&](std::monostate) { return builder.op(ir::Const {false}); },
                [&](const ir::Const& c) { return builder.op(c); },
                [&](ir::LocalRef lr) { return builder.op(lr); },
                [&](const ir::PlaceBuilder& pb) {
                    return builder.op(pb.build());
                }
            );
        }

        ir::PlaceBuilder as_place_builder(
            ir::Builder& builder, types::SpecType type, Location loc
        ) const {
            return value.visit(
                [&](std::monostate) {
                    return builder.build_place(ir::LocalRef {0});
                },
                [&](const ir::Const& c) {
                    return builder.build_place(
                        builder.assign_use(type, builder.op(c), loc)
                    );
                },
                [&](ir::LocalRef lr) { return builder.build_place(lr); },
                [&](const ir::PlaceBuilder& pb) { return pb; }
            );
        }

        ir::LocalRef as_local(
            ir::Builder& builder, types::SpecType type, Location loc
        ) const {
            return value.visit(
                [&](std::monostate) { return ir::LocalRef {0}; },
                [&](const ir::Const& c) {
                    return builder.assign_use(type, builder.op(c), loc);
                },
                [&](ir::LocalRef lr) { return lr; },
                [&](const ir::PlaceBuilder& pb) {
                    return builder.assign_use(
                        type, builder.op(pb.build()), loc
                    );
                }
            );
        }
    };

    std::vector<LoopTargets> loops_;
    IndexMap<SemInstRef, Value> values_;
    std::uint32_t current_inst_ = 0;

public:
    FuncGenerator(
        const acu::ir::AnalyzedPackage& apackage, const ::acu::ir::Func& sfunc
    )
        : apackage_(&apackage),
          sfunc_(&sfunc),
          rfunc_(
              sfunc.name,
              *sfunc.source,
              sfunc.location,
              sfunc.is_extern,
              get_params(apackage, sfunc),
              sfunc.return_type
          ),
          builder_(rfunc_),
          values_(sfunc.insts, Value {}) {}

    ir::Func generate() {
        if (sfunc_->is_extern) {
            return std::move(rfunc_);
        }

        builder_.set_block(builder_.add_block());

        if (!sfunc_->insts.empty()) {
            current_inst_ = sfunc_->insts.start;
            ::acu::ir::Block main_block {
                .end = {sfunc_->insts.start + sfunc_->insts.size - 1}
            };
            visit_block(main_block);
        }

        if (!is_terminated()) {
            builder_.ret({}, Location {});
        }

        rfunc_.rebuild_cfg();
        return std::move(rfunc_);
    }

private:
    static std::vector<ir::Local> get_params(
        const acu::ir::AnalyzedPackage& apackage, const ::acu::ir::Func& sfunc
    ) {
        std::vector<ir::Local> rparams;
        rparams.reserve(sfunc.params.size);
        for (auto i : sfunc.params) {
            const auto& param = apackage.ir_package->param(i);
            rparams.push_back(
                ir::Local {.name = param.name, .type = param.type}
            );
        }
        return rparams;
    }

    void visit_block(acu::ir::Block sblock) {
        while (current_inst_ <= sblock.end.index) {
            visit_inst(SemInstRef {current_inst_++});
        }
    }

    [[nodiscard]] bool is_terminated() const {
        if (builder_.current_block().index == ~0u) return true;
        return rfunc_.block(builder_.current_block()).terminator.has_value();
    }

    [[nodiscard]] bool is_reference(types::SpecType t) const {
        return t.specifier == types::Specifier::Let ||
               t.specifier == types::Specifier::Var;
    }

    ir::Operand get_operand(SemInstRef sref, Location loc) {
        return values_[sref].as_operand(builder_, loc);
    }

    Value get_cast(
        acu::ir::ParamRef param, types::SpecType expected_type, Location loc
    ) {
        auto actual_type = apackage_->ir_package->param(param).type;
        Value v {ir::LocalRef {param.index}};
        return get_cast(v, actual_type, expected_type, loc);
    }

    Value get_cast(
        Value& v,
        types::SpecType actual_type,
        types::SpecType expected_type,
        Location loc
    ) {
        if (actual_type.type != expected_type.type) {
            v = Value {builder_.assign_cast(
                expected_type, v.as_operand(builder_, loc), loc
            )};
            actual_type = {
                .type = expected_type.type,
                .specifier = types::Specifier::Val,
            };
        }

        if (is_reference(actual_type) && !is_reference(expected_type)) {
            v = Value {v.as_place_builder(builder_, actual_type, loc).deref()};
            actual_type.specifier = types::Specifier::Val;
        }
        if (!is_reference(actual_type) && is_reference(expected_type)) {
            v = Value {builder_.assign_ref(
                expected_type,
                v.as_place_builder(builder_, actual_type, loc).build(),
                loc
            )};
            actual_type.specifier = expected_type.specifier;
        }

        return v;
    }

    Value get_cast(
        SemInstRef sref, types::SpecType expected_type, Location loc
    ) {
        auto actual_type = apackage_->inst_types[sref];
        Value v = values_[sref];
        return get_cast(v, actual_type, expected_type, loc);
    }

    uint32_t get_field_index(types::TypeId struct_type, std::string_view name) {
        const auto& type_def = apackage_->ir_package->types().get(struct_type);
        const auto& s = type_def.data.get<types::Type::Struct>();
        for (uint32_t i = 0; i < s.fields.size(); ++i) {
            if (s.fields[i].name == name) return i;
        }
        return 0;
    }

    ir::Const convert_const(const SemInst::Const::Value& val) {
        return val.visit(
            [&](bool v) -> ir::Const { return {v}; },
            [&](std::int64_t v) -> ir::Const { return {v}; },
            [&](double v) -> ir::Const { return {v}; },
            [&](char32_t v) -> ir::Const { return {v}; },
            [&](std::string_view v) -> ir::Const { return {v}; },
            [&](::acu::ir::FuncRef v) -> ir::Const {
                return {ir::FuncRef {v.index}};
            },
            [&](::acu::ir::UsedFuncRef v) -> ir::Const {
                return {ir::UsedFuncRef {.index = v.index}};
            },
            [&](types::TypeId) -> ir::Const { return {false}; }
        );
    }

    void visit_inst(SemInstRef sref) {
        if (is_terminated()) return;

        const auto& sinst = apackage_->ir_package->inst(sref);
        const auto& type = apackage_->inst_types[sref];

        sinst.data.visit(
            [&](const SemInst::Const& c) {
                values_[sref] = Value {convert_const(c.value)};
            },
            [&](const SemInst::VarDecl& inst) {
                values_[sref] = Value {builder_.add_local(type, inst.name)};
            },
            [&](const SemInst::LoadVar& inst) {
                values_[sref] = get_cast(inst.var, type, sinst.location);
            },
            [&](const SemInst::LoadParam& inst) {
                values_[sref] = get_cast(inst.param, type, sinst.location);
            },
            [&](const SemInst::Store& inst) {
                auto p = values_[inst.var]
                             .as_place_builder(
                                 builder_,
                                 apackage_->inst_types[inst.var],
                                 sinst.location
                             )
                             .build();
                auto v = get_cast(
                    inst.value, apackage_->inst_types[inst.var], sinst.location
                );
                builder_.assign(
                    p,
                    builder_.r_use(v.as_operand(builder_, sinst.location)),
                    sinst.location
                );
            },
            [&](const SemInst::Binary& inst) {
                auto val_type = type;
                val_type.specifier = types::Specifier::Val;
                values_[sref] = Value {builder_.assign_binary(
                    val_type,
                    static_cast<ir::BinaryOp>(inst.op),
                    get_cast(inst.left, val_type, sinst.location)
                        .as_operand(builder_, sinst.location),
                    get_cast(inst.right, val_type, sinst.location)
                        .as_operand(builder_, sinst.location),
                    sinst.location
                )};
            },
            [&](const SemInst::Unary& inst) {
                auto val_type = type;
                val_type.specifier = types::Specifier::Val;
                values_[sref] = Value {builder_.assign_unary(
                    val_type,
                    static_cast<ir::UnaryOp>(inst.op),
                    get_cast(inst.value, val_type, sinst.location)
                        .as_operand(builder_, sinst.location),
                    sinst.location
                )};
            },
            [&](const SemInst::Comparison& inst) {
                auto comparators =
                    apackage_->ir_package->comparators(inst.comparators);
                if (comparators.empty()) return;

                if (comparators.size() == 1) {
                    auto& comp = comparators[0];
                    types::SpecType comp_type = {
                        .type = comp.type, .specifier = types::Specifier::Val
                    };
                    auto l = get_cast(inst.left, comp_type, sinst.location);
                    visit_block(comp.value);
                    auto r =
                        get_cast(comp.value.end, comp_type, sinst.location);
                    values_[sref] = Value {builder_.assign_comp(
                        comp_type,
                        static_cast<ir::ComparisonOp>(comp.op),
                        l.as_operand(builder_, sinst.location),
                        r.as_operand(builder_, sinst.location),
                        sinst.location
                    )};
                } else {
                    auto& comp = comparators[0];
                    types::SpecType comp_type = {
                        .type = comp.type, .specifier = types::Specifier::Val
                    };
                    auto l = get_cast(inst.left, comp_type, sinst.location);
                    visit_block(comp.value);
                    auto r =
                        get_cast(comp.value.end, comp_type, sinst.location);
                    auto res = builder_.assign_comp(
                        comp_type,
                        static_cast<ir::ComparisonOp>(comp.op),
                        l.as_operand(builder_, sinst.location),
                        r.as_operand(builder_, sinst.location),
                        sinst.location
                    );
                    values_[sref] = Value {res};
                    for (size_t i = 1; i < comparators.size(); ++i) {
                        visit_block(comparators[i].value);
                    }
                }
            },
            [&](const SemInst::Call& inst) {
                const auto& callee_inst =
                    apackage_->ir_package->inst(inst.value);
                auto s_args = apackage_->ir_package->inst_refs(inst.args);
                std::vector<ir::Operand> r_args;
                r_args.reserve(s_args.size());

                if (auto* c = callee_inst.data.get_if<SemInst::Const>()) {
                    if (auto* t = c->value.get_if<types::TypeId>()) {
                        const auto& struct_type =
                            apackage_->ir_package->types().get(*t);
                        const auto& s =
                            struct_type.data.get<types::Type::Struct>();
                        for (size_t i = 0; i < s.fields.size(); ++i) {
                            r_args.push_back(
                                get_cast(
                                    s_args[i], s.fields[i].type, sinst.location
                                )
                                    .as_operand(builder_, sinst.location)
                            );
                        }
                        values_[sref] = Value {builder_.assign_struct(
                            {.type = *t, .specifier = types::Specifier::Val},
                            r_args,
                            sinst.location
                        )};
                        return;
                    }
                }

                auto func_type = apackage_->inst_types[inst.value].type;
                if (func_type != types::None) {
                    const auto& f_type =
                        apackage_->ir_package->types().get(func_type);
                    const auto& defined_func =
                        f_type.data.get<types::Type::Func>();
                    for (size_t i = 0; i < s_args.size(); ++i) {
                        r_args.push_back(
                            get_cast(
                                s_args[i],
                                defined_func.params[i].type,
                                sinst.location
                            )
                                .as_operand(builder_, sinst.location)
                        );
                    }
                    values_[sref] = Value {builder_.assign_call(
                        type,
                        get_operand(inst.value, sinst.location),
                        r_args,
                        sinst.location
                    )};
                }
            },
            [&](const SemInst::If& inst) {
                auto then_block = builder_.add_block();
                auto else_block =
                    inst.else_block ? builder_.add_block() : ir::BlockRef {~0u};
                auto merge_block = builder_.add_block();

                auto cond = get_cast(
                    inst.value,
                    {.type = types::Bool, .specifier = types::Specifier::Val},
                    sinst.location
                );
                builder_.branch(
                    cond.as_operand(builder_, sinst.location),
                    then_block,
                    inst.else_block ? else_block : merge_block,
                    sinst.location
                );

                builder_.set_block(then_block);
                visit_block(inst.then_block);
                if (!is_terminated()) {
                    builder_.jump(merge_block);
                }

                if (inst.else_block) {
                    builder_.set_block(else_block);
                    visit_block(*inst.else_block);
                    if (!is_terminated()) {
                        builder_.jump(merge_block);
                    }
                }

                builder_.set_block(merge_block);
            },
            [&](const SemInst::Loop& inst) {
                auto header = builder_.add_block();
                auto exit = builder_.add_block();

                builder_.jump(header);
                builder_.set_block(header);

                loops_.push_back(
                    {.continue_target = header, .break_target = exit}
                );
                visit_block(inst.block);
                if (!is_terminated()) {
                    builder_.jump(header);
                }
                loops_.pop_back();

                builder_.set_block(exit);
            },
            [&](const SemInst::Return& inst) {
                std::optional<ir::Operand> val;
                if (inst.value) {
                    val = get_cast(
                              *inst.value, sfunc_->return_type, sinst.location
                    )
                              .as_operand(builder_, sinst.location);
                }
                builder_.ret(val, sinst.location);
            },
            [&](const SemInst::Break&) {
                if (!loops_.empty()) {
                    builder_.jump(loops_.back().break_target, sinst.location);
                }
            },
            [&](const SemInst::Continue&) {
                if (!loops_.empty()) {
                    builder_.jump(
                        loops_.back().continue_target, sinst.location
                    );
                }
            },
            [&](const SemInst::GetAttr& inst) {
                values_[sref] = Value {
                    values_[inst.value]
                        .as_place_builder(
                            builder_,
                            apackage_->inst_types[inst.value],
                            sinst.location
                        )
                        .field(get_field_index(
                            apackage_->inst_types[inst.value].type, inst.name
                        ))
                };
            },
            [&](const SemInst::SetAttr& inst) {
                auto p =
                    values_[inst.value]
                        .as_place_builder(
                            builder_,
                            apackage_->inst_types[inst.value],
                            sinst.location
                        )
                        .field(get_field_index(
                            apackage_->inst_types[inst.value].type, inst.name
                        ))
                        .build();
                builder_.assign(
                    p,
                    builder_.r_use(get_operand(inst.value, sinst.location)),
                    sinst.location
                );
            },
            [&](const SemInst::GetItem& inst) {
                values_[sref] =
                    Value {values_[inst.value]
                               .as_place_builder(
                                   builder_,
                                   apackage_->inst_types[inst.value],
                                   sinst.location
                               )
                               .index(
                                   values_[inst.index].as_local(
                                       builder_,
                                       {.type = types::UInt,
                                        .specifier = types::Specifier::Val},
                                       sinst.location
                                   )
                               )};
            },
            [&](const SemInst::SetItem& inst) {
                builder_.assign(
                    values_[inst.value]
                        .as_place_builder(
                            builder_,
                            apackage_->inst_types[inst.value],
                            sinst.location
                        )
                        .index(
                            values_[inst.index].as_local(
                                builder_,
                                {.type = types::UInt,
                                 .specifier = types::Specifier::Val},
                                sinst.location
                            )
                        )
                        .build(),
                    builder_.r_use(get_operand(inst.value, sinst.location)),
                    sinst.location
                );
            },
            [&](const SemInst::Deref& inst) {
                values_[sref] =
                    Value {values_[inst.value]
                               .as_place_builder(
                                   builder_,
                                   apackage_->inst_types[inst.value],
                                   sinst.location
                               )
                               .deref()};
            },
            [&](const SemInst::AddressOf& inst) {
                values_[sref] = Value {builder_.assign_addr_of(
                    apackage_->inst_types[sref],
                    values_[inst.value]
                        .as_place_builder(
                            builder_,
                            apackage_->inst_types[inst.value],
                            sinst.location
                        )
                        .build(),
                    sinst.location
                )};
            },
            [&](const SemInst::Array& inst) {
                auto s_items = apackage_->ir_package->inst_refs(inst.items);
                std::vector<ir::Operand> r_items;
                r_items.reserve(s_items.size());

                const auto& arr = apackage_->ir_package->types()
                                      .get(type.type)
                                      .data.get<types::Type::Array>();

                for (auto s_item : s_items) {
                    r_items.push_back(
                        get_cast(s_item, arr.item, sinst.location)
                            .as_operand(builder_, sinst.location)
                    );
                }

                values_[sref] = Value {
                    builder_.assign_array(type, r_items, sinst.location)
                };
            },
            [&](const SemInst::As& inst) {
                values_[sref] = get_cast(inst.value, type, sinst.location);
            },
            [&](const SemInst::Logical& inst) {
                auto res = builder_.add_local(type);
                auto val_a = get_operand(inst.left, sinst.location);
                builder_.assign(
                    builder_.place(res), builder_.r_use(val_a), sinst.location
                );

                auto right_block = builder_.add_block();
                auto merge_block = builder_.add_block();

                auto cond = get_cast(
                    inst.left,
                    {.type = types::Bool, .specifier = types::Specifier::Val},
                    sinst.location
                );

                if (inst.op == ::acu::ir::Inst::LogicalOp::And) {
                    builder_.branch(
                        cond.as_operand(builder_, sinst.location),
                        right_block,
                        merge_block,
                        sinst.location
                    );
                } else {
                    builder_.branch(
                        cond.as_operand(builder_, sinst.location),
                        merge_block,
                        right_block,
                        sinst.location
                    );
                }

                builder_.set_block(right_block);
                visit_block(inst.right);
                auto val_b = get_operand(inst.right.end, sinst.location);
                builder_.assign(
                    builder_.place(res), builder_.r_use(val_b), sinst.location
                );
                builder_.jump(merge_block);

                builder_.set_block(merge_block);
                values_[sref] = Value {res};
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
