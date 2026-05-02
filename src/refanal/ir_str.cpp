#include "refanal/ir_str.h"

#include <format>
#include <string>
#include "refanal/ir.h"

namespace acu::refanal {

namespace {

std::string to_string(const ir::Local& local, const types::TypePool& types) {
    return std::format("{}: {}", local.name, types.to_string(local.type));
}

std::string to_string(const ir::Const::Value& value) {
    return value.visit(
        [&](bool v) -> std::string { return v ? "true" : "false"; },
        [&](std::int64_t v) { return std::to_string(v); },
        [&](double v) { return std::to_string(v); },
        [&](char32_t v) { return std::to_string(v); },
        [&](std::string_view v) { return std::string(v); },
        [&](ir::FuncRef func) { return std::format("func {}", func.index); },
        [&](ir::UsedFuncRef func) { return std::format("used func {}", func.index); }
    );
}

std::string binary_op_to_string(ir::BinaryOp op) {
    switch (op) {
        using enum ir::BinaryOp;
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
        default: return "?";
    }
}

std::string unary_op_to_string(ir::UnaryOp op) {
    switch (op) {
        using enum ir::UnaryOp;
        case Not: return "not";
        case Neg: return "-";
        case BitNot: return "~";
        default: return "?";
    }
}

std::string comparison_op_to_string(ir::ComparisonOp op) {
    switch (op) {
        using enum ir::ComparisonOp;
        case Less: return "<";
        case Greater: return ">";
        case LessEqual: return "<=";
        case GreaterEqual: return ">=";
        case Equal: return "==";
        case NotEqual: return "!=";
        default: return "?";
    }
}

std::string to_string(ir::OperandRef ref, const ir::Func& func);

std::string to_string(const ir::Place& place, const ir::Func& func) {
    std::string str = std::format("_{}", place.local.index);
    auto projections = func.projections(place.projections);
    for (const auto& proj : projections) {
        str = proj.data.visit(
            [&](ir::Projection::Index i) {
                return std::format("({})[{}]", str, to_string(i.index, func));
            },
            [&](ir::Projection::Field f) {
                return std::format("({}).{}", str, f.field);
            },
            [&](ir::Projection::Deref) {
                return std::format("*({})", str);
            }
        );
    }
    return str;
}

std::string to_string(const ir::Operand& operand, const ir::Func& func) {
    return operand.data.visit(
        [&](const ir::Const& c) { return to_string(c.value); },
        [&](const ir::Place& p) { return to_string(p, func); }
    );
}

std::string to_string(ir::OperandRef ref, const ir::Func& func) {
    return to_string(func.operand(ref), func);
}

std::string to_string(const ir::RValue& rvalue, const ir::Func& func) {
    return rvalue.data.visit(
        [&](const ir::RValue::Use& v) { return to_string(v.operand, func); },
        [&](const ir::RValue::Unary& v) {
            return std::format(
                "{} {}", unary_op_to_string(v.op), to_string(v.operand, func)
            );
        },
        [&](const ir::RValue::Binary& v) {
            return std::format(
                "{} {}, {}",
                binary_op_to_string(v.op),
                to_string(v.left, func),
                to_string(v.right, func)
            );
        },
        [&](const ir::RValue::Comparison& v) {
            return std::format(
                "{} {}, {}",
                comparison_op_to_string(v.op),
                to_string(v.left, func),
                to_string(v.right, func)
            );
        },
        [&](const ir::RValue::Call& v) {
            std::string str = std::format("call {}(", to_string(v.callee, func));
            auto args = func.operands(v.args);
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) str += ", ";
                str += to_string(args[i], func);
            }
            str += ")";
            return str;
        },
        [&](const ir::RValue::AddressOf& v) {
            return std::format("addrof {}", to_string(v.place, func));
        },
        [&](const ir::RValue::Cast& v) {
            return std::format("cast {}", to_string(v.operand, func));
        },
        [&](const ir::RValue::CreateStruct& v) {
            std::string str = std::format("struct {}(", v.type.index);
            auto args = func.operands(v.args);
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) str += ", ";
                str += to_string(args[i], func);
            }
            str += ")";
            return str;
        },
        [&](const ir::RValue::Array& v) {
            std::string str = "array(";
            auto items = func.operands(v.items);
            for (size_t i = 0; i < items.size(); ++i) {
                if (i > 0) str += ", ";
                str += to_string(items[i], func);
            }
            str += ")";
            return str;
        }
    );
}

}  // namespace

std::string to_string(
    const ir::Func& func, const acu::ir::AnalyzedPackage& analyzed
) {
    std::string str;
    str += std::format("Func {}\n", func.name());
    str += "locals:\n";
    for (auto i : func.locals().indices()) {
        str += std::format(
            "  _{} = {}{}\n",
            i.index,
            to_string(func.locals()[i], analyzed.ir_package->types()),
            i.index < func.arg_count() ? " (argument)" : ""
        );
    }

    str += "blocks:\n";
    for (size_t b = 0; b < func.blocks().size(); ++b) {
        str += std::format("  block {}:\n", b);
        const auto& block = func.block(ir::BlockRef {static_cast<uint32_t>(b)});
        for (auto ref : block.statements) {
            const auto& stmt = func.statement(ref);
            std::string ir_str = "    ";

            stmt.data.visit(
                [&](const ir::Statement::Assign& i) {
                    ir_str += std::format(
                        "{} = {}",
                        to_string(i.place, func),
                        to_string(i.rvalue, func)
                    );
                },
                [&](const ir::Statement::Nop&) { ir_str += "nop"; }
            );
            str += ir_str + "\n";
        }
        if (block.terminator) {
            std::string term_str = "    ";
            block.terminator->data.visit(
                [&](const ir::Terminator::Jump& i) {
                    term_str += std::format("jump to block {}", i.target.index);
                },
                [&](const ir::Terminator::Branch& i) {
                    term_str += std::format(
                        "branch on {} to block {} else block {}",
                        to_string(i.condition, func),
                        i.true_target.index,
                        i.false_target.index
                    );
                },
                [&](const ir::Terminator::Return& i) {
                    if (i.value)
                        term_str +=
                            std::format("return {}", to_string(*i.value, func));
                    else
                        term_str += "return";
                },
                [&](const ir::Terminator::Unreachable&) {
                    term_str += "unreachable";
                }
            );
            str += term_str + "\n";
        }
    }
    return str;
}



std::string to_string(
    const ir::Module& module, const acu::ir::AnalyzedPackage& analyzed
) {
    std::string str;
    for (const auto& func : module.funcs()) {
        str += to_string(func, analyzed) + "\n";
    }
    return str;
}

}  // namespace acu::refanal
