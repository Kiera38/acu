#include <cstddef>
#include <format>
#include <string>

#include "ir.h"
#include "semanal/types.h"

namespace acu::ir {
namespace {
std::string to_string(const Param& param) {
    return std::format("{}: {}", param.name, param.type.type.index);
}

std::string to_string(const Inst::Const::Value& value) {
    return value.visit(
        [&](bool v) -> std::string { return v ? "true" : "false"; },
        [&](std::int64_t v) { return std::to_string(v); },
        [&](double v) { return std::to_string(v); },
        [&](char32_t v) { return std::to_string(v); },
        [&](std::string_view v) { return std::string(v); },
        [&](FuncRef func) { return std::format("func {}", func.index); },
        [&](types::TypeId type) { return std::format("type {}", type.index); }
    );
}

// helpers for printing operator enums
static std::string binary_op_to_string(Inst::BinaryOp op) {
    switch (op) {
        using enum Inst::BinaryOp;
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

static std::string logical_op_to_string(Inst::LogicalOp op) {
    switch (op) {
        using enum Inst::LogicalOp;
        case And: return "and";
        case Or: return "or";
        default: return "?";
    }
}

static std::string unary_op_to_string(Inst::UnaryOp op) {
    switch (op) {
        using enum Inst::UnaryOp;
        case Not: return "not";
        case Neg: return "-";
        case BitNot: return "~";
        default: return "?";
    }
}

static std::string comparison_op_to_string(ComparisonOp op) {
    switch (op) {
        using enum ComparisonOp;
        case Less: return "<";
        case Greater: return ">";
        case LessEqual: return "<=";
        case GreaterEqual: return ">=";
        case Equal: return "==";
        case NotEqual: return "!=";
        default: return "?";
    }
}

static std::string indent_string(std::string_view str, int indent_level = 1) {
    std::string result;
    std::string indent(static_cast<size_t>(indent_level) * 2, ' ');
    size_t start = 0;
    while (start < str.size()) {
        auto pos = str.find('\n', start);
        if (pos == std::string::npos) {
            result += indent;
            result.append(str.data() + start, str.size() - start);
            break;
        }
        result += indent;
        result.append(str.data() + start, pos - start + 1);
        start = pos + 1;
    }
    return result;
}

std::string to_string(
    std::span<const Inst> block, size_t offset, const Func& func
) {
    std::string block_str;
    for (size_t i = 0; i < block.size(); i++) {
        size_t idx = offset + i;
        block[i].data.visit(
            [&](const Inst::Const& inst) {
                block_str +=
                    std::format("%{} = const {}\n", idx, to_string(inst.value));
            },
            [&](const Inst::LoadVar& inst) {
                block_str +=
                    std::format("%{} = load var %{}\n", idx, inst.var.index);
            },
            [&](const Inst::LoadParam& inst) {
                block_str += std::format(
                    "%{} = load param %{}\n", idx, inst.param.index
                );
            },
            [&](const Inst::Store& inst) {
                block_str += std::format(
                    "%{} = store %{} -> var %{}\n",
                    idx,
                    inst.value.index,
                    inst.var.index
                );
            },
            [&](const Inst::Binary& inst) {
                block_str += std::format(
                    "%{} = {} %{} , %{}\n",
                    idx,
                    binary_op_to_string(inst.op),
                    inst.left.index,
                    inst.right.index
                );
            },
            [&](const Inst::Logical& inst) {
                block_str += std::format(
                    "%{} = logical %{} {} {{\n",
                    idx,
                    inst.left.index,
                    logical_op_to_string(inst.op)
                );
                auto all = func.insts();
                block_str += indent_string(
                    to_string(
                        all.subspan(
                            InstRef{inst.right.start.index},
                            inst.right.end.index - inst.right.start.index + 1
                        ),
                        inst.right.start.index,
                        func
                    ),
                    1
                );
                i += inst.right.end.index - offset - i;

                block_str += "}\n";
            },
            [&](const Inst::Unary& inst) {
                block_str += std::format(
                    "%{} = {} %{}\n",
                    idx,
                    unary_op_to_string(inst.op),
                    inst.value.index
                );
            },
            [&](const Inst::Comparison& inst) {
                block_str +=
                    std::format("%{} = comparison %{}\n", idx, inst.left.index);
                auto comps = func.comparators(inst.comparators);
                auto all = func.insts();
                for (size_t k = 0; k < comps.size(); ++k) {
                    auto& comp = comps[k];
                    block_str += std::format(
                        "    {} block[{},{}]\n",
                        comparison_op_to_string(comp.op),
                        comp.value.start.index,
                        comp.value.end.index
                    );
                    block_str += indent_string(
                        to_string(
                            all.subspan(
                                InstRef{comp.value.start.index},
                                comp.value.end.index - comp.value.start.index +
                                    1
                            ),
                            comp.value.start.index,
                            func
                        ),
                        2
                    );
                }
            },
            [&](const Inst::Call& inst) {
                block_str +=
                    std::format("%{} = call %{}(", idx, inst.value.index);
                auto refs = func.inst_refs(inst.args);
                for (size_t j = 0; j < refs.size(); ++j) {
                    if (j) block_str += ", ";
                    block_str += std::format("%{}", refs[j].index);
                }
                block_str += ")\n";
            },
            [&](const Inst::Loop& inst) {
                block_str += std::format("%{} = loop {{\n", idx);
                auto all = func.insts();
                block_str += indent_string(
                    to_string(
                        all.subspan(
                            InstRef{inst.block.start.index},
                            inst.block.end.index - inst.block.start.index + 1
                        ),
                        inst.block.start.index,
                        func
                    ),
                    1
                );
                i += inst.block.end.index - offset - i;
                block_str += "}\n";
            },
            [&](const Inst::If& inst) {
                block_str += std::format(
                    "%{} = if %{} then {{\n", idx, inst.value.index
                );
                auto all = func.insts();
                block_str += indent_string(
                    to_string(
                        all.subspan(
                            InstRef{inst.then_block.start.index},
                            inst.then_block.end.index -
                                inst.then_block.start.index + 1
                        ),
                        inst.then_block.start.index,
                        func
                    ),
                    1
                );
                i += inst.then_block.end.index - offset - i;
                if (inst.else_block.has_value()) {
                    auto& eb = *inst.else_block;
                    block_str += "} else {\n";
                    auto all = func.insts();
                    block_str += indent_string(
                        to_string(
                            all.subspan(
                                InstRef{eb.start.index},
                                eb.end.index - eb.start.index + 1
                            ),
                            eb.start.index,
                            func
                        ),
                        1
                    );
                    i += eb.end.index - offset - i;
                }
                block_str += "}\n";
            },
            [&](const Inst::Return& inst) {
                if (inst.value.has_value())
                    block_str += std::format(
                        "%{} = return %{}\n", idx, inst.value->index
                    );
                else
                    block_str += std::format("%{} = return\n", idx);
            },
            [&](const Inst::Break&) {
                block_str += std::format("%{} = break\n", idx);
            },
            [&](const Inst::Continue&) {
                block_str += std::format("%{} = continue\n", idx);
            },
            [&](const Inst::AddressOf& inst) {
                block_str +=
                    std::format("%{} = addrof %{}\n", idx, inst.value.index);
            },
            [&](const Inst::Deref& inst) {
                block_str +=
                    std::format("%{} = deref %{}\n", idx, inst.value.index);
            },
            [&](const Inst::GetItem& inst) {
                block_str += std::format(
                    "%{} = getitem %{}, %{}\n",
                    idx,
                    inst.value.index,
                    inst.index.index
                );
            },
            [&](const Inst::SetItem& inst) {
                block_str += std::format(
                    "%{} = setitem %{}, %{}, %{}\n",
                    idx,
                    inst.var.index,
                    inst.index.index,
                    inst.value.index
                );
            },
            [&](const Inst::GetAttr& inst) {
                block_str += std::format(
                    "%{} = getattr %{}, {}\n", idx, inst.value.index, inst.name
                );
            },
            [&](const Inst::SetAttr& inst) {
                block_str += std::format(
                    "%{} = setattr %{}, %{}, {}\n",
                    idx,
                    inst.var.index,
                    inst.value.index,
                    inst.name
                );
            },
            [&](const Inst::Array& inst) {
                block_str += std::format("%{} = array(", idx);
                auto refs = func.inst_refs(inst.items);
                for (size_t j = 0; j < refs.size(); ++j) {
                    if (j) block_str += ", ";
                    block_str += std::format("%{}", refs[j].index);
                }
                block_str += ")\n";
            },
            [&](const Inst::As& inst) {
                block_str += std::format(
                    "%{} = as %{} type {}\n",
                    idx,
                    inst.value.index,
                    inst.type.type.index
                );
            },
            [&](const auto&) {

            }
        );
    }
    return block_str;
}
}

std::string to_string(const Func& func) {
    std::string params_str;
    for (const auto& param : func.params()) {
        params_str += to_string(param) + '\n';
    }
    std::string code_str = to_string(func.insts(), 0, func);
    return std::format(
        "Func {}\nparams:{}\ncode:\n{}", func.name(), params_str, code_str
    );
}

std::string to_string(const Package& package) {
    std::string funcs_str;
    for (const auto& func : package.funcs()) {
        funcs_str += to_string(func) + '\n';
    }
    return std::format("Package\nfuncs:\n{}", funcs_str);
}

}