#include "refanal/ir_str.h"

#include <format>
#include <string>
#include "semanal/semanal.h"

namespace acu::refanal {

namespace {

std::string to_string(const ir::Param& param, const types::TypePool& types) {
    return std::format("{}: {}", param.name, types.to_string(param.type));
}

std::string to_string(const ir::Inst::Const::Value& value) {
    return value.visit(
        [&](bool v) -> std::string { return v ? "true" : "false"; },
        [&](std::int64_t v) { return std::to_string(v); },
        [&](double v) { return std::to_string(v); },
        [&](char32_t v) { return std::to_string(v); },
        [&](std::string_view v) { return std::string(v); },
        [&](ir::FuncRef func) { return std::format("func {}", func.index); }
    );
}

std::string binary_op_to_string(ir::Inst::BinaryOp op) {
    switch (op) {
        using enum ir::Inst::BinaryOp;
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

std::string unary_op_to_string(ir::Inst::UnaryOp op) {
    switch (op) {
        using enum ir::Inst::UnaryOp;
        case Not: return "not";
        case Neg: return "-";
        case BitNot: return "~";
        default: return "?";
    }
}

std::string comparison_op_to_string(ir::Inst::ComparisonOp op) {
    switch (op) {
        using enum ir::Inst::ComparisonOp;
        case Less: return "<";
        case Greater: return ">";
        case LessEqual: return "<=";
        case GreaterEqual: return ">=";
        case Equal: return "==";
        case NotEqual: return "!=";
        default: return "?";
    }
}

}  // namespace

std::string to_string(
    const ir::Func& func, const semanal::AnalyzedPackage& analyzed
) {
    std::string str;
    str += std::format("Func {}\n", func.name());
    str += "params:\n";
    for (auto i : func.params().indices()) {
        str += std::format(
            "  %{} = param {}\n",
            i.index,
            to_string(func.params()[i], analyzed.ir_package.types())
        );
    }

    str += "blocks:\n";
    for (size_t b = 0; b < func.blocks().size(); ++b) {
        str += std::format("  block {}:\n", b);
        const auto& block = func.block(ir::BlockRef {static_cast<uint32_t>(b)});
        for (auto ref : block.insts) {
            const auto& inst = func.inst(ref);
            std::string ir_str = std::format(
                "    %{} : {} = ",
                ref.index,
                analyzed.ir_package.types().to_string(inst.type)
            );

            inst.data.visit(
                [&](const ir::Inst::Const& i) {
                    ir_str += std::format("const {}", to_string(i.value));
                },
                [&](const ir::Inst::VarDecl& i) {
                    ir_str += std::format("var decl {}", i.name);
                },
                [&](const ir::Inst::LoadVar& i) {
                    ir_str += std::format("load var %{}", i.var.index);
                },
                [&](const ir::Inst::LoadParam& i) {
                    ir_str += std::format("load param %{}", i.param.index);
                },
                [&](const ir::Inst::Store& i) {
                    ir_str += std::format(
                        "store %{} -> var %{}", i.value.index, i.var.index
                    );
                },
                [&](const ir::Inst::Binary& i) {
                    ir_str += std::format(
                        "{} %{}, %{}",
                        binary_op_to_string(i.op),
                        i.left.index,
                        i.right.index
                    );
                },
                [&](const ir::Inst::Unary& i) {
                    ir_str += std::format(
                        "{} %{}", unary_op_to_string(i.op), i.value.index
                    );
                },
                [&](const ir::Inst::Comparison& i) {
                    ir_str += std::format(
                        "{} %{}, %{}",
                        comparison_op_to_string(i.op),
                        i.left.index,
                        i.right.index
                    );
                },
                [&](const ir::Inst::Call& i) {
                    ir_str += std::format("call %{}(", i.value.index);
                    auto args = func.inst_refs(i.args);
                    for (size_t j = 0; j < args.size(); ++j) {
                        if (j > 0) ir_str += ", ";
                        ir_str += std::format("%{}", args[j].index);
                    }
                    ir_str += ")";
                },
                [&](const ir::Inst::Cast& i) {
                    ir_str += std::format(
                        "cast %{} to type {}",
                        i.value.index,
                        inst.type.type.index
                    );
                },
                [&](const ir::Inst::CreateStruct& i) {
                    ir_str += std::format(
                        "create struct type {}(", i.struct_type.index
                    );
                    auto args = func.inst_refs(i.args);
                    for (size_t j = 0; j < args.size(); ++j) {
                        if (j > 0) ir_str += ", ";
                        ir_str += std::format("%{}", args[j].index);
                    }
                    ir_str += ")";
                },
                [&](const ir::Inst::GetField& i) {
                    ir_str += std::format(
                        "get field {} from %{}", i.index, i.value.index
                    );
                },
                [&](const ir::Inst::SetField& i) {
                    ir_str += std::format(
                        "set field {} on %{} with %{}",
                        i.index,
                        i.var.index,
                        i.value.index
                    );
                },
                [&](const ir::Inst::AddressOf& i) {
                    ir_str += std::format("addrof %{}", i.value.index);
                },
                [&](const ir::Inst::GetItem& i) {
                    ir_str += std::format(
                        "getitem %{}[%{}]", i.value.index, i.index.index
                    );
                },
                [&](const ir::Inst::SetItem& i) {
                    ir_str += std::format(
                        "setitem %{}[%{}] = %{}",
                        i.var.index,
                        i.index.index,
                        i.value.index
                    );
                },
                [&](const ir::Inst::Deref& i) {
                    ir_str += std::format("deref %{}", i.value.index);
                },
                [&](const ir::Inst::Array& i) {
                    ir_str += "array(";
                    auto items = func.inst_refs(i.items);
                    for (size_t j = 0; j < items.size(); ++j) {
                        if (j > 0) ir_str += ", ";
                        ir_str += std::format("%{}", items[j].index);
                    }
                    ir_str += ")";
                },
                [&](const ir::Inst::Jump& i) {
                    ir_str += std::format("jump to block {}", i.target.index);
                },
                [&](const ir::Inst::Branch& i) {
                    ir_str += std::format(
                        "branch on %{} to block {} else block {}",
                        i.condition.index,
                        i.true_target.index,
                        i.false_target.index
                    );
                },
                [&](const ir::Inst::Return& i) {
                    if (i.value)
                        ir_str += std::format("return %{}", i.value->index);
                    else
                        ir_str += "return";
                }
            );
            str += ir_str + "\n";
        }
    }
    return str;
}

std::string to_string(
    const ir::Module& module, const semanal::AnalyzedPackage& analyzed
) {
    std::string str;
    for (const auto& func : module.funcs()) {
        str += to_string(func, analyzed) + "\n";
    }
    return str;
}

}  // namespace acu::refanal
