#include <algorithm>
#include <deque>
#include <format>
#include <optional>
#include <vector>

#include "errors.h"
#include "index.h"
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
    bool locked_spec = false;

    types::TypeId add_type(
        types::SpecType tp,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    ) {
        if (locked) {
            if (type.has_value() && type->type != tp.type) {
                if (!can_convert(tp.type, type->type)) {
                    std::string hint = "";
                    if (can_cast(tp.type, type->type, pool)) {
                        hint = std::format(
                            "use 'as {}' for explicit conversion",
                            pool.to_string(type->type)
                        );
                    }
                    err_handler.error(
                        source,
                        loc,
                        std::format(
                            "Type mismatch: cannot convert {} to {}",
                            pool.to_string(tp.type),
                            pool.to_string(type->type)
                        ),
                        hint
                    );
                }
            }
        } else {
            if (type.has_value()) {
                auto unified = unify(type->type, tp.type);
                if (!unified.has_value()) {
                    err_handler.error(
                        source,
                        loc,
                        std::format(
                            "Type mismatch: cannot unify {} and {}",
                            pool.to_string(tp.type),
                            pool.to_string(type->type)
                        )
                    );
                } else {
                    type->type = *unified;
                }
            } else {
                type = tp;
                define_loc = loc;
            }
        }

        if (!locked_spec && tp.specifier != types::Specifier::None) {
            if (type->specifier == types::Specifier::None) {
                type->specifier = tp.specifier;
            } else if (
                tp.specifier == types::Specifier::Var &&
                type->specifier == types::Specifier::Let
            ) {
                type->specifier = types::Specifier::Var;
            } else if (
                tp.specifier == types::Specifier::Val &&
                type->specifier != types::Specifier::Val
            ) {
                type->specifier = types::Specifier::Val;
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
                    "use 'as {}' for explicit conversion",
                    pool.to_string(tp.type)
                );
            }
            err_handler.error(
                source,
                loc,
                std::format(
                    "Type mismatch: cannot convert {} to {}",
                    pool.to_string(tp.type),
                    pool.to_string(type->type)
                ),
                hint
            );
        }
        type = tp;
        locked = true;
        if (tp.specifier != types::Specifier::None) {
            locked_spec = true;
        }
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
            return add_type(*other.type, loc, source, pool, err_handler);
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

class TypeAnalyzer {
public:
    TypeAnalyzer(
        ir::Package& package,
        IndexSpan<TypeVar, ir::InstRef> type_vars,
        ir::FuncRef func_ref,
        ErrorHandler& err_handler
    )
        : package_(&package),
          type_vars_(type_vars),
          type_pool_(&package.types()),
          func_(&package.func(func_ref)),
          err_handler_(&err_handler) {
        func_type_id_ = package.func_type(func_ref);
    }

    bool propagate() {
        changed_ = false;
        current_inst_ = 0;
        propagate_range(
            ir::Block {.end = {func_->insts.start + func_->insts.size - 1}}
        );
        return changed_;
    }

private:
    void error(Location location, std::string message) const {
        err_handler_->error(*func_->source, location, std::move(message));
    }

    [[nodiscard]] const types::Type::Func& get_func_type() const {
        return type_pool_->get(func_type_id_).data.get<types::Type::Func>();
    }

    void require_var(ir::InstRef ref, Location loc, std::string_view context) {
        auto& tv = type_vars_[ref];
        if (tv.type && tv.type->specifier == types::Specifier::Var) return;

        const auto& inst = package_->inst(ref);
        bool possible = inst.data.visit(
            [&](const ir::Inst::LoadVar& data) {
                require_var(data.var, loc, context);
                return true;
            },
            [&](const ir::Inst::VarDecl&) {
                if (tv.locked_spec &&
                    tv.type->specifier == types::Specifier::Let) {
                    error(
                        loc,
                        std::format(
                            "cannot mutate immutable variable (declared with "
                            "let): {}",
                            context
                        )
                    );
                    return false;
                }
                if (tv.type->specifier == types::Specifier::Let) {
                    tv.type->specifier = types::Specifier::Var;
                    changed_ = true;
                }
                return true;
            },
            [&](const ir::Inst::GetAttr& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto st = type.data.get_if<types::Type::Struct>()) {
                        for (const auto& field : st->fields) {
                            if (field.name == data.name) {
                                if (field.type.specifier ==
                                    types::Specifier::Let) {
                                    error(
                                        loc,
                                        std::format(
                                            "cannot mutate immutable field "
                                            "'{}': {}",
                                            data.name,
                                            context
                                        )
                                    );
                                    return false;
                                }
                                if (field.type.specifier ==
                                    types::Specifier::Val) {
                                    require_var(data.value, loc, context);
                                }
                                return true;
                            }
                        }
                    }
                }
                return false;
            },
            [&](const ir::Inst::GetItem& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto at = type.data.get_if<types::Type::Array>()) {
                        if (at->item.specifier == types::Specifier::Let) {
                            error(
                                loc,
                                std::format(
                                    "cannot mutate immutable array item: {}",
                                    context
                                )
                            );
                            return false;
                        }
                        if (at->item.specifier == types::Specifier::Val) {
                            require_var(data.value, loc, context);
                        }
                        return true;
                    } else if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        if (pt->type.specifier == types::Specifier::Let) {
                            error(
                                loc,
                                std::format(
                                    "cannot mutate immutable pointer target: "
                                    "{}",
                                    context
                                )
                            );
                            return false;
                        }
                        return true;
                    }
                }
                return false;
            },
            [&](const ir::Inst::Deref& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        if (pt->type.specifier == types::Specifier::Let) {
                            error(
                                loc,
                                std::format(
                                    "cannot mutate immutable pointer target: "
                                    "{}",
                                    context
                                )
                            );
                            return false;
                        }
                        return true;
                    }
                }
                return false;
            },
            [&](const ir::Inst::LoadParam& data) {
                auto param_type = get_func_type().params[data.param.index];
                if (param_type.type.specifier == types::Specifier::Let) {
                    error(
                        loc,
                        std::format(
                            "cannot mutate immutable parameter: {}", context
                        )
                    );
                    return false;
                }
                return true;
            },
            [&](const auto&) {
                error(loc, std::format("value cannot be mutated: {}", context));
                return false;
            }
        );

        if (possible && tv.type) {
            if (tv.type->specifier == types::Specifier::Let) {
                tv.type->specifier = types::Specifier::Var;
                changed_ = true;
            }
        }
    }

    void propagate_range(ir::Block range) {
        while (current_inst_ <= range.end.index) {
            propagate_inst({current_inst_++});
        }
    }

    void propagate_inst(ir::InstRef ref) {
        const auto& inst = package_->inst(ref);
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
                lock_type(
                    ref,
                    {.type = type, .specifier = types::Specifier::Val},
                    inst.location
                );
            },
            [&](const ir::Inst::VarDecl& data) {
                if (data.type.has_value()) {
                    lock_type(ref, *data.type, inst.location);
                } else {
                    add_type(
                        ref,
                        {.type = types::Nothing,
                         .specifier = types::Specifier::Let},
                        inst.location
                    );
                }
            },
            [&](const ir::Inst::LoadVar& data) {
                if (type_vars_[data.var].defined()) {
                    copy_type(data.var, ref, inst.location);
                }
            },
            [&](const ir::Inst::LoadParam& data) {
                auto param_type = get_func_type().params[data.param.index];
                lock_type(ref, param_type.type, inst.location);
            },
            [&](const ir::Inst::Store& data) {
                copy_type(data.value, data.var, inst.location);
                lock_type(ref, types::None, inst.location);
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
                            error(
                                inst.location,
                                std::format(
                                    "Type mismatch: binary operation "
                                    "not supported for type '{}'",
                                    type_pool_->to_string(*unified)
                                )
                            );
                        }
                        add_type(
                            ref,
                            {.type = *unified,
                             .specifier = types::Specifier::Val},
                            inst.location
                        );
                    }
                }
            },
            [&](const ir::Inst::Logical& data) {
                propagate_range(data.right);

                auto left_tp = type_vars_[data.left].type;
                if (left_tp && !can_convert(left_tp->type, types::Bool)) {
                    error(
                        inst.location,
                        std::format(
                            "Type mismatch: cannot convert '{}' to Bool",
                            type_pool_->to_string(*left_tp)
                        )
                    );
                }
                auto right_tp = type_vars_[data.right.end].type;
                if (right_tp && !can_convert(right_tp->type, types::Bool)) {
                    error(
                        inst.location,
                        std::format(
                            "Type mismatch: cannot convert '{}' to Bool",
                            type_pool_->to_string(*right_tp)
                        )
                    );
                }
                add_type(
                    ref,
                    {.type = types::Bool, .specifier = types::Specifier::Val},
                    inst.location
                );
            },
            [&](const ir::Inst::Unary& data) {
                if (data.op == ir::Inst::UnaryOp::Not) {
                    auto val_tp = type_vars_[data.value].type;
                    if (val_tp && !can_convert(val_tp->type, types::Bool)) {
                        error(
                            inst.location,
                            std::format(
                                "Type mismatch: cannot convert '{}' to Bool",
                                type_pool_->to_string(*val_tp)
                            )
                        );
                    }
                    add_type(
                        ref,
                        {.type = types::Bool,
                         .specifier = types::Specifier::Val},
                        inst.location
                    );
                } else {
                    copy_type(data.value, ref, inst.location);
                }
            },
            [&](const ir::Inst::Comparison& data) {
                lock_type(
                    ref,
                    {.type = types::Bool, .specifier = types::Specifier::Val},
                    inst.location
                );
                auto current_left = data.left;
                auto comparators = package_->comparators(data.comparators);

                for (auto& comparator : comparators) {
                    propagate_range(comparator.value);
                    ir::InstRef operand_ref = comparator.value.end;
                    auto& left_tv = type_vars_[current_left];
                    auto& right_tv = type_vars_[operand_ref];
                    if (left_tv.defined() && right_tv.defined()) {
                        auto unified =
                            unify(left_tv.get().type, right_tv.get().type);
                        if (unified) {
                            comparator.type = *unified;
                        }
                    }
                    current_left = operand_ref;
                }
            },
            [&](const ir::Inst::Call& data) {
                auto func_tp = type_vars_[data.value].type;
                if (func_tp.has_value()) {
                    const auto& type = type_pool_->get(func_tp->type);
                    if (auto ft = type.data.get_if<types::Type::Func>()) {
                        check_func(ref, inst, *ft);
                    } else {
                        error(inst.location, "is not function");
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
                    error(
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
                    auto& func_type = get_func_type();
                    if (func_type.return_type.specifier ==
                        types::Specifier::Var) {
                        require_var(*data.value, inst.location, "return value");
                    }
                    add_type(*data.value, func_type.return_type, inst.location);
                }
                lock_type(ref, types::Nothing, inst.location);
            },
            [&](const ir::Inst::AddressOf& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    add_type(
                        ref,
                        {.type = type_pool_->add_ptr(*tp),
                         .specifier = types::Specifier::Val},
                        inst.location
                    );
                }
            },
            [&](const ir::Inst::Deref& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        add_type(ref, pt->type, inst.location);
                    }
                }
            },
            [&](const ir::Inst::GetItem& data) {
                auto tp = type_vars_[data.value].type;
                if (tp.has_value()) {
                    const auto& type = type_pool_->get(tp->type);
                    if (auto at = type.data.get_if<types::Type::Array>()) {
                        add_type(ref, at->item, inst.location);
                    } else if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        add_type(ref, pt->type, inst.location);
                    }
                }
            },
            [&](const ir::Inst::SetItem& data) {
                auto var_tp = type_vars_[data.var].type;
                if (var_tp.has_value()) {
                    const auto& type = type_pool_->get(var_tp->type);
                    if (auto at = type.data.get_if<types::Type::Array>()) {
                        add_type(data.value, at->item, inst.location);
                    } else if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                        add_type(data.value, pt->type, inst.location);
                    }
                }
                require_var(data.var, inst.location, "item assignment");
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
                                add_type(data.value, field.type, inst.location);
                                break;
                            }
                        }
                    }
                }
                require_var(data.var, inst.location, "attribute assignment");
                lock_type(ref, types::None, inst.location);
            },
            [&](const ir::Inst::Array& data) {
                auto items = package_->inst_refs(data.items);
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
                        {.type = type_pool_->add_array(
                             {.type = *common_tp,
                              .specifier = types::Specifier::Let},
                             items.size()
                         ),
                         .specifier = types::Specifier::Val},
                        inst.location
                    );
                }
            },
            [&](const ir::Inst::As& data) {
                lock_type(ref, data.type, inst.location);
                auto from_tp = type_vars_[data.value].type;
                if (from_tp.has_value() &&
                    !can_cast(from_tp->type, data.type.type, *type_pool_)) {
                    error(
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
                error(inst.location, "Internal error: Unknown instruction");
            }
        );
    }

    void check_func(
        ir::InstRef ref, const ir::Inst& inst, const types::Type::Func& ft
    ) {
        const auto& data = inst.data.get<ir::Inst::Call>();
        add_type(ref, ft.return_type, inst.location);
        if (data.args.size + data.named_args.size > ft.params.size()) {
            error(
                inst.location,
                std::format(
                    "argument count mismatch: function has {} "
                    "parameters but call arguments {}",
                    ft.params.size(),
                    data.args.size + data.named_args.size
                )
            );
        }
        if (data.args.size < ft.min_pos_args) {
            error(
                inst.location,
                std::format(
                    "функция ожидает не меньше {} позиционных аргументов, но "
                    "передано {}",
                    ft.min_pos_args,
                    data.args.size
                )
            );
        }
        if (data.args.size > ft.max_pos_args) {
            error(
                inst.location,
                std::format(
                    "функция ожидает не больше {} позиционных аргументов, но "
                    "передано {}",
                    ft.min_pos_args,
                    data.args.size
                )
            );
        }
        auto args = package_->inst_refs(data.args);
        for (size_t i = 0; i < args.size(); ++i) {
            auto arg_tp = type_vars_[args[i]].type;
            if (arg_tp && !can_convert(arg_tp->type, ft.params[i].type.type)) {
                error(
                    inst.location,
                    std::format(
                        "Type mismatch: cannot convert "
                        "argument {} from {} to {}",
                        i,
                        type_pool_->to_string(*arg_tp),
                        type_pool_->to_string(ft.params[i].type)
                    )
                );
            }
            if (ft.params[i].type.specifier == types::Specifier::Var) {
                require_var(args[i], inst.location, "function argument");
            }
        }
        auto named_args = package_->call_args(data.named_args);
        for (const auto& named_arg : named_args) {
            auto param = [&] -> std::optional<types::Type::FuncParam> {
                auto search_params = std::span(ft.params).subspan(args.size());
                auto result =
                    std::ranges::find_if(search_params, [&](const auto& param) {
                        return param.name == named_arg.name;
                    });
                if (result != search_params.end()) {
                    return *result;
                }
                search_params = std::span(ft.params).subspan(0, args.size());
                result =
                    std::ranges::find_if(search_params, [&](const auto& param) {
                        return param.name == named_arg.name;
                    });
                if (result != search_params.end()) {
                    error(
                        inst.location,
                        std::format(
                            "argument with name {} already "
                            "passed as {} argument",
                            named_arg.name,
                            result - search_params.begin()
                        )
                    );
                    return std::nullopt;
                }
                error(
                    inst.location,
                    std::format(
                        "argument with name {} not found", named_arg.name
                    )
                );
                return std::nullopt;
            }();
            auto arg_tp = type_vars_[named_arg.value].type;
            if (param && arg_tp &&
                !can_convert(arg_tp->type, param->type.type)) {
                error(
                    inst.location,
                    std::format(
                        "Type mismatch: cannot convert "
                        "argument {} from {} to {}",
                        named_arg.name,
                        type_pool_->to_string(*arg_tp),
                        type_pool_->to_string(param->type)
                    )
                );
            }
            if (param && param->type.specifier == types::Specifier::Var) {
                require_var(
                    named_arg.value, inst.location, "named function argument"
                );
            }
        }
    }

    void add_type(ir::InstRef inst, types::SpecType type, Location loc) {
        auto& tv = type_vars_[inst];
        bool previously_defined = tv.defined();
        auto previous_type = tv.get();
        auto tp =
            tv.add_type(type, loc, *func_->source, *type_pool_, *err_handler_);
        if (!previously_defined || tp != previous_type.type ||
            tv.get().specifier != previous_type.specifier) {
            changed_ = true;
        }
    }

    void copy_type(ir::InstRef src, ir::InstRef dest, Location loc) {
        auto& tv = type_vars_[dest];
        bool previously_defined = tv.defined();
        auto previous_type = tv.get().type;
        auto tp = tv.union_tp(
            type_vars_[src], loc, *func_->source, *type_pool_, *err_handler_
        );
        if (!previously_defined || tp != previous_type) {
            changed_ = true;
        }
    }

    void lock_type(ir::InstRef inst, types::TypeId type, Location loc) {
        auto& tv = type_vars_[inst];
        if (tv.locked) return;
        tv.lock(type, loc, *func_->source, *type_pool_, *err_handler_);
        changed_ = true;
    }

    void lock_type(ir::InstRef inst, types::SpecType type, Location loc) {
        auto& tv = type_vars_[inst];
        if (tv.locked) return;
        tv.lock(type, loc, *func_->source, *type_pool_, *err_handler_);
        changed_ = true;
    }

    ir::Package* package_;
    IndexSpan<TypeVar, ir::InstRef> type_vars_;
    types::TypePool* type_pool_;
    types::TypeId func_type_id_ {};
    ir::Func* func_;
    ErrorHandler* err_handler_;
    bool changed_ = false;
    std::uint32_t current_inst_ = 0;
};

