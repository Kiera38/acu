#include "refanal/generator.h"

#include <cassert>
#include <string_view>

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
    const acu::ir::AFunc* afunc_;
    const acu::ir::Func* sfunc_;
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

        ir::OperandRef as_operand(
            ir::Builder& builder, types::SpecType type, Location loc
        ) const {
            return value.visit(
                [&](std::monostate) {
                    return builder.op(ir::Const {false}, type.specifier);
                },
                [&](const ir::Const& c) {
                    return builder.op(c, type.specifier);
                },
                [&](ir::LocalRef lr) { return builder.op(lr, type.specifier); },
                [&](const ir::PlaceBuilder& pb) {
                    return builder.op(pb.build(), type.specifier);
                }
            );
        }

        ir::PlaceBuilder as_place_builder(
            ir::Builder& builder, types::SpecType type, Location loc
        ) const {
            return value.visit(
                [&](std::monostate) {
                    return builder.build_place(builder.add_local(type));
                },
                [&](const ir::Const& c) {
                    return builder.build_place(builder.assign_use(
                        type, builder.op(c, type.specifier), loc
                    ));
                },
                [&](ir::LocalRef lr) { return builder.build_place(lr); },
                [&](const ir::PlaceBuilder& pb) { return pb; }
            );
        }

        ir::LocalRef as_local(
            ir::Builder& builder, types::SpecType type, Location loc
        ) const {
            return value.visit(
                [&](std::monostate) { return builder.add_local(type); },
                [&](const ir::Const& c) {
                    return builder.assign_use(
                        type, builder.op(c, type.specifier), loc
                    );
                },
                [&](ir::LocalRef lr) { return lr; },
                [&](const ir::PlaceBuilder& pb) {
                    return builder.assign_use(
                        type, builder.op(pb.build(), type.specifier), loc
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
        const acu::ir::AnalyzedPackage& apackage, acu::ir::AFuncRef aref
    )
        : apackage_(&apackage),
          afunc_(&apackage.funcs_[aref]),
          sfunc_(&apackage.ir_package->func(afunc_->func)),
          rfunc_(
              sfunc_->name,
              *sfunc_->source,
              sfunc_->location,
              sfunc_->is_extern,
              sfunc_->is_public,
              afunc_->type,
              get_params(apackage, *sfunc_),
              sfunc_->return_type
          ),
          builder_(rfunc_),
          values_(sfunc_->insts, Value {}) {}

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

    [[nodiscard]] types::SpecType get_item_type(types::TypeId type_id) const {
        const auto& type = apackage_->ir_package->types().get(type_id);
        if (auto ptr = type.data.get_if<types::Type::Ptr>()) {
            return ptr->type;
        }
        return type.data.get<types::Type::Array>().item;
    }

    [[nodiscard]] ir::OperandRef get_operand(
        SemInstRef sref, types::SpecType type, Location loc
    ) {
        return values_[sref].as_operand(builder_, type, loc);
    }

    Value get_cast(
        acu::ir::ParamRef param, types::SpecType expected_type, Location loc
    ) {
        auto actual_type = apackage_->ir_package->param(param).type;
        Value v {ir::LocalRef {param.index}};
        return get_cast(v, actual_type, expected_type, loc);
    }

    Value& get_cast(
        Value& v,
        types::SpecType actual_type,
        types::SpecType expected_type,
        Location loc
    ) {
        if (actual_type.type != expected_type.type) {
            const auto& type =
                apackage_->ir_package->types().get(actual_type.type);
            v = Value {builder_.assign_cast(
                expected_type,
                v.as_operand(
                    builder_,
                    {.type = actual_type.type,
                     .specifier = type.data.is<types::Type::Array>()
                                      ? types::Specifier::Let
                                      : types::Specifier::Val},
                    loc
                ),
                loc
            )};
        }

        return v;
    }

    Value get_cast(
        SemInstRef sref, types::SpecType expected_type, Location loc
    ) {
        auto actual_type = afunc_->types[sref];
        Value v = values_[sref];
        return get_cast(v, actual_type, expected_type, loc);
    }

    ir::OperandRef get_operand_cast(
        SemInstRef sref, types::SpecType expected_type, Location loc
    ) {
        return get_cast(sref, expected_type, loc)
            .as_operand(builder_, expected_type, loc);
    }

    const types::Type::StructField& get_field(
        types::TypeId struct_type, std::string_view name
    ) {
        const auto& type = apackage_->ir_package->types().get(struct_type);
        std::span<const types::Type::StructField> fields;
        if (auto s = type.data.get_if<types::Type::Struct>()) {
            fields = s->fields;
        } else {
            fields = type.data.get<types::Type::UsedStruct>().fields();
        }

        for (uint32_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == name) return fields[i];
        }
        assert(false && "Field not found");
        static types::Type::StructField empty;
        return empty;
    }

    uint32_t get_field_index(types::TypeId struct_type, std::string_view name) {
        const auto& type = apackage_->ir_package->types().get(struct_type);
        std::span<const types::Type::StructField> fields;
        if (auto s = type.data.get_if<types::Type::Struct>()) {
            fields = s->fields;
        } else {
            fields = type.data.get<types::Type::UsedStruct>().fields();
        }

        for (uint32_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == name) return i;
        }
        assert(false && "Field not found");
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
                return {ir::UsedFuncRef {v.index}};
            },
            [&](types::TypeId) -> ir::Const { return {false}; }
        );
    }

    void visit_inst(SemInstRef sref) {
        if (is_terminated()) return;

        const auto& sinst = apackage_->ir_package->inst(sref);
        const auto& type = afunc_->types[sref];

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
                auto pb = values_[inst.var].as_place_builder(
                    builder_, afunc_->types[inst.var], sinst.location
                );
                auto p = pb.build();
                auto v = get_operand_cast(
                    inst.value, afunc_->types[inst.var], sinst.location
                );
                builder_.assign(p, builder_.r_use(v), sinst.location);
            },
            [&](const SemInst::Binary& inst) {
                auto val_type = type;
                val_type.specifier = types::Specifier::Val;
                values_[sref] = Value {builder_.assign_binary(
                    val_type,
                    static_cast<ir::BinaryOp>(inst.op),
                    get_operand_cast(inst.left, val_type, sinst.location),
                    get_operand_cast(inst.right, val_type, sinst.location),
                    sinst.location
                )};
            },
            [&](const SemInst::Unary& inst) {
                auto val_type = type;
                val_type.specifier = types::Specifier::Val;
                values_[sref] = Value {builder_.assign_unary(
                    val_type,
                    static_cast<ir::UnaryOp>(inst.op),
                    get_operand_cast(inst.value, val_type, sinst.location),
                    sinst.location
                )};
            },
            [&](const SemInst::Comparison& inst) {
                if (inst.comparators.size == 1) {
                    auto& comp =
                        apackage_->ir_package->comparator(inst.comparators[0]);
                    types::SpecType comp_type = {
                        .type =
                            afunc_->comparator_types[{inst.comparators.start}],
                        .specifier = types::Specifier::Val
                    };
                    auto l =
                        get_operand_cast(inst.left, comp_type, sinst.location);
                    visit_block(comp.value);
                    auto r = get_operand_cast(
                        comp.value.end, comp_type, sinst.location
                    );
                    values_[sref] = Value {builder_.assign_comp(
                        type,
                        static_cast<ir::ComparisonOp>(comp.op),
                        l,
                        r,
                        sinst.location
                    )};
                    return;
                }

                auto res_local = builder_.add_local(type);
                auto merge_block = builder_.add_block();
                auto current_left_ref = inst.left;

                for (size_t i = 0; i < inst.comparators.size; ++i) {
                    auto comp_ref = inst.comparators[i];
                    types::SpecType comp_type = {
                        .type = afunc_->comparator_types[comp_ref],
                        .specifier = types::Specifier::Val
                    };

                    auto l = get_operand_cast(
                        current_left_ref, comp_type, sinst.location
                    );
                    const auto& comp =
                        apackage_->ir_package->comparator(comp_ref);
                    visit_block(comp.value);
                    auto r = get_operand_cast(
                        comp.value.end, comp_type, sinst.location
                    );

                    auto cmp_res = builder_.assign_comp(
                        type,
                        static_cast<ir::ComparisonOp>(comp.op),
                        l,
                        r,
                        sinst.location
                    );

                    builder_.assign(
                        builder_.place(res_local),
                        builder_.r_use(
                            builder_.op(cmp_res, types::Specifier::Val)
                        ),
                        sinst.location
                    );

                    if (i < inst.comparators.size - 1) {
                        auto next_block = builder_.add_block();
                        builder_.branch(
                            builder_.op(cmp_res, types::Specifier::Val),
                            next_block,
                            merge_block,
                            sinst.location
                        );
                        builder_.set_block(next_block);
                    }
                    current_left_ref = comp.value.end;
                }

                builder_.jump(merge_block);
                builder_.set_block(merge_block);
                values_[sref] = Value {res_local};
            },
            [&](const SemInst::Call& inst) {
                const auto& callee_inst =
                    apackage_->ir_package->inst(inst.value);
                auto s_args = apackage_->ir_package->inst_refs(inst.args);
                std::vector<ir::OperandRef> r_args;
                r_args.reserve(s_args.size());

                if (auto* c = callee_inst.data.get_if<SemInst::Const>()) {
                    if (auto* t = c->value.get_if<types::TypeId>()) {
                        const auto& struct_type =
                            apackage_->ir_package->types().get(*t);
                        const auto& s =
                            struct_type.data.get<types::Type::Struct>();
                        for (size_t i = 0; i < s.fields.size(); ++i) {
                            r_args.push_back(get_operand_cast(
                                s_args[i], s.fields[i].type, sinst.location
                            ));
                        }
                        values_[sref] = Value {builder_.assign_struct(
                            {.type = *t, .specifier = types::Specifier::Val},
                            r_args,
                            sinst.location
                        )};
                        return;
                    }
                }

                auto func_type = afunc_->types[inst.value];
                const auto& f_type =
                    apackage_->ir_package->types().get(func_type.type);
                const auto& defined_func = f_type.data.get<types::Type::Func>();
                for (size_t i = 0; i < s_args.size(); ++i) {
                    r_args.push_back(get_operand_cast(
                        s_args[i], defined_func.params[i].type, sinst.location
                    ));
                }
                values_[sref] = Value {builder_.assign_call(
                    type,
                    get_operand(inst.value, func_type, sinst.location),
                    r_args,
                    sinst.location
                )};
            },
            [&](const SemInst::If& inst) {
                auto then_block = builder_.add_block();
                auto else_block =
                    inst.else_block ? builder_.add_block() : ir::BlockRef {~0u};
                auto merge_block = builder_.add_block();

                auto cond = get_operand_cast(
                    inst.value,
                    {.type = types::Bool, .specifier = types::Specifier::Val},
                    sinst.location
                );
                builder_.branch(
                    cond,
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
                std::optional<ir::OperandRef> val;
                if (inst.value) {
                    val = get_operand_cast(
                        *inst.value, sfunc_->return_type, sinst.location
                    );
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
                auto type = afunc_->types[inst.value];
                auto pb = values_[inst.value].as_place_builder(
                    builder_, type, sinst.location
                );
                values_[sref] =
                    Value {pb.field(get_field_index(type.type, inst.name))};
            },
            [&](const SemInst::SetAttr& inst) {
                auto type = afunc_->types[inst.var];
                auto pb = values_[inst.var].as_place_builder(
                    builder_, type, sinst.location
                );
                const auto& field = get_field(type.type, inst.name);
                auto p =
                    pb.field(get_field_index(type.type, inst.name)).build();
                builder_.assign(
                    p,
                    builder_.r_use(
                        get_operand_cast(inst.value, field.type, sinst.location)
                    ),
                    sinst.location
                );
            },
            [&](const SemInst::GetItem& inst) {
                auto type = afunc_->types[inst.value];
                auto pb = values_[inst.value].as_place_builder(
                    builder_, type, sinst.location
                );
                values_[sref] = Value {pb.index(
                    values_[inst.index].as_operand(
                        builder_,
                        {.type = types::UInt,
                         .specifier = types::Specifier::Val},
                        sinst.location
                    )
                )};
            },
            [&](const SemInst::SetItem& inst) {
                auto item_type = get_item_type(afunc_->types[inst.var].type);
                builder_.assign(
                    values_[inst.var]
                        .as_place_builder(
                            builder_, afunc_->types[inst.var], sinst.location
                        )
                        .index(get_operand_cast(
                            inst.index,
                            {.type = types::UInt,
                             .specifier = types::Specifier::Val},
                            sinst.location
                        ))
                        .build(),
                    builder_.r_use(
                        get_operand_cast(inst.value, item_type, sinst.location)
                    ),
                    sinst.location
                );
            },
            [&](const SemInst::Deref& inst) {
                values_[sref] = Value {
                    values_[inst.value]
                        .as_place_builder(
                            builder_, afunc_->types[inst.value], sinst.location
                        )
                        .deref()
                };
            },
            [&](const SemInst::AddressOf& inst) {
                auto type = afunc_->types[inst.value];
                auto pb = values_[inst.value].as_place_builder(
                    builder_, type, sinst.location
                );
                values_[sref] = Value {builder_.assign_addr_of(
                    afunc_->types[sref], pb.build(), sinst.location
                )};
            },
            [&](const SemInst::Array& inst) {
                auto s_items = apackage_->ir_package->inst_refs(inst.items);
                std::vector<ir::OperandRef> r_items;
                r_items.reserve(s_items.size());

                const auto& arr = apackage_->ir_package->types()
                                      .get(type.type)
                                      .data.get<types::Type::Array>();

                for (auto s_item : s_items) {
                    r_items.push_back(
                        get_operand_cast(s_item, arr.item, sinst.location)
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
                auto val_a = get_operand_cast(inst.left, type, sinst.location);
                builder_.assign(
                    builder_.place(res), builder_.r_use(val_a), sinst.location
                );

                auto right_block = builder_.add_block();
                auto merge_block = builder_.add_block();

                auto cond = get_operand_cast(
                    inst.left,
                    {.type = types::Bool, .specifier = types::Specifier::Val},
                    sinst.location
                );

                if (inst.op == ::acu::ir::Inst::LogicalOp::And) {
                    builder_.branch(
                        cond, right_block, merge_block, sinst.location
                    );
                } else {
                    builder_.branch(
                        cond, merge_block, right_block, sinst.location
                    );
                }

                builder_.set_block(right_block);
                visit_block(inst.right);
                auto val_b =
                    get_operand_cast(inst.right.end, type, sinst.location);
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
    for (auto i : analyzed_package.funcs_.indices()) {
        FuncGenerator fg(analyzed_package, i);
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
