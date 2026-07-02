#include <format>
#include <utility>

#include "nodes.h"
#include "tokens.h"

namespace acu::nodes {
namespace {
class Formatter {
public:
    std::string to_string(const Expr& expr) {
        format(expr);
        return std::move(result);
    }
    std::string to_string(const Stmt& stmt) {
        format(stmt);
        return std::move(result);
    }
    std::string to_string(const Item& item) {
        format(item);
        return std::move(result);
    }
    std::string to_string(const Module& module) {
        format(module);
        return std::move(result);
    }

private:
    void format(const Expr& expr) {
        expr.visit(
            [&](const Expr::Literal& literal) {
                print("Literal: {}\n", parser::to_string(literal.value));
            },
            [&](const Expr::Name& name) { print("Name: {}\n", name.name); },
            [&](const Expr::Binary& binary) {
                print("Binary: {}\n", to_string(binary.op));
                indent([&] {
                    format(*binary.left);
                    format(*binary.right);
                });
            },
            [&](const Expr::Comparison& comparison) {
                print("Comparison: {}\n", to_string(comparison.operators));
                indent([&] {
                    for (const auto& operand : comparison.operands) {
                        format(*operand);
                    }
                });
            },
            [&](const Expr::Unary& unary) {
                print("Unary: {}\n", to_string(unary.op));
                indent([&] { format(*unary.operand); });
            },
            [&](const Expr::Call& call) {
                print("Call:\n");
                indent([&] {
                    print("Value:\n");
                    indent([&] { format(*call.value); });
                    print("Args:\n");
                    indent([&] {
                        for (const auto& arg : call.args) {
                            if (arg.name) {
                                print("{}: ", *arg.name);
                                disable_indent([&] { format(*call.value); });
                            } else {
                                format(*call.value);
                            }
                        }
                    });
                });
            },
            [&](const Expr::GetItem& get_item) {
                print("GetItem:\n");
                indent([&] {
                    print("Value:\n");
                    indent([&] { format(*get_item.value); });
                    print("Args:\n");
                    indent([&] {
                        for (const auto& arg : get_item.args) {
                            format(*arg);
                        }
                    });
                });
            },
            [&](const Expr::GetAttr& get_attr) {
                print("GetAttr: {}\n", get_attr.name);
                indent([&] { format(*get_attr.value); });
            },
            [&](const Expr::Array& array) {
                print("Array:\n");
                indent([&] {
                    for (const auto& item : array.items) {
                        format(*item);
                    }
                });
            },
            [&](const Expr::As& as) {
                print("As:\n");
                indent([&] {
                    format(*as.value);
                    format(*as.type);
                });
            },
            [&](const Expr::Spec& spec) {
                print("Spec: {}\n", to_string(spec.specifier));
                indent([&] { format(*spec.type); });
            }
        );
    }
    void format(const Stmt& stmt) {
        stmt.visit(
            [&](const Stmt::Expr& expr) {
                print("Expr:\n");
                indent([&] { format(*expr.expr); });
            },
            [&](const Stmt::Var& var) {
                print("Var: {}\n", var.name);
                indent([&] {
                    if (var.type) {
                        print("Type:\n");
                        indent([&] { format(*var.type); });
                    }
                    if (var.init) {
                        print("Init:\n");
                        indent([&] { format(*var.init); });
                    }
                });
            },
            [&](const Stmt::Block& block) {
                print("Block:\n");
                indent([&] {
                    for (const auto& stmt : block.stmts) {
                        format(*stmt);
                    }
                });
            },
            [&](const Stmt::If& if_stmt) {
                print("If:\n");
                indent([&] {
                    print("Condition:\n");
                    indent([&] { format(*if_stmt.cond); });
                    print("Then:\n");
                    indent([&] { format(*if_stmt.then_block); });
                    if (if_stmt.else_block) {
                        print("Else:\n");
                        indent([&] { format(*if_stmt.else_block); });
                    }
                });
            },
            [&](const Stmt::While& while_stmt) {
                print("While:\n");
                indent([&] {
                    print("Condition:\n");
                    indent([&] { format(*while_stmt.cond); });
                    print("Body:\n");
                    indent([&] { format(*while_stmt.body); });
                });
            },
            [&](const Stmt::Return& return_stmt) {
                if (return_stmt.value) {
                    print("Return:\n");
                    indent([&] { format(*return_stmt.value); });
                } else {
                    print("Return\n");
                }
            },
            [&](const Stmt::Break& break_stmt) { print("Break\n"); },
            [&](const Stmt::Continue& continue_stmt) { print("Continue\n"); },
            [&](const Stmt::Assign& assign_stmt) {
                print("Assign:\n");
                indent([&] {
                    print("Targets:\n");
                    indent([&] {
                        for (const auto& target : assign_stmt.targets) {
                            format(*target);
                        }
                    });
                    print("Value:\n");
                    indent([&] { format(*assign_stmt.value); });
                });
            },
            [&](const Stmt::OpAssign& assign_op) {
                print("AssignOp: {}\n", to_string(assign_op.op));
                indent([&] {
                    format(*assign_op.target);
                    format(*assign_op.value);
                });
            }
        );
    }
    void format(const Item& item) {
        item.visit(
            [&](const Use& use) { print("Using: {}", use.module_name.join()); },
            [&](const FromUse& from_use) {
                print("FromUse: {}", from_use.module_name.join());
                indent([&] {
                    for (const auto& item : from_use.items) {
                        if (item.alias) {
                            print("Item: {} as {}\n", item.name, *item.alias);
                        } else {
                            print("Item: {}\n", item.name);
                        }
                    }
                });
            },
            [&](const Func& func) {
                print(
                    "Func: {}{}{}\n",
                    func.is_public ? "public " : "",
                    func.is_extern ? "extern " : "",
                    func.name
                );
                indent([&] {
                    print("Args:\n");
                    indent([&] {
                        for (const auto& arg : func.args) {
                            print("{}", arg.name);
                            if (arg.type) {
                                disable_indent([&] { format(*arg.type); });
                            }
                        }
                    });
                    print("Min pos args: {}\n", func.min_pos_args);
                    print("Max pos args: {}\n", func.max_pos_args);
                    if (func.return_type) {
                        print("Returns:\n");
                        indent([&] { format(*func.return_type); });
                    }
                    if (func.body) {
                        print("Body:\n");
                        indent([&] { format(*func.body); });
                    }
                });
            },
            [&](const Struct& struct_item) {
                print("Struct: {}\n", struct_item.name);
                indent([&] {
                    for (const auto& field : struct_item.fields) {
                        print("{}", field.name);
                        disable_indent([&] { format(*field.type); });
                    }
                });
            }
        );
    }
    void format(const Module& module) {
        print("Module: {}\n", module.source->module_name);
        indent([&] {
            for (const auto& item : module.items) {
                format(item);
            }
        });
    }

    static std::string_view to_string(Expr::BinaryOp op) {
        switch (op) {
            using enum Expr::BinaryOp;
            case Add: return "+";
            case Sub: return "-";
            case Mul: return "*";
            case Div: return "/";
            case Mod: return "%";
            case LShift: return "<<";
            case RShift: return ">>";
            case BitAnd: return "&";
            case BitOr: return "|";
            case BitXor: return "^";
            case LogicalOr: return "or";
            case LogicalAnd: return "and";
        }
        std::unreachable();
    }
    static std::string to_string(std::span<const Expr::ComparisonOp> ops) {
        std::string result;
        for (auto op : ops.first(ops.size() - 1)) {
            std::format_to(std::back_inserter(result), "{}, ", to_string(op));
        }
        std::format_to(std::back_inserter(result), "{}", to_string(ops.back()));
        return result;
    }
    static std::string_view to_string(Expr::ComparisonOp op) {
        switch (op) {
            using enum Expr::ComparisonOp;
            case Less: return "<";
            case Greater: return ">";
            case LessEqual: return "<=";
            case GreaterEqual: return ">=";
            case Equal: return "==";
            case NotEqual: return "!=";
        }
        std::unreachable();
    }
    static std::string_view to_string(Expr::UnaryOp op) {
        switch (op) {
            using enum Expr::UnaryOp;
            case Not: return "not";
            case Neg: return "-";
            case BitNot: return "~";
            case Deref: return ".*";
            case AddressOf: return "&";
        }
        std::unreachable();
    }
    static std::string_view to_string(Expr::Specifier spec) {
        switch (spec) {
            using enum Expr::Specifier;
            case Let: return "let";
            case Var: return "var";
            case Val: return "val";
        }
        std::unreachable();
    }
    static std::string_view to_string(Stmt::AssignOp op) {
        switch (op) {
            using enum Stmt::AssignOp;
            case Add: return "+=";
            case Sub: return "-=";
            case Mul: return "*=";
            case Div: return "/=";
            case Mod: return "%=";
            case LShift: return "<<=";
            case RShift: return ">>=";
            case BitAnd: return "&=";
            case BitOr: return "|=";
            case BitXor: return "^=";
        }
        std::unreachable();
    }

    template <typename... Args>
    void print(std::format_string<Args...> string, Args&&... args) {
        if (!disable_indent_) {
            std::format_to(
                std::back_inserter(result), "{:{}}", "", indent_ * 2
            );
        } else {
            disable_indent_ = false;
        }
        std::format_to(
            std::back_inserter(result), string, std::forward<Args>(args)...
        );
    }
    void indent(std::invocable auto&& func) {
        indent_++;
        std::invoke(func);
        indent_--;
    }
    void disable_indent(std::invocable auto&& func) {
        disable_indent_ = true;
        std::invoke(func);
        disable_indent_ = false;
    }

    std::string result;
    int indent_ = 0;
    bool disable_indent_ = false;
};
}

std::string to_string(const Expr& expr) { return Formatter {}.to_string(expr); }
std::string to_string(const Stmt& stmt) { return Formatter {}.to_string(stmt); }
std::string to_string(const Item& item) { return Formatter {}.to_string(item); }
std::string to_string(const Module& module) {
    return Formatter {}.to_string(module);
}
}