#include "nodes.h"

namespace acu::nodes {
namespace {
// Helper function to join vector elements with a separator
template <typename T, typename F>
std::string join_vector(
    const std::vector<T>& vec, const std::string& sep, F func
) {
    std::string result;
    bool first = true;
    for (const auto& item : vec) {
        if (!first) {
            result += sep;
        }
        result += func(item);
        first = false;
    }
    return result;
}

// Helper function to indent multi-line strings
std::string indent_string(std::string_view str, int indent_level = 1) {
    std::string indent(static_cast<std::size_t>(indent_level * 2), ' ');
    std::string result;
    size_t pos = 0;
    size_t next_pos = 0;

    while ((next_pos = str.find('\n', pos)) != std::string::npos) {
        result += str.substr(pos, next_pos - pos + 1);
        if (next_pos + 2 < str.length()) {
            result += indent;
        }
        pos = next_pos + 1;
    }
    result += str.substr(pos);
    return result;
}

std::string location_to_string(acu::Location loc) {
    return std::format("[{}:{}]", loc.start, loc.end);
}

std::string to_string(
    const nodes::Expr::Literal& literal, acu::Location location = {}
) {
    std::string value_str = literal.value.visit(
        [](bool val) -> std::string { return val ? "true" : "false"; },
        [](std::int64_t val) -> std::string { return std::format("{}", val); },
        [](double val) -> std::string { return std::format("{:.6f}", val); },
        [](char32_t val) -> std::string {
            return std::format("'{}'", static_cast<char>(val));
        },
        [](std::string_view val) -> std::string {
            return std::format("\"{}\"", std::string(val));
        }
    );
    return std::format(
        "Literal({}) @ {}", value_str, location_to_string(location)
    );
}

std::string to_string(const Expr::Name& name, acu::Location location = {}) {
    return std::format(
        "Name({}) @ {}", std::string(name.name), location_to_string(location)
    );
}

std::string binary_op_to_string(Expr::BinaryOp op) {
    switch (op) {
        using enum Expr::BinaryOp;
        case Add:
            return "+";
        case Sub:
            return "-";
        case Mul:
            return "*";
        case Div:
            return "/";
        case Mod:
            return "%";
        case LShift:
            return "<<";
        case RShift:
            return ">>";
        case BitAnd:
            return "&";
        case BitOr:
            return "|";
        case BitXor:
            return "^";
        case LogicalOr:
            return "or";
        case LogicalAnd:
            return "and";
        default:
            return "unknown_op";
    }
}

std::string to_string(const Expr::Binary& binary, acu::Location location = {}) {
    std::string left_str = indent_string(to_string(*binary.left), 2);
    std::string right_str = indent_string(to_string(*binary.right), 2);
    return std::format(
        "Binary(\n  {},\n  {},\n  {}\n) @ {}",
        left_str,
        binary_op_to_string(binary.op),
        right_str,
        location_to_string(location)
    );
}

std::string comparison_op_to_string(Expr::ComparisonOp op) {
    switch (op) {
        using enum Expr::ComparisonOp;
        case Less:
            return "<";
        case Greater:
            return ">";
        case LessEqual:
            return "<=";
        case GreaterEqual:
            return ">=";
        case Equal:
            return "==";
        case NotEqual:
            return "!=";
        default:
            return "unknown_comp_op";
    }
}

std::string to_string(
    const Expr::Comparison& comparison, acu::Location location = {}
) {
    if (comparison.operands.empty()) {
        return std::format("Comparison() @ {}", location_to_string(location));
    }

    std::string result = std::format(
        "Comparison(\n  {}",
        indent_string(to_string(*comparison.operands[0]), 2)
    );

    for (size_t i = 0; i < comparison.operators.size(); ++i) {
        if (i + 1 < comparison.operands.size()) {
            std::string operand_str =
                indent_string(to_string(*comparison.operands[i + 1]), 2);
            result += std::format(
                ",\n  {} {}",
                comparison_op_to_string(comparison.operators[i]),
                operand_str
            );
        }
    }

    result += std::format("\n) @ {}", location_to_string(location));
    return result;
}

std::string unary_op_to_string(Expr::UnaryOp op) {
    switch (op) {
        using enum nodes::Expr::UnaryOp;
        case Not:
            return "!";
        case Neg:
            return "-";
        case BitNot:
            return "~";
        case Deref:
            return "*";
        case AddressOf:
            return "&";
        default:
            return "unknown_unary_op";
    }
}

std::string to_string(const Expr::Unary& unary, acu::Location location = {}) {
    std::string operand_str = indent_string(to_string(*unary.operand), 2);
    return std::format(
        "Unary(\n  {},\n  {}\n) @ {}",
        unary_op_to_string(unary.op),
        operand_str,
        location_to_string(location)
    );
}

std::string to_string(const Expr::Call& call, acu::Location location = {}) {
    std::string value_str = indent_string(to_string(*call.value), 2);
    std::string args_str = join_vector(call.args, ",\n", [](const auto& arg) {
        return indent_string(to_string(*arg), 2);
    });
    if (!args_str.empty()) {
        args_str = "\n" + args_str;
    }
    return std::format(
        "Call(\n  {},\n  [{}]\n) @ {}",
        value_str,
        args_str,
        location_to_string(location)
    );
}

std::string to_string(
    const Expr::GetItem& getitem, acu::Location location = {}
) {
    std::string value_str = indent_string(to_string(*getitem.value), 2);
    std::string args_str =
        join_vector(getitem.args, ",\n", [](const auto& arg) {
            return indent_string(to_string(*arg), 2);
        });
    if (!args_str.empty()) {
        args_str = "\n" + args_str;
    }
    return std::format(
        "GetItem(\n  {},\n  [{}]\n) @ {}",
        value_str,
        args_str,
        location_to_string(location)
    );
}

std::string to_string(
    const Expr::GetAttr& getattr, acu::Location location = {}
) {
    std::string value_str = indent_string(to_string(*getattr.value), 2);
    return std::format(
        "GetAttr(\n  {},\n  {}\n) @ {}",
        value_str,
        std::string(getattr.name),
        location_to_string(location)
    );
}

std::string to_string(
    const nodes::Expr::Array& array, acu::Location location = {}
) {
    std::string items_str =
        join_vector(array.items, ",\n", [](const auto& item) {
            return indent_string(to_string(*item), 2);
        });
    if (!items_str.empty()) {
        items_str = "\n" + items_str;
    }
    return std::format(
        "Array(\n[{}]\n) @ {}", items_str, location_to_string(location)
    );
}

std::string to_string(const Expr::As& as, acu::Location location = {}) {
    std::string value_str = indent_string(to_string(*as.value), 2);
    std::string type_str = indent_string(to_string(*as.type), 2);
    return std::format(
        "As(\n  {},\n  {}\n) @ {}",
        value_str,
        type_str,
        location_to_string(location)
    );
}

std::string to_string(
    const Stmt::Expr& expr_stmt, acu::Location location = {}
) {
    std::string expr_str = indent_string(to_string(*expr_stmt.expr), 2);
    return std::format(
        "ExprStmt(\n  {}\n) @ {}", expr_str, location_to_string(location)
    );
}

std::string to_string(const Stmt::Var& var, acu::Location location = {}) {
    std::string name_str = std::string(var.name);
    std::string type_str =
        var.type ? indent_string(to_string(*var.type), 2) : "None";
    std::string init_str =
        var.init ? indent_string(to_string(*var.init), 2) : "None";
    return std::format(
        "Var(\n  {},\n  {},\n  {}\n) @ {}",
        name_str,
        type_str,
        init_str,
        location_to_string(location)
    );
}

std::string to_string(const Stmt::Block& block, acu::Location location = {}) {
    std::string stmts_str =
        join_vector(block.stmts, ",\n", [](const auto& stmt) {
            return indent_string(to_string(*stmt), 2);
        });
    if (!stmts_str.empty()) {
        stmts_str = "\n" + stmts_str;
    }
    return std::format(
        "Block(\n  [{}]\n) @ {}", stmts_str, location_to_string(location)
    );
}

std::string to_string(const Stmt::If& if_stmt, acu::Location location = {}) {
    std::string cond_str = indent_string(to_string(*if_stmt.cond), 2);
    std::string then_str =
        indent_string(to_string(*if_stmt.then_block), 2);
    std::string else_str =
        if_stmt.else_block
            ? indent_string(to_string(*if_stmt.else_block), 2)
            : "None";
    return std::format(
        "If(\n  {},\n  {},\n  {}\n) @ {}",
        cond_str,
        then_str,
        else_str,
        location_to_string(location)
    );
}

std::string to_string(
    const Stmt::While& while_stmt, acu::Location location = {}
) {
    std::string cond_str = indent_string(to_string(*while_stmt.cond), 2);
    std::string body_str = indent_string(to_string(*while_stmt.body), 2);
    return std::format(
        "While(\n  {},\n  {}\n) @ {}",
        cond_str,
        body_str,
        location_to_string(location)
    );
}

std::string to_string(
    const Stmt::Return& return_stmt, acu::Location location = {}
) {
    std::string value_str =
        return_stmt.value ? indent_string(to_string(*return_stmt.value), 2)
                          : "None";
    return std::format(
        "Return(\n  {}\n) @ {}", value_str, location_to_string(location)
    );
}

std::string to_string(const Stmt::Break&, acu::Location location = {}) {
    return std::format("Break() @ {}", location_to_string(location));
}

std::string to_string(const Stmt::Continue&, acu::Location location = {}) {
    return std::format("Continue() @ {}", location_to_string(location));
}

std::string to_string(const Stmt::Assign& assign, acu::Location location = {}) {
    std::string targets_str =
        join_vector(assign.targets, ",\n", [](const auto& target) {
            return indent_string(to_string(*target), 2);
        });
    if (!targets_str.empty()) {
        targets_str = "\n" + targets_str;
    }
    std::string value_str = indent_string(to_string(*assign.value), 2);
    return std::format(
        "Assign(\n  [{}],\n  {}\n) @ {}",
        targets_str,
        value_str,
        location_to_string(location)
    );
}

std::string assign_op_to_string(Stmt::AssignOp op) {
    switch (op) {
        using enum Stmt::AssignOp;
        case Add:
            return "+=";
        case Sub:
            return "-=";
        case Mul:
            return "*=";
        case Div:
            return "/=";
        case Mod:
            return "%=";
        case LShift:
            return "<<=";
        case RShift:
            return ">>=";
        case BitAnd:
            return "&=";
        case BitOr:
            return "|=";
        case BitXor:
            return "^=";
        default:
            return "unknown_assign_op";
    }
}

std::string to_string(
    const Stmt::OpAssign& opassign, acu::Location location = {}
) {
    std::string target_str = indent_string(to_string(*opassign.target), 2);
    std::string value_str = indent_string(to_string(*opassign.value), 2);
    return std::format(
        "OpAssign(\n  {},\n  {},\n  {}\n) @ {}",
        target_str,
        assign_op_to_string(opassign.op),
        value_str,
        location_to_string(location)
    );
}

std::string to_string(const Stmt::Use& use, acu::Location location = {}) {
    std::string module_str = join_vector(
        use.module_name, ".", [](const auto& name) { return std::string(name); }
    );
    return std::format(
        "Use(\n  {}\n) @ {}", module_str, location_to_string(location)
    );
}

std::string to_string(
    const Stmt::FromUse& from_use, acu::Location location = {}
) {
    std::string module_str =
        join_vector(from_use.module_name, ".", [](const auto& name) {
            return std::string(name);
        });

    std::string items_str =
        join_vector(from_use.items, ",\n", [](const auto& item) {
            std::string alias_str =
                item.alias ? std::format(" as {}", std::string(*item.alias))
                           : "";
            return std::format("{}{}", std::string(item.name), alias_str);
        });
    if (!items_str.empty()) {
        items_str = "\n" + items_str;
    }

    return std::format(
        "FromUse(\n  {},\n  [{}]\n) @ {}",
        module_str,
        items_str,
        location_to_string(location)
    );
}

std::string to_string(const StructField& field) {
    std::string type_str = indent_string(to_string(*field.type), 2);
    return std::format(
        "StructField(\n  {},\n  {}\n) @ {}",
        field.name,
        type_str,
        location_to_string(field.location)
    );
}

std::string to_string(const FuncArg& arg) {
    std::string name_str = std::string(arg.name);
    std::string type_str =
        arg.type ? indent_string(to_string(*arg.type), 2) : "None";
    return std::format(
        "FuncArg(\n  {},\n  {}\n) @ {}",
        name_str,
        type_str,
        location_to_string(arg.location)
    );
}
}

std::string to_string(const Expr& expr) {
    return expr.value.visit([&expr](const auto& node) {
        return to_string(node, expr.location);
    });
}

std::string to_string(const Stmt& stmt) {
    return stmt.value.visit([&stmt](const auto& node) {
        return to_string(node, stmt.location);
    });
}

std::string to_string(const Func& func) {
    std::string name_str = std::string(func.name);
    std::string args_str = join_vector(func.args, ",\n", [](const auto& arg) {
        return indent_string(to_string(arg), 2);
    });
    if (!args_str.empty()) {
        args_str = "\n" + args_str;
    }
    std::string return_type_str =
        func.return_type ? indent_string(to_string(*func.return_type), 2)
                         : "None";
    std::string body_str = indent_string(to_string(*func.body), 2);
    return std::format(
        "Func(\n  {},\n  [{}],\n  {},\n  {}\n) @ {}",
        name_str,
        args_str,
        return_type_str,
        body_str,
        location_to_string(func.location)
    );
}

std::string to_string(const Struct& struct_def) {
    std::string fields =
        join_vector(struct_def.fields, ",\n", [](const StructField& field) {
            return indent_string(to_string(field));
        });
    return std::format(
        "Struct(\n  {},\n[{}]\n) @ {}",
        struct_def.name,
        fields,
        location_to_string(struct_def.location)
    );
}

std::string to_string(const Module& module) {
    std::string imports =
        join_vector(module.imports, ",\n", [](const auto& stmt) {
            return indent_string(to_string(*stmt));
        });
    std::string funcs = join_vector(module.funcs, ",\n", [](const Func& func) {
        return indent_string(to_string(func));
    });
    std::string structs =
        join_vector(module.structs, ",\n", [](const Struct& struct_def) {
            return indent_string(to_string(struct_def));
        });
    return std::format(
        "Module(\n  [{}],\n  [{}],\n  [{}]\n)", imports, funcs, structs
    );
}
}