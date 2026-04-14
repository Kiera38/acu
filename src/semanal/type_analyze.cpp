#include <deque>
#include <format>
#include <optional>
#include <vector>

#include "errors.h"
#include "ir.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"
#include "semanal/types.h"
#include "source.h"

namespace acu::semanal {
namespace {
bool is_int(types::TypeId id) {
    return id.index >= types::Int8.index && id.index <= types::Int64.index;
}

bool is_uint(types::TypeId id) {
    return id.index >= types::UInt8.index && id.index <= types::UInt64.index;
}

bool is_float(types::TypeId id) {
    return id.index >= types::Float32.index && id.index <= types::Float64.index;
}

bool can_convert(types::TypeId from, types::TypeId to) {
    if (from == to) {
        return true;
    }

    if (from == types::Nothing) {
        return true;
    }

    if (is_int(from)) {
        if (is_int(to)) {
            return from.index <= to.index;
        }
        if (is_uint(to)) {
            return (to.index - types::UInt8.index) >
                   (from.index - types::Int8.index);
        }
        if (to == types::Bool || is_float(to)) {
            return true;
        }
    }

    if (is_uint(from)) {
        if (is_uint(to)) {
            return from.index <= to.index;
        }
        if (to == types::Bool || is_float(to)) {
            return true;
        }
    }

    if (is_float(from)) {
        if (is_float(to)) {
            return from.index <= to.index;
        }
    }

    return false;
}

bool can_cast(
    types::TypeId from, types::TypeId to, const types::TypePool& type_pool
) {
    if (can_convert(from, to)) {
        return true;
    }

    if ((is_int(from) || is_uint(from)) && (is_int(to) || is_uint(to))) {
        return true;
    }

    if (is_float(from) && is_float(to)) {
        return true;
    }

    const auto& from_tp = type_pool.get(from);
    const auto& to_tp = type_pool.get(to);

    if ((is_int(from) || is_uint(from)) && to_tp.data.is<types::Type::Ptr>()) {
        return true;
    }
    if (from_tp.data.is<types::Type::Ptr>() && (is_int(to) || is_uint(to))) {
        return true;
    }

    if (auto at = from_tp.data.get_if<types::Type::Array>()) {
        if (auto pt = to_tp.data.get_if<types::Type::Ptr>()) {
            if (at->item == pt->type) {
                return true;
            }
        }
    }

    return false;
}

std::optional<types::TypeId> unify(types::TypeId type1, types::TypeId type2) {
    if (type1 == type2) {
        return type1;
    }
    if (can_convert(type1, type2)) {
        return type2;
    }
    if (can_convert(type2, type1)) {
        return type1;
    }
    return std::nullopt;
}

struct TypeVar {
    std::optional<types::SpecType> type;
    std::optional<Location> define_loc;
    bool locked = false;

    types::TypeId add_type(
        types::TypeId tp,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    ) {
        if (locked) {
            if (type.has_value() && type->type != tp) {
                if (!can_convert(tp, type->type)) {
                    std::string hint = "";
                    if (can_cast(tp, type->type, pool)) {
                        hint = std::format(
                            "use 'as {}' for explicit conversion",
                            pool.to_string(*type)
                        );
                    }
                    err_handler.error(
                        source,
                        loc,
                        std::format(
                            "Type mismatch: cannot convert {} to {}",
                            pool.to_string(tp),
                            pool.to_string(*type)
                        ),
                        hint
                    );
                }
            }
        } else {
            if (type.has_value()) {
                auto unified = unify(type->type, tp);
                if (!unified.has_value()) {
                    err_handler.error(
                        source,
                        loc,
                        std::format(
                            "Type mismatch: cannot unify {} and {}",
                            pool.to_string(tp),
                            pool.to_string(*type)
                        )
                    );
                } else {
                    type = {.type = *unified, .specifier = type->specifier};
                }
            } else {
                type = {.type = tp, .specifier = types::Specifier::None};
                define_loc = loc;
            }
        }
        return type->type;
    }

    void lock(
        types::SpecType tp,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    ) {
        if (locked) {
            err_handler.error(
                source,
                loc,
                std::format("Internal error: Type variable already locked")
            );
            return;
        }
        if (type.has_value() && !can_convert(type->type, tp.type)) {
            std::string hint = "";
            if (can_cast(type->type, tp.type, pool)) {
                hint = std::format(
                    "use 'as {}' for explicit conversion", pool.to_string(tp)
                );
            }
            err_handler.error(
                source,
                loc,
                std::format(
                    "Type mismatch: cannot convert {} to {}",
                    pool.to_string(tp),
                    pool.to_string(*type)
                ),
                hint
            );
        }
        type = tp;
        locked = true;
        if (!define_loc.has_value()) {
            define_loc = loc;
        }
    }

    void lock(
        types::TypeId tp,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    ) {
        lock(
            {.type = tp, .specifier = types::Specifier::None},
            loc,
            source,
            pool,
            err_handler
        );
    }

    types::TypeId union_tp(
        const TypeVar& other,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    ) {
        if (other.type.has_value()) {
            return add_type(other.type->type, loc, source, pool, err_handler);
        }
        return type.has_value() ? type->type : types::None;
    }

    [[nodiscard]] bool defined() const { return type.has_value(); }

    [[nodiscard]] types::SpecType get() const {
        if (!type.has_value()) {
            return {.type = types::None, .specifier = types::Specifier::None};
        }
        return *type;
    }
};

struct TypeVarMap {
    IndexVector<TypeVar, ir::InstRef> vars;

    TypeVar& operator[](ir::InstRef inst) {
        if (inst.index >= vars.size()) {
            vars.resize(inst.index + 1);
        }
        return vars[inst];
    }
};

class TypeAnalyzer {
public:
    TypeAnalyzer(
        ir::Package& package, ir::FuncRef func_ref, ErrorHandler& err_handler
    )
        : package_(&package),
          type_pool_(&package.types()),
          func_ref_(func_ref),
          func_(&package.func(func_ref)),
          err_handler_(&err_handler) {
        func_type_id_ = package.func_type(func_ref);
        source_ = &func_->source();
        const auto& type = package.types().get(func_type_id_);
        if (!type.data.is<types::Type::Func>()) {
            throw std::runtime_error(
                "Internal error: Function type is not a Func type"
            );
        }
    }

    [[nodiscard]] ir::FuncRef func_ref() const { return func_ref_; }
    [[nodiscard]] bool is_extern() const { return func_->is_extern(); }

    [[nodiscard]] IndexVector<types::SpecType, ir::InstRef> get_types() const {
        IndexVector<types::SpecType, ir::InstRef> result;
        for (auto i : type_vars_.vars.indices()) {
            const auto& var = type_vars_.vars[i];
            if (!var.defined()) {
                err_handler_->error(
                    *source_,
                    func_->inst(i).location,
                    "Type variable not defined (inference failed)"
                );
                result.push_back(
                    {.type = types::None, .specifier = types::Specifier::None}
                );
            } else {
                result.push_back(var.get());
            }
        }
        return result;
    }

    bool propagate() {
        changed_ = false;
        current_inst_ = 0;
        propagate_range(
            ir::Block {.start = ir::InstRef {0}, .end = func_->last_inst()}
        );
        return changed_;
    }

private:
    [[nodiscard]] const types::Type::Func& get_func_type() const {
        return type_pool_->get(func_type_id_).data.get<types::Type::Func>();
    }

    void propagate_range(ir::Block range) {
        if (range.end.index < range.start.index) {
            return;
        }
        while (current_inst_ <= range.end.index) {
            propagate_inst({current_inst_++});
        }
    }

