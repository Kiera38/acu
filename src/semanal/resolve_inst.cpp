#include "resolver.h"

namespace acu::semanal {
namespace {
std::uint8_t as_uint8(
    std::string_view str,
    const Source& source,
    Location loc,
    ErrorHandler& err_handler
) {
    std::uint8_t result = 0;
    auto [ptr, err] =
        std::from_chars(str.data(), str.data() + str.length(), result);
    if (err != std::errc {}) {
        err_handler.error(source, loc, "error in number");
    }
    return result;
}
}
types::SpecType Resolver::resolve_type(const nodes::Expr& expr) {
    auto res = expr.value.visit(
        [&](const nodes::Expr::Name& name) -> types::SpecType {
            if (auto type = context_->find(name.name)) {
                return {.type = type->data.get<types::TypeId>()};
            } else {
                if (name.name.starts_with("Int")) {
                    if (name.name == "Int") return {.type = types::Int};
                    auto bits = as_uint8(
                        name.name.substr(3),
                        context_->source(),
                        expr.location,
                        *err_handler_
                    );
                    switch (bits) {
                        case 8: return {.type = types::Int8};
                        case 16: return {.type = types::Int16};
                        case 32: return {.type = types::Int32};
                        case 64: return {.type = types::Int64};
                        default:
                            err_handler_->error(
                                context_->source(),
                                expr.location,
                                "unsupported integer size"
                            );
                            return {.type = types::None};
                    }
                } else if (name.name.starts_with("UInt")) {
                    if (name.name == "UInt") return {.type = types::UInt};
                    auto bits = as_uint8(
                        name.name.substr(4),
                        context_->source(),
                        expr.location,
                        *err_handler_
                    );
                    switch (bits) {
                        case 8: return {.type = types::UInt8};
                        case 16: return {.type = types::UInt16};
                        case 32: return {.type = types::UInt32};
                        case 64: return {.type = types::UInt64};
                        default:
                            err_handler_->error(
                                context_->source(),
                                expr.location,
                                "unsupported integer size"
                            );
                            return {.type = types::None};
                    }
                } else if (name.name.starts_with("Float")) {
                    if (name.name == "Float") return {.type = types::Float};
                    auto bits = as_uint8(
                        name.name.substr(5),
                        context_->source(),
                        expr.location,
                        *err_handler_
                    );
                    switch (bits) {
                        case 32: return {.type = types::Float32};
                        case 64: return {.type = types::Float64};
                        default:
                            err_handler_->error(
                                context_->source(),
                                expr.location,
                                "unsupported float size"
                            );
                            return {.type = types::None};
                    }
                } else if (name.name == "Bool") {
                    return {.type = types::Bool};
                } else if (name.name == "None") {
                    return {.type = types::None};
                } else if (name.name == "Nothing") {
                    return {.type = types::Nothing};
                }
                err_handler_->error(
                    context_->source(),
                    expr.location,
                    std::format("name '{}' not found", name.name),
                    context_->suggest_similar_name(name.name)
                );
                return {.type = types::None};
            }
        },
        [&](const nodes::Expr::GetItem& node) -> types::SpecType {
            const auto& name = node.value->value.get<nodes::Expr::Name>();
            if (name.name == "Array") {
                if (node.args.size() != 2) {
                    err_handler_->error(
                        context_->source(),
                        expr.location,
                        "Array type has 2 parameters"
                    );
                    return {.type = types::None};
                }
                auto type = resolve_type(*node.args[0]);
                if (type.specifier == types::Specifier::None) {
                    type.specifier = types::Specifier::Val;
                }
                auto length = get_int_const(*node.args[1]);
                return {.type = ir_package_.types().add_array(type, length)};
            } else if (name.name == "Ptr") {
                if (node.args.size() != 1) {
                    err_handler_->error(
                        context_->source(),
                        expr.location,
                        "Ptr type has 1 parameter"
                    );
                    return {.type = types::None};
                }
                auto type = resolve_type(*node.args[0]);
                if (type.specifier == types::Specifier::None) {
                    type.specifier = types::Specifier::Val;
                }
                return {.type = ir_package_.types().add_ptr(type)};
            } else {
                err_handler_->error(
                    context_->source(), expr.location, "unknown type"
                );
                return {.type = types::None};
            }
        },
        [&](const nodes::Expr::Spec& spec) -> types::SpecType {
            auto specifier = [&] {
                switch (spec.specifier) {
                    using enum nodes::Expr::Specifier;
                    case Let: return types::Specifier::Let;
                    case Var: return types::Specifier::Var;
                    case Val: return types::Specifier::Val;
                }
                std::unreachable();
            }();
            return {
                .type = resolve_type(*spec.type).type, .specifier = specifier
            };
        },
        [&](const nodes::Expr::GetAttr& node) -> types::SpecType {
            PackageName path;
            if (flatten_module_path(expr, path)) {
                if (auto item_entry = find_in_imported_module_chain(path)) {
                    if (auto* type_id_ptr =
                            item_entry->data.get_if<types::TypeId>()) {
                        return {.type = *type_id_ptr};
                    }
                }
            }
            err_handler_->error(
                context_->source(), expr.location, "expr is not a type"
            );
            return {.type = types::None};
        },
        [&](const auto&) -> types::SpecType {
            err_handler_->error(
                context_->source(), expr.location, "expr is not a type"
            );
            return {.type = types::None};
        }
    );
    return res;
}

std::int64_t Resolver::get_int_const(const nodes::Expr& expr) {
    if (auto* lit = expr.value.get_if<nodes::Expr::Literal>()) {
        if (auto* val = lit->value.get_if<std::int64_t>()) {
            return *val;
        }
    }
    err_handler_->error(
        context_->source(), expr.location, "Expected integer constant"
    );
    return 0;
}

void Resolver::resolve_stmt(const nodes::Stmt& stmt, ir::Func& func) {
    stmt.value.visit(
        [&](const nodes::Stmt::Expr& data) { resolve_expr(*data.expr, func); },
        [&](const nodes::Stmt::Var& data) {
            std::optional<types::SpecType> type;
            if (data.type) {
                type = resolve_type(*data.type);
            }
            auto var_ref = ir_package_.add({
                .data = ir::Inst::VarDecl {.name = data.name, .type = type},
                .location = stmt.location,
            });

            if (auto existing = context_->add(data.name, {var_ref, stmt.location})) {
                err_handler_->error(
                    context_->source(),
                    stmt.location,
                    std::format("redefinition of '{}'", data.name),
                    "",
                    {{&context_->source(), existing->location, "previous definition is here"}}
                );
            }
            if (data.init) {
                auto value_ref = resolve_expr(*data.init, func);
                ir_package_.add({
                    .data =
                        ir::Inst::Store {.var = var_ref, .value = value_ref},
                    .location = stmt.location,
                });
            }
        },
        [&](const nodes::Stmt::Block& data) {
            context_->push();
            for (const auto& stmt : data.stmts) {
                resolve_stmt(*stmt, func);
            }
            context_->pop();
        },
        [&](const nodes::Stmt::If& data) {
            auto cond_ref = resolve_expr(*data.cond, func);
            auto if_ref = ir_package_.add({
                .data = ir::Inst::If {.value = cond_ref},
                .location = stmt.location,
            });

            ir::Block then_block = resolve_block(*data.then_block, func);
            std::optional<ir::Block> else_block {};
            if (data.else_block) {
                else_block = resolve_block(*data.else_block, func);
            }
            ir_package_.set_if_blocks(if_ref, then_block, else_block);
        },
        [&](const nodes::Stmt::While& data) {
            auto loop_ref = ir_package_.add({
                .data = ir::Inst::Loop {},
                .location = stmt.location,
            });
            auto loop_block = resolve_block(func, [&] {
                auto cond_ref = resolve_expr(*data.cond, func);
                auto if_ref = ir_package_.add({
                    .data = ir::Inst::If {.value = cond_ref},
                    .location = stmt.location,
                });
                auto then_block = resolve_block(*data.body, func);
                auto break_ref = ir_package_.add({
                    .data = ir::Inst::Break {},
                    .location = stmt.location,
                });
                ir_package_.set_if_blocks(
                    if_ref,
                    then_block,
                    ir::Block {.end = break_ref}
                );
            });
            ir_package_.set_loop_block(loop_ref, loop_block);
        },
        [&](const nodes::Stmt::Return& data) {
            ir::Inst return_inst;
            if (data.value) {
                auto value_ref = resolve_expr(*data.value, func);
                return_inst.data = ir::Inst::Return {.value = value_ref};
            } else {
                return_inst.data = ir::Inst::Return {.value = std::nullopt};
            }
            return_inst.location = stmt.location;

            ir_package_.add(return_inst);
        },
        [&](const nodes::Stmt::Break& data) {
            ir_package_.add({
                .data = ir::Inst::Break {},
                .location = stmt.location,
            });
        },
        [&](const nodes::Stmt::Continue& data) {
            ir_package_.add({
                .data = ir::Inst::Continue {},
                .location = stmt.location,
            });
        },
        [&](const nodes::Stmt::Assign& data) {
            if (!data.targets.empty()) {
                auto value_ref = resolve_expr(*data.value, func);
                for (const auto& target : data.targets) {
                    convert_store(value_ref, func, *target);
                }
            }
        },
        [&](const nodes::Stmt::OpAssign& data) {
            auto target_ref = resolve_expr(*data.target, func);
            auto value_ref = resolve_expr(*data.value, func);
            auto ir_op = [&] {
                switch (data.op) {
                    using enum nodes::Stmt::AssignOp;
                    case Add: return ir::Inst::BinaryOp::Add;
                    case Sub: return ir::Inst::BinaryOp::Sub;
                    case Mul: return ir::Inst::BinaryOp::Mul;
                    case Div: return ir::Inst::BinaryOp::Div;
                    case Mod: return ir::Inst::BinaryOp::Mod;
                    case LShift: return ir::Inst::BinaryOp::LShift;
                    case RShift: return ir::Inst::BinaryOp::RShift;
                    case BitAnd: return ir::Inst::BinaryOp::BitAnd;
                    case BitOr: return ir::Inst::BinaryOp::BitOr;
                    case BitXor: return ir::Inst::BinaryOp::BitXor;
                }
                std::unreachable();
            }();
            convert_store(
                ir_package_.add({
                    .data =
                        ir::Inst::Binary {
                            .left = target_ref, .right = value_ref, .op = ir_op
                        },
                    .location = stmt.location,
                }),
                func,
                *data.target
            );
        },
        [&](const auto&) {}
    );
}

ir::Block Resolver::resolve_block(
    ir::Func& func, std::invocable auto&& resolve
) {
    std::invoke(resolve);
    return {.end = ir_package_.last_inst()};
}

ir::Block Resolver::resolve_block(const nodes::Stmt& stmt, ir::Func& func) {
    return resolve_block(func, [&] { resolve_stmt(stmt, func); });
}

ir::Block Resolver::resolve_block(const nodes::Expr& expr, ir::Func& func) {
    return resolve_block(func, [&] { resolve_expr(expr, func); });
}

ir::InstRef Resolver::convert_store(
    ir::InstRef value, ir::Func& func, const nodes::Expr& expr
) {
    return expr.value.visit(
        [&](const nodes::Expr::Name& node) {
            if (auto var = context_->find(node.name)) {
                auto ref = var->data.get<ir::InstRef>();
                return ir_package_.add({
                    .data = ir::Inst::Store {.var = ref, .value = value},
                    .location = expr.location,
                });
            } else {
                auto var_ref = ir_package_.add({
                    .data = ir::Inst::VarDecl {.name = node.name},
                    .location = expr.location,
                });
                context_->add(node.name, {var_ref, expr.location});
                return ir_package_.add({
                    .data = ir::Inst::Store {.var = var_ref, .value = value},
                    .location = expr.location,
                });
            }
        },
        [&](const nodes::Expr::GetItem& node) {
            return ir_package_.add({
                .data =
                    ir::Inst::SetItem {
                        .var = resolve_expr(*node.value, func),
                        .index = resolve_expr(*node.args[0], func),
                        .value = value
                    },
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::GetAttr& node) {
            return ir_package_.add({
                .data =
                    ir::Inst::SetAttr {
                        .var = resolve_expr(*node.value, func),
                        .value = value,
                        .name = node.name,
                    },
                .location = expr.location,
            });
        },
        [&](const auto&) -> ir::InstRef {
            err_handler_->error(
                context_->source(),
                expr.location,
                "cannot use this expression on the left "
                "side of assignment"
            );
            return value;
        }
    );
}

ir::Inst::Const Resolver::convert_const(const nodes::Expr::Literal& lit) {
    return lit.value.visit([&](auto v) -> ir::Inst::Const { return {v}; });
}

ir::InstRef Resolver::resolve_expr(const nodes::Expr& expr, ir::Func& func) {
    return expr.value.visit(
        [&](const nodes::Expr::Literal& node) {
            return ir_package_.add({
                .data = convert_const(node),
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::Name& node) {
            auto entry = context_->find(node.name);
            if (entry != nullptr) {
                return ir_package_.add(entry->data.visit(
                    [&](ir::InstRef var_ref) -> ir::Inst {
                        return {
                            .data = ir::Inst::LoadVar {.var = var_ref},
                            .location = expr.location
                        };
                    },
                    [&](ir::ParamRef param_ref) -> ir::Inst {
                        return {
                            .data = ir::Inst::LoadParam {.param = param_ref},
                            .location = expr.location
                        };
                    },
                    [&](ir::FuncRef func_ref) -> ir::Inst {
                        return {
                            .data = ir::Inst::Const {.value = func_ref},
                            .location = expr.location
                        };
                    },
                    [&](ir::UsedFuncRef func_ref) -> ir::Inst {
                        return {
                            .data = ir::Inst::Const {.value = func_ref},
                            .location = expr.location
                        };
                    },
                    [&](types::TypeId struct_ref) -> ir::Inst {
                        return {
                            .data = ir::Inst::Const {.value = struct_ref},
                            .location = expr.location
                        };
                    },
                    [&](const auto&) -> ir::Inst {
                        return {
                            .data = ir::Inst::Const {false},
                            .location = expr.location
                        };
                    }
                ));
            } else {
                err_handler_->error(
                    context_->source(),
                    expr.location,
                    std::format("name '{}' not found", node.name),
                    context_->suggest_similar_name(node.name)
                );
                return ir_package_.add({
                    .data = ir::Inst::Const {false},
                    .location = expr.location,
                });
            }
        },
        [&](const nodes::Expr::Binary& node) {
            auto left_ref = resolve_expr(*node.left, func);
            if (node.op == nodes::Expr::BinaryOp::LogicalAnd ||
                node.op == nodes::Expr::BinaryOp::LogicalOr) {
                auto logical_ref = ir_package_.add({
                    .data =
                        ir::Inst::Logical {
                            .left = left_ref,
                            .op = node.op == nodes::Expr::BinaryOp::LogicalOr
                                      ? ir::Inst::LogicalOp::Or
                                      : ir::Inst::LogicalOp::And
                        },
                    .location = expr.location,
                });

                auto right_block = resolve_block(*node.right, func);
                ir_package_.set_logical_block(logical_ref, right_block);
                return logical_ref;
            } else {
                auto right_ref = resolve_expr(*node.right, func);
                ir::Inst::BinaryOp ir_op = [&] {
                    switch (node.op) {
                        using enum nodes::Expr::BinaryOp;
                        case Add: return ir::Inst::BinaryOp::Add;
                        case Sub: return ir::Inst::BinaryOp::Sub;
                        case Mul: return ir::Inst::BinaryOp::Mul;
                        case Div: return ir::Inst::BinaryOp::Div;
                        case Mod: return ir::Inst::BinaryOp::Mod;
                        case LShift: return ir::Inst::BinaryOp::LShift;
                        case RShift: return ir::Inst::BinaryOp::RShift;
                        case BitAnd: return ir::Inst::BinaryOp::BitAnd;
                        case BitOr: return ir::Inst::BinaryOp::BitOr;
                        case BitXor: return ir::Inst::BinaryOp::BitXor;
                        default: std::unreachable();
                    }
                }();
                return ir_package_.add({
                    .data =
                        ir::Inst::Binary {
                            .left = left_ref, .right = right_ref, .op = ir_op
                        },
                    .location = expr.location,
                });
            }
        },
        [&](const nodes::Expr::Unary& node) {
            auto operand_ref = resolve_expr(*node.operand, func);
            if (node.op == nodes::Expr::UnaryOp::Deref ||
                node.op == nodes::Expr::UnaryOp::AddressOf) {
                switch (node.op) {
                    case nodes::Expr::UnaryOp::Deref:
                        return ir_package_.add({
                            .data = ir::Inst::Deref {operand_ref},
                            .location = expr.location,
                        });
                    case nodes::Expr::UnaryOp::AddressOf:
                        return ir_package_.add({
                            .data = ir::Inst::AddressOf {operand_ref},
                            .location = expr.location,
                        });
                    default: return ir_package_.add(ir::Inst {});
                }
            }

            ir::Inst::UnaryOp ir_op = [&] {
                switch (node.op) {
                    using enum nodes::Expr::UnaryOp;
                    case Not: return ir::Inst::UnaryOp::Not;
                    case Neg: return ir::Inst::UnaryOp::Neg;
                    case BitNot: return ir::Inst::UnaryOp::BitNot;
                    default: std::unreachable();
                }
            }();

            return ir_package_.add({
                .data = ir::Inst::Unary {.value = operand_ref, .op = ir_op},
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::Call& node) {
            auto callee_ref = resolve_expr(*node.value, func);

            std::vector<ir::InstRef> arg_refs;
            std::vector<ir::CallArg> named_args;
            arg_refs.reserve(node.args.size());
            for (const auto& arg : node.args) {
                if (arg.name.has_value()) {
                    named_args.push_back(
                        {.name = *arg.name,
                         .value = resolve_expr(*arg.value, func)}
                    );
                } else {
                    if (!named_args.empty()) {
                        err_handler_->error(
                            context_->source(),
                            arg.value->location,
                            "psitional args must be before named args"
                        );
                    }
                    arg_refs.push_back(resolve_expr(*arg.value, func));
                }
            }
            auto inst_refs = ir_package_.add(arg_refs);
            auto call_args = ir_package_.add_call_args(named_args);
            return ir_package_.add({
                .data =
                    ir::Inst::Call {
                        .value = callee_ref,
                        .args = inst_refs,
                        .named_args = call_args
                    },
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::GetItem& node) {
            auto container_ref = resolve_expr(*node.value, func);
            auto index_ref = resolve_expr(*node.args[0], func);
            return ir_package_.add({
                .data =
                    ir::Inst::GetItem {
                        .value = container_ref, .index = index_ref
                    },
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::GetAttr& node) {
            PackageName path;
            if (flatten_module_path(expr, path)) {
                if (auto item_entry = find_in_imported_module_chain(path)) {
                    return ir_package_.add(item_entry->data.visit(
                        [&](ir::FuncRef func_ref) -> ir::Inst {
                            return {
                                .data = ir::Inst::Const {.value = func_ref},
                                .location = expr.location
                            };
                        },
                        [&](ir::UsedFuncRef func_ref) -> ir::Inst {
                            return {
                                .data = ir::Inst::Const {.value = func_ref},
                                .location = expr.location
                            };
                        },
                        [&](types::TypeId struct_ref) -> ir::Inst {
                            return {
                                .data = ir::Inst::Const {.value = struct_ref},
                                .location = expr.location
                            };
                        },
                        [&](const auto&) -> ir::Inst {
                            throw std::runtime_error("");
                            return {
                                .data = ir::Inst::Const {false},
                                .location = expr.location
                            };
                        }
                    ));
                }
            }
            auto obj_ref = resolve_expr(*node.value, func);
            return ir_package_.add({
                .data = ir::Inst::GetAttr {.value = obj_ref, .name = node.name},
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::Array& node) {
            std::vector<ir::InstRef> item_refs;
            item_refs.reserve(node.items.size());
            for (const auto& item : node.items) {
                item_refs.push_back(resolve_expr(*item, func));
            }

            auto inst_refs = ir_package_.add(item_refs);
            return ir_package_.add({
                .data = ir::Inst::Array {inst_refs},
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::As& node) {
            auto value_ref = resolve_expr(*node.value, func);
            auto type = resolve_type(*node.type);
            return ir_package_.add({
                .data = ir::Inst::As {.value = value_ref, .type = type},
                .location = expr.location,
            });
        },
        [&](const nodes::Expr::Comparison& node) {
            if (node.operands.size() < 2 || node.operators.size() < 1 ||
                node.operands.size() != node.operators.size() + 1) {
                err_handler_->error(
                    context_->source(),
                    expr.location,
                    "Invalid comparison expression"
                );
                return ir_package_.add(ir::Inst {});
            }

            auto left_ref = resolve_expr(*node.operands[0], func);
            auto comparison_ref = ir_package_.add({
                .data = ir::Inst::Comparison {.left = left_ref},
                .location = expr.location,
            });

            std::vector<ir::Comparator> comparators;
            comparators.reserve(node.operators.size());
            for (size_t i = 0; i < node.operators.size(); ++i) {
                auto right_block = resolve_block(*node.operands[i + 1], func);
                ir::ComparisonOp ir_op = [&] {
                    switch (node.operators[i]) {
                        using enum nodes::Expr::ComparisonOp;
                        case Less: return ir::ComparisonOp::Less;
                        case Greater: return ir::ComparisonOp::Greater;
                        case LessEqual: return ir::ComparisonOp::LessEqual;
                        case GreaterEqual:
                            return ir::ComparisonOp::GreaterEqual;
                        case Equal: return ir::ComparisonOp::Equal;
                        case NotEqual: return ir::ComparisonOp::NotEqual;
                    }
                    std::unreachable();
                }();
                comparators.push_back({
                    .value = right_block,
                    .op = ir_op,
                });
            }
            ir_package_.set_comparators(comparison_ref, comparators);
            return comparison_ref;
        },
        [&](const auto&) { return ir_package_.add(ir::Inst {}); }
    );
}

}