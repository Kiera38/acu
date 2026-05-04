#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "errors.h"
#include "index.h"
#include "ir.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"
#include "semanal/type_utils.h"
#include "semanal/types.h"
#include "source.h"


namespace acu::semanal {
namespace {

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
        current_inst_ = func_->insts.start;
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
                handle_const(ref, data, inst.location);
            },
            [&](const ir::Inst::VarDecl& data) {
                handle_var_decl(ref, data, inst.location);
            },
            [&](const ir::Inst::LoadVar& data) {
                handle_load_var(ref, data, inst.location);
            },
            [&](const ir::Inst::LoadParam& data) {
                handle_load_param(ref, data, inst.location);
            },
            [&](const ir::Inst::Store& data) {
                handle_store(ref, data, inst.location);
            },
            [&](const ir::Inst::Binary& data) {
                handle_binary(ref, data, inst.location);
            },
            [&](const ir::Inst::Logical& data) {
                handle_logical(ref, data, inst.location);
            },
            [&](const ir::Inst::Unary& data) {
                handle_unary(ref, data, inst.location);
            },
            [&](const ir::Inst::Comparison& data) {
                handle_comparison(ref, data, inst.location);
            },
            [&](const ir::Inst::Call& data) { handle_call(ref, inst, data); },
            [&](const ir::Inst::Loop& data) {
                handle_loop(ref, data, inst.location);
            },
            [&](const ir::Inst::If& data) {
                handle_if(ref, data, inst.location);
            },
            [&](const ir::Inst::Break&) {
                lock_type(ref, types::Nothing, inst.location);
            },
            [&](const ir::Inst::Continue&) {
                lock_type(ref, types::Nothing, inst.location);
            },
            [&](const ir::Inst::Return& data) {
                handle_return(ref, data, inst.location);
            },
            [&](const ir::Inst::AddressOf& data) {
                handle_address_of(ref, data, inst.location);
            },
            [&](const ir::Inst::Deref& data) {
                handle_deref(ref, data, inst.location);
            },
            [&](const ir::Inst::GetItem& data) {
                handle_get_item(ref, data, inst.location);
            },
            [&](const ir::Inst::SetItem& data) {
                handle_set_item(ref, data, inst.location);
            },
            [&](const ir::Inst::GetAttr& data) {
                handle_get_attr(ref, data, inst.location);
            },
            [&](const ir::Inst::SetAttr& data) {
                handle_set_attr(ref, data, inst.location);
            },
            [&](const ir::Inst::Array& data) {
                handle_array(ref, data, inst.location);
            },
            [&](const ir::Inst::As& data) {
                handle_as(ref, data, inst.location);
            },
            [&](const auto&) {
                error(inst.location, "Internal error: Unknown instruction");
            }
        );
    }

    void handle_const(
        ir::InstRef ref, const ir::Inst::Const& data, Location loc
    ) {
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
            [&](ir::FuncRef func_ref) { return package_->func_type(func_ref); },
            [&](ir::UsedFuncRef func_ref) {
                auto used_func = package_->used_func(func_ref);
                return used_func.type;
            },
            [&](types::TypeId type_id) { return types::Const; }
        );
        if (data.value.is<std::string_view>()) {
            lock_type(
                ref, {.type = type, .specifier = types::Specifier::Var}, loc
            );
        } else {
            lock_type(
                ref, {.type = type, .specifier = types::Specifier::Val}, loc
            );
        }
    }

    void handle_var_decl(
        ir::InstRef ref, const ir::Inst::VarDecl& data, Location loc
    ) {
        if (data.type.has_value()) {
            lock_type(ref, *data.type, loc);
        } else {
            add_type(
                ref,
                {.type = types::Nothing, .specifier = types::Specifier::Let},
                loc
            );
        }
    }

    void handle_load_var(
        ir::InstRef ref, const ir::Inst::LoadVar& data, Location loc
    ) {
        if (type_vars_[data.var].defined()) {
            copy_type(data.var, ref, loc);
        }
    }

    void handle_load_param(
        ir::InstRef ref, const ir::Inst::LoadParam& data, Location loc
    ) {
        auto param_type = get_func_type().params[data.param.index];
        lock_type(ref, param_type.type, loc);
    }

    void handle_store(
        ir::InstRef ref, const ir::Inst::Store& data, Location loc
    ) {
        copy_type(data.value, data.var, loc);
        lock_type(ref, types::None, loc);
    }

    void handle_binary(
        ir::InstRef ref, const ir::Inst::Binary& data, Location loc
    ) {
        auto left = type_vars_[data.left].type;
        auto right = type_vars_[data.right].type;
        if (left && right) {
            auto unified = unify(left->type, right->type, *type_pool_);
            if (unified) {
                bool ok = false;
                if (data.op == ir::Inst::BinaryOp::Add ||
                    data.op == ir::Inst::BinaryOp::Sub ||
                    data.op == ir::Inst::BinaryOp::Mul ||
                    data.op == ir::Inst::BinaryOp::Div) {
                    ok = type_pool_->is_int(*unified) ||
                         type_pool_->is_float(*unified);
                } else {
                    ok = type_pool_->is_int(*unified);
                }

                if (!ok) {
                    error(
                        loc,
                        std::format(
                            "Type mismatch: binary operation "
                            "not supported for type '{}'",
                            type_pool_->to_string(*unified)
                        )
                    );
                }
                add_type(
                    ref,
                    {.type = *unified, .specifier = types::Specifier::Val},
                    loc
                );
            }
        }
    }

    void handle_logical(
        ir::InstRef ref, const ir::Inst::Logical& data, Location loc
    ) {
        propagate_range(data.right);

        auto left_tp = type_vars_[data.left].type;
        if (left_tp && !can_convert(left_tp->type, types::Bool, *type_pool_)) {
            error(
                loc,
                std::format(
                    "Type mismatch: cannot convert '{}' to Bool",
                    type_pool_->to_string(*left_tp)
                )
            );
        }
        auto right_tp = type_vars_[data.right.end].type;
        if (right_tp &&
            !can_convert(right_tp->type, types::Bool, *type_pool_)) {
            error(
                loc,
                std::format(
                    "Type mismatch: cannot convert '{}' to Bool",
                    type_pool_->to_string(*right_tp)
                )
            );
        }
        add_type(
            ref, {.type = types::Bool, .specifier = types::Specifier::Val}, loc
        );
    }

    void handle_unary(
        ir::InstRef ref, const ir::Inst::Unary& data, Location loc
    ) {
        if (data.op == ir::Inst::UnaryOp::Not) {
            auto val_tp = type_vars_[data.value].type;
            if (val_tp &&
                !can_convert(val_tp->type, types::Bool, *type_pool_)) {
                error(
                    loc,
                    std::format(
                        "Type mismatch: cannot convert '{}' to Bool",
                        type_pool_->to_string(*val_tp)
                    )
                );
            }
            add_type(
                ref,
                {.type = types::Bool, .specifier = types::Specifier::Val},
                loc
            );
        } else {
            copy_type(data.value, ref, loc);
        }
    }

    void handle_comparison(
        ir::InstRef ref, const ir::Inst::Comparison& data, Location loc
    ) {
        lock_type(
            ref, {.type = types::Bool, .specifier = types::Specifier::Val}, loc
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
                    unify(left_tv.get().type, right_tv.get().type, *type_pool_);
                if (unified) {
                    comparator.type = *unified;
                }
            }
            current_left = operand_ref;
        }
    }

    void handle_call(
        ir::InstRef ref, const ir::Inst& inst, const ir::Inst::Call& data
    ) {
        auto func_tp = type_vars_[data.value].type;
        if (func_tp.has_value()) {
            const auto& type = type_pool_->get(func_tp->type);
            if (auto ft = type.data.get_if<types::Type::Func>()) {
                check_func(ref, inst, *ft);
            } else if (type.data.get_if<types::Type::Const>()) {
                if (auto id = package_->inst(data.value)
                                  .data.get<ir::Inst::Const>()
                                  .value.get_if<types::TypeId>()) {
                    lock_type(
                        ref,
                        {.type = *id, .specifier = types::Specifier::Val},
                        inst.location
                    );
                }
            } else {
                error(
                    inst.location,
                    "expression is not a function or a struct and "
                    "cannot be called"
                );
            }
        }
    }

    void handle_loop(
        ir::InstRef ref, const ir::Inst::Loop& data, Location loc
    ) {
        propagate_range(data.block);
        lock_type(ref, types::None, loc);
    }

    void handle_if(ir::InstRef ref, const ir::Inst::If& data, Location loc) {
        propagate_range(data.then_block);
        if (data.else_block) {
            propagate_range(*data.else_block);
        }

        auto cond_tp = type_vars_[data.value].type;
        if (cond_tp && !can_convert(cond_tp->type, types::Bool, *type_pool_)) {
            error(
                loc,
                std::format(
                    "Type mismatch: cannot convert '{}' to Bool",
                    type_pool_->to_string(*cond_tp)
                )
            );
        }
        lock_type(ref, types::None, loc);
    }

    void handle_return(
        ir::InstRef ref, const ir::Inst::Return& data, Location loc
    ) {
        if (data.value.has_value()) {
            auto& func_type = get_func_type();
            if (func_type.return_type.specifier == types::Specifier::Var) {
                require_var(*data.value, loc, "return value");
            }
            add_type(*data.value, func_type.return_type, loc);
        }
        lock_type(ref, types::Nothing, loc);
    }

    void handle_address_of(
        ir::InstRef ref, const ir::Inst::AddressOf& data, Location loc
    ) {
        auto tp = type_vars_[data.value].type;
        if (tp.has_value()) {
            add_type(
                ref,
                {.type = type_pool_->add_ptr(*tp),
                 .specifier = types::Specifier::Val},
                loc
            );
        }
    }

    void handle_deref(
        ir::InstRef ref, const ir::Inst::Deref& data, Location loc
    ) {
        auto tp = type_vars_[data.value].type;
        if (tp.has_value()) {
            const auto& type = type_pool_->get(tp->type);
            if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                add_type(ref, pt->type, loc);
            }
        }
    }

    void handle_get_item(
        ir::InstRef ref, const ir::Inst::GetItem& data, Location loc
    ) {
        auto tp = type_vars_[data.value].type;
        if (tp.has_value()) {
            const auto& type = type_pool_->get(tp->type);
            if (auto at = type.data.get_if<types::Type::Array>()) {
                add_type(ref, at->item, loc);
            } else if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                add_type(ref, pt->type, loc);
            }
        }
    }

    void handle_set_item(
        ir::InstRef ref, const ir::Inst::SetItem& data, Location loc
    ) {
        auto var_tp = type_vars_[data.var].type;
        if (var_tp.has_value()) {
            const auto& type = type_pool_->get(var_tp->type);
            if (auto at = type.data.get_if<types::Type::Array>()) {
                add_type(data.value, at->item, loc);
            } else if (auto pt = type.data.get_if<types::Type::Ptr>()) {
                add_type(data.value, pt->type, loc);
            }
        }
        require_var(data.var, loc, "item assignment");
        lock_type(ref, types::None, loc);
    }

    void handle_get_attr(
        ir::InstRef ref, const ir::Inst::GetAttr& data, Location loc
    ) {
        auto tp = type_vars_[data.value].type;
        if (tp.has_value()) {
            const auto& type = type_pool_->get(tp->type);
            if (auto st = type.data.get_if<types::Type::Struct>()) {
                for (const auto& field : st->fields) {
                    if (field.name == data.name) {
                        lock_type(ref, field.type, loc);
                        break;
                    }
                }
            }
        }
    }

    void handle_set_attr(
        ir::InstRef ref, const ir::Inst::SetAttr& data, Location loc
    ) {
        auto var_tp = type_vars_[data.var].type;
        if (var_tp.has_value()) {
            const auto& type = type_pool_->get(var_tp->type);
            if (auto st = type.data.get_if<types::Type::Struct>()) {
                for (const auto& field : st->fields) {
                    if (field.name == data.name) {
                        add_type(data.value, field.type, loc);
                        break;
                    }
                }
            }
        }
        require_var(data.var, loc, "attribute assignment");
        lock_type(ref, types::None, loc);
    }

    void handle_array(
        ir::InstRef ref, const ir::Inst::Array& data, Location loc
    ) {
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
                common_tp = unify(*common_tp, item_tp->type, *type_pool_);
                if (!common_tp) {
                    break;
                }
            }
        }

        if (common_tp.has_value() && !type_vars_[ref].locked) {
            lock_type(
                ref,
                {.type = type_pool_->add_array(
                     {.type = *common_tp, .specifier = types::Specifier::Let},
                     items.size()
                 ),
                 .specifier = types::Specifier::Val},
                loc
            );
        }
    }

    void handle_as(ir::InstRef ref, const ir::Inst::As& data, Location loc) {
        lock_type(ref, data.type, loc);
        auto from_tp = type_vars_[data.value].type;
        if (from_tp.has_value() &&
            !can_cast(from_tp->type, data.type.type, *type_pool_)) {
            error(
                loc,
                std::format(
                    "Type mismatch: cannot explicitly cast {} to {}",
                    type_pool_->to_string(*from_tp),
                    type_pool_->to_string(data.type)
                )
            );
        }
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
                    "too many arguments: function has {} "
                    "parameters but {} were provided",
                    ft.params.size(),
                    data.args.size + data.named_args.size
                )
            );
        }
        if (data.args.size < ft.min_pos_args) {
            error(
                inst.location,
                std::format(
                    "too few arguments: expected at least {}, but got {}",
                    ft.min_pos_args,
                    data.args.size
                )
            );
        }
        if (data.args.size > ft.max_pos_args) {
            error(
                inst.location,
                std::format(
                    "too many arguments: expected at most {}, but got {}",
                    ft.max_pos_args,
                    data.args.size
                )
            );
        }
        auto args = package_->inst_refs(data.args);
        for (size_t i = 0; i < args.size(); ++i) {
            auto arg_tp = type_vars_[args[i]].type;
            if (arg_tp && !can_convert(
                              arg_tp->type, ft.params[i].type.type, *type_pool_
                          )) {
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
                            "named argument '{}' was already provided as "
                            "positional argument {}",
                            named_arg.name,
                            result - search_params.begin()
                        )
                    );
                    return std::nullopt;
                }
                error(
                    inst.location,
                    std::format(
                        "function has no parameter named '{}'", named_arg.name
                    )
                );
                return std::nullopt;
            }();
            auto arg_tp = type_vars_[named_arg.value].type;
            if (param && arg_tp &&
                !can_convert(arg_tp->type, param->type.type, *type_pool_)) {
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
    const ir::Func* current_func_ = nullptr;

public:
    void process(const ir::Func& func) {
        current_func_ = &func;
        for (auto ref : func.insts) {
            propagate_inst(ref);
        }
        current_func_ = nullptr;
    }
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

    for (auto func_ref : package.funcs().indices()) {
        if (package.func(func_ref).is_extern) {
            continue;
        }

        TypeAnalyzer analyzer(package, type_vars.data(), func_ref, err_handler);
        while (analyzer.propagate()) {
            // Internal fixed-point iteration for the function
        }
    }

    result.inst_types = get_types(type_vars.data());
    return result;
}

}