    void propagate_inst(ir::InstRef ref) {
        const auto& inst = func_->inst(ref);
        inst.data.visit(
            [&](const ir::Inst::Const& data) {
                auto type = data.value.visit(
                    [&](bool) { return types::Bool; },
                    [&](std::int64_t) { return types::Int; },
                    [&](double) { return types::Float; },
                    [&](char32_t) { return types::UInt32; },
                    [&](std::string_view v) {
                        return type_pool_->add_array(
                            {
                                .type = types::UInt8,
                                .specifier = types::Specifier::Val,
                            },
                            v.size() + 1
                        );
                    },
                    [&](ir::FuncRef func_ref) {
                        return package_->func_type(func_ref);
                    },
                    [&](ir::UsedFuncRef func_ref) {
                        auto used_func = package_->used_func(func_ref);
                        return used_func.type;
                    },
                    [&](types::TypeId type_id) { return types::Const; }
                );
                lock_type(ref, type, inst.location);
            },
            [&](const ir::Inst::VarDecl& data) {
                if (data.type.has_value()) {
                    lock_type(ref, *data.type, inst.location);
                }
            },
            [&](const ir::Inst::LoadVar& data) {
                if (type_vars_[data.var].defined()) {
                    copy_type(data.var, ref, inst.location);
                }
            },
            [&](const ir::Inst::LoadParam& data) {
                auto param_type = get_func_type().params[data.param.index];
                lock_type(ref, param_type, inst.location);
            },
            [&](const ir::Inst::Store& data) {
                copy_type(data.value, data.var, inst.location);
                add_type(ref, types::None, inst.location);
            },
            [&](const ir::Inst::Binary& data) {
                auto left = type_vars_[data.left].type;
                auto right = type_vars_[data.right].type;
                if (left && right) {
                    auto unified = unify(left->type, right->type);
                    if (unified) {
                        bool ok = false;
                        if (data.op == ir::Inst::BinaryOp::Add ||
                            data.op == ir::Inst::BinaryOp::Sub ||
                            data.op == ir::Inst::BinaryOp::Mul ||
                            data.op == ir::Inst::BinaryOp::Div) {
                            ok = is_int(*unified) || is_uint(*unified) ||
                                 is_float(*unified);
                        } else {
                            ok = is_int(*unified) || is_uint(*unified);
                        }

                        if (!ok) {
                            err_handler_->error(
                                *source_,
                                inst.location,
                                std::format(
                                    "Type mismatch: binary operation "
                                    "not supported for type '{}'",
                                    type_pool_->to_string(*unified)
                                )
                            );
                        }
                        add_type(ref, *unified, inst.location);
                    }
                }
            },
            [&](const ir::Inst::Logical& data) {
                propagate_range(data.right);

                auto left_tp = type_vars_[data.left].type;
                if (left_tp && !can_convert(left_tp->type, types::Bool)) {
                    err_handler_->error(
                        *source_,
                        inst.location,
                        std::format(
                            "Type mismatch: cannot convert '{}' to Bool",
                            type_pool_->to_string(*left_tp)
                        )
                    );
                }
                if (data.right.end.index >= data.right.start.index) {
                    auto right_tp = type_vars_[data.right.end].type;
                    if (right_tp && !can_convert(right_tp->type, types::Bool)) {
                        err_handler_->error(
                            *source_,
                            inst.location,
                            std::format(
                                "Type mismatch: cannot convert '{}' to Bool",
                                type_pool_->to_string(*right_tp)
                            )
                        );
                    }
                }
                add_type(ref, types::Bool, inst.location);
            },
            [&](const ir::Inst::Unary& data) {
                if (data.op == ir::Inst::UnaryOp::Not) {
                    auto val_tp = type_vars_[data.value].type;
                    if (val_tp && !can_convert(val_tp->type, types::Bool)) {
                        err_handler_->error(
                            *source_,
                            inst.location,
                            std::format(
                                "Type mismatch: cannot convert '{}' to Bool",
                                type_pool_->to_string(*val_tp)
                            )
                        );
                    }
                    add_type(ref, types::Bool, inst.location);
                } else {
                    copy_type(data.value, ref, inst.location);
                }
            },
            [&](const ir::Inst::Comparison& data) {
                lock_type(ref, types::Bool, inst.location);
                auto current_left = data.left;
                auto comparators = func_->comparators(data.comparators);

                for (auto& comparator : comparators) {
                    propagate_range(comparator.value);

                    if (comparator.value.end.index >=
                        comparator.value.start.index) {
                        ir::InstRef operand_ref = comparator.value.end;
                        auto left_tv = type_vars_[current_left];
                        auto right_tv = type_vars_[operand_ref];
                        if (left_tv.defined() && right_tv.defined()) {
                            auto unified =
                                unify(left_tv.get().type, right_tv.get().type);
                            if (unified) {
                                comparator.type = *unified;
                            }
                        }

                        current_left = operand_ref;
                    }
                }
            },
            [&](const ir::Inst::Call& data) {
                auto func_tp = type_vars_[data.value].type;
                if (func_tp.has_value()) {
                    const auto& type = type_pool_->get(func_tp->type);
                    if (auto ft = type.data.get_if<types::Type::Func>()) {
                        add_type(ref, ft->return_type.type, inst.location);
                        auto args = func_->inst_refs(data.args);
                        for (size_t i = 0;
                             i < args.size() && i < ft->params.size();
                             ++i) {
                            auto arg_tp = type_vars_[args[i]].type;
                            if (arg_tp && !can_convert(
                                              arg_tp->type, ft->params[i].type
                                          )) {
                                err_handler_->error(
                                    *source_,
                                    inst.location,
                                    std::format(
                                        "Type mismatch: cannot convert "
                                        "argument {} from {} to {}",
                                        i,
                                        type_pool_->to_string(*arg_tp),
                                        type_pool_->to_string(ft->params[i])
                                    )
                                );
                            }
                        }
                    }
                }
            },
            [&](const ir::Inst::Loop& data) {
                propagate_range(data.block);
                lock_type(ref, types::None, inst.location);
            },
            [&](const ir::Inst::If& data) {
                propagate_range(data.then_block);
                if (data.else_block) {
                    propagate_range(*data.else_block);
                }

                auto cond_tp = type_vars_[data.value].type;
                if (cond_tp && !can_convert(cond_tp->type, types::Bool)) {
                    err_handler_->error(
                        *source_,
                        inst.location,
                        std::format(
                            "Type mismatch: cannot convert '{}' to Bool",
                            type_pool_->to_string(*cond_tp)
                        )
                    );
                }
                lock_type(ref, types::None, inst.location);
            },
            [&](const ir::Inst::Break&) {
                lock_type(ref, types::Nothing, inst.location);
            },
            [&](const ir::Inst::Continue&) {
                lock_type(ref, types::Nothing, inst.location);
            },
            [&](const ir::Inst::Return& data) {
                if (data.value.has_value()) {
                    add_type(
                        *data.value,
                        get_func_type().return_type.type,
                        inst.location
                    );
                }
                lock_type(ref, types::Nothing, inst.location);
            },
            [&](const ir::Inst::AddressOf& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    add_type(ref, type_pool_->add_ptr(*tp), inst.location);
                }
            },
            [&](const ir::Inst::Deref& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        add_type(ref, pt->type.type, inst.location);
                    }
                }
            },
            [&](const ir::Inst::GetItem& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto at = type.data.get_if<types::Type::Array>()) {
                        add_type(ref, at->item.type, inst.location);
                    } else if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        add_type(ref, pt->type.type, inst.location);
                    }
                }
            },
            [&](const ir::Inst::SetItem& data) {
                auto var_tp = type_vars_[data.var].type;
                if (var_tp.has_value()) {
                    const auto& type = type_pool_->get(var_tp->type);
                    if (auto at = type.data.get_if<types::Type::Array>()) {
                        add_type(data.value, at->item.type, inst.location);
                    } else if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        add_type(data.value, pt->type.type, inst.location);
                    }
                }
                lock_type(ref, types::None, inst.location);
            },
            [&](const ir::Inst::GetAttr& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto st = type.data.get_if<types::Type::Struct>()) {
                        for (const auto& field : st->fields) {
                            if (field.name == data.name) {
                                lock_type(ref, field.type, inst.location);
                                break;
                            }
                        }
                    }
                }
            },
            [&](const ir::Inst::SetAttr& data) {
                auto var_tp = type_vars_[data.var].type;
                if (var_tp.has_value()) {
                    const auto& type = type_pool_->get(var_tp->type);
                    if (auto st = type.data.get_if<types::Type::Struct>()) {
                        for (const auto& field : st->fields) {
                            if (field.name == data.name) {
                                add_type(
                                    data.value, field.type.type, inst.location
                                );
                                break;
                            }
                        }
                    }
                }
                lock_type(ref, types::None, inst.location);
            },
            [&](const ir::Inst::Array& data) {
                auto items = func_->inst_refs(data.items);
                if (items.empty()) {
                    return;
                }

                std::optional<types::TypeId> common_tp;
                for (auto item_ref : items) {
                    auto item_tp = type_vars_[item_ref].type;
                    if (!item_tp) {
                        common_tp = std::nullopt;
                        break;
                    }
                    if (!common_tp) {
                        common_tp = item_tp->type;
                    } else {
                        common_tp = unify(*common_tp, item_tp->type);
                        if (!common_tp) {
                            break;
                        }
                    }
                }

                if (common_tp.has_value() && !type_vars_[ref].locked) {
                    lock_type(
                        ref,
                        type_pool_->add_array(
                            {.type = *common_tp}, items.size()
                        ),
                        inst.location
                    );
                }
            },
            [&](const ir::Inst::As& data) {
                lock_type(ref, data.type, inst.location);
                auto from_tp = type_vars_[data.value].type;
                if (from_tp.has_value() &&
                    !can_cast(from_tp->type, data.type.type, *type_pool_)) {
                    err_handler_->error(
                        *source_,
                        inst.location,
                        std::format(
                            "Type mismatch: cannot explicitly cast {} to {}",
                            type_pool_->to_string(*from_tp),
                            type_pool_->to_string(data.type)
                        )
                    );
                }
            },
            [&](const auto& i) {
                err_handler_->error(
                    *source_,
                    inst.location,
                    "Internal error: Unknown instruction"
                );
            }
        );
    }

    void add_type(ir::InstRef inst, types::TypeId type, Location loc) {
        auto& tv = type_vars_[inst];
        bool previously_defined = tv.defined();
        auto previous_type = tv.get().type;
        auto tp = tv.add_type(type, loc, *source_, *type_pool_, *err_handler_);
        if (!previously_defined || tp != previous_type) {
            changed_ = true;
        }
    }

    void copy_type(ir::InstRef src, ir::InstRef dest, Location loc) {
        auto& tv = type_vars_[dest];
        bool previously_defined = tv.defined();
        auto previous_type = tv.get().type;
        auto tp = tv.union_tp(
            type_vars_[src], loc, *source_, *type_pool_, *err_handler_
        );
        if (!previously_defined || tp != previous_type) {
            changed_ = true;
        }
    }

    void lock_type(ir::InstRef inst, types::TypeId type, Location loc) {
        auto& tv = type_vars_[inst];
        if (tv.locked) return;
        tv.lock(type, loc, *source_, *type_pool_, *err_handler_);
        changed_ = true;
    }

    void lock_type(ir::InstRef inst, types::SpecType type, Location loc) {
        auto& tv = type_vars_[inst];
        if (tv.locked) return;
        tv.lock(type, loc, *source_, *type_pool_, *err_handler_);
        changed_ = true;
    }

    ir::Package* package_;
    const Source* source_;
    types::TypePool* type_pool_;
    types::TypeId func_type_id_;
    ir::FuncRef func_ref_;
    ir::Func* func_;
    TypeVarMap type_vars_;
    ErrorHandler* err_handler_;
    bool changed_ = false;
    std::uint32_t current_inst_ = 0;
};
}

AnalyzedPackage type_analyze(ir::Package& package, ErrorHandler& err_handler) {
    AnalyzedPackage result(&package);
    std::deque<TypeAnalyzer> analyzers;
    for (auto func_ref : package.funcs().indices()) {
        analyzers.emplace_back(result.ir_package, func_ref, err_handler);
    }
    while (!analyzers.empty()) {
        auto analyzer = std::move(analyzers.front());
        analyzers.pop_front();
        if (analyzer.is_extern()) {
            result.analyzed_funcs.push_back({
                .ref = analyzer.func_ref(),
                .inst_types = {},
            });
            continue;
        }
        if (analyzer.propagate()) {
            analyzers.push_back(std::move(analyzer));
        } else {
            result.analyzed_funcs.push_back({
                .ref = analyzer.func_ref(),
                .inst_types = analyzer.get_types(),
            });
        }
    }
    return result;
}

}