[[nodiscard]] IndexVector<types::SpecType, ir::InstRef> get_types(
    IndexSpan<TypeVar, ir::InstRef> type_vars
) {
    IndexVector<types::SpecType, ir::InstRef> result;
    result.reserve(type_vars.size());
    for (auto i : type_vars.indices()) {
        const auto& var = type_vars[i];
        result.push_back(var.get());
        // if (!var.defined()) {
        //     error(
        //         package.inst(i).location,
        //         "Type variable not defined (inference failed)"
        //     );
        //     result.push_back(
        //         {.type = types::None, .specifier = types::Specifier::None}
        //     );
        // } else {
        //     result.push_back(var.get());
        // }
    }
    return result;
}

}

ir::AnalyzedPackage type_analyze(
    ir::Package& package, ErrorHandler& err_handler
) {
    ir::AnalyzedPackage result(&package);
    IndexVector<TypeVar, ir::InstRef> type_vars(package.last_inst().index + 1);
    std::deque<TypeAnalyzer> analyzers;
    for (auto func_ref : package.funcs().indices()) {
        if (package.func(func_ref).is_extern) {
            continue;
        }
        analyzers.emplace_back(
            package, type_vars.data(), func_ref, err_handler
        );
    }
    while (!analyzers.empty()) {
        auto analyzer = analyzers.front();
        analyzers.pop_front();
        if (analyzer.propagate()) {
            analyzers.push_back(analyzer);
        }
    }
    result.inst_types = get_types(type_vars.data());
    return result;
}

}
