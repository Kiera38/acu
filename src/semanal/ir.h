#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "source.h"
#include "types.h"
#include "variant.h"

namespace acu::ir {
struct FuncRef {
    std::uint32_t index;
};

struct InstRef {
    std::uint32_t index;
};

struct Block {
    InstRef start;
    InstRef end;
};

struct ParamRef {
    std::uint32_t index;
};

struct VarRef {
    std::uint32_t index;
};

struct Comparators {
    std::uint32_t start;
    std::uint32_t count;
};

struct InstRefs {
    std::uint32_t start;
    std::uint32_t count;
};

struct Inst {
    struct Const {
        using Value = utils::Variant<
            bool,
            std::int64_t,
            double,
            char32_t,
            std::string_view,
            FuncRef,
            types::TypeId>;
        Value value;
    };

    struct LoadVar {
        VarRef var;
    };

    struct LoadParam {
        ParamRef param;
    };

    struct Store {
        VarRef var;
        InstRef value;
    };

    enum class BinaryOp : std::uint8_t {
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        LShift,
        RShift,
        BitAnd,
        BitOr,
        BitXor,
    };

    struct Binary {
        InstRef left;
        InstRef right;
        BinaryOp op;
    };

    enum class LogicalOp : std::uint8_t {
        And,
        Or,
    };

    struct Logical {
        InstRef left;
        Block right;
        LogicalOp op;
    };

    enum class UnaryOp : std::uint8_t {
        Not,
        Neg,
        BitNot,
    };

    struct Unary {
        InstRef value;
        UnaryOp op;
    };

    struct Comparison {
        InstRef left;
        Comparators comparators;
    };

    struct Call {
        InstRef value;
        InstRefs args;
    };

    struct Loop {
        Block block;
    };

    struct If {
        InstRef value {};
        Block then_block {};
        std::optional<Block> else_block;
    };

    struct Return {
        std::optional<InstRef> value;
    };

    struct Break {};

    struct Continue {};

    struct AddressOf {
        InstRef value;
    };

    struct GetItem {
        InstRef value;
        InstRef index;
    };

    struct SetItem {
        InstRef var;
        InstRef index;
        InstRef value;
    };

    struct GetAttr {
        InstRef value;
        std::string_view name;
    };

    struct SetAttr {
        InstRef var;
        InstRef value;
        std::string_view name;
    };

    struct Deref {
        InstRef value;
    };

    struct Array {
        InstRefs items;
    };

    struct As {
        InstRef value {};
        types::TypeId type;
    };

    using Value = utils::Variant<
        Const,
        LoadVar,
        LoadParam,
        Store,
        Binary,
        Logical,
        Unary,
        Comparison,
        Call,
        Loop,
        If,
        Return,
        Break,
        Continue,
        AddressOf,
        Deref,
        GetItem,
        SetItem,
        GetAttr,
        SetAttr,
        Array,
        As>;
    Value data;
    Location location;
};

struct Var {
    std::string_view name;
    std::optional<types::TypeId> type;
};

struct Param {
    std::string_view name;
    types::TypeId type;
};

enum class ComparisonOp : std::uint8_t {
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Equal,
    NotEqual
};

struct Comparator {
    Block value;
    ComparisonOp op;
};

class Func {
public:
    Func(std::string_view name) : name_(name) {}

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] Param param(ParamRef ref) const { return params_[ref.index]; }
    [[nodiscard]] std::span<const Param> params() const { return params_; }
    [[nodiscard]] types::TypeId return_type() const { return return_type_; }
    void set_type(std::span<const Param> params, types::TypeId return_type) {
        params_.append_range(params);
        return_type_ = return_type;
    }

    [[nodiscard]] Var var(VarRef ref) const { return vars_[ref.index]; }
    [[nodiscard]] std::span<const Var> vars() const {return vars_;}
    [[nodiscard]] Inst inst(InstRef ref) const { return insts_[ref.index]; }
    [[nodiscard]] std::span<const Inst> insts() const { return insts_; }
    [[nodiscard]] std::span<const Inst> block(Block block) const {
        return std::span(insts_).subspan(
            block.start.index, block.end.index - block.start.index
        );
    }
    [[nodiscard]] std::span<const Comparator> comparators(
        Comparators comparators
    ) const {
        return std::span(comparators_)
            .subspan(comparators.start, comparators.count);
    }

    [[nodiscard]] std::span<const InstRef> inst_refs(InstRefs refs) const {
        return std::span(inst_refs_).subspan(refs.start, refs.count);
    }

    VarRef add(const Var& var) {
        VarRef ref {static_cast<std::uint32_t>(vars_.size())};
        vars_.push_back(var);
        return ref;
    }

    InstRef add(const Inst& inst) {
        InstRef ref {static_cast<std::uint32_t>(insts_.size())};
        insts_.push_back(inst);
        return ref;
    }

    [[nodiscard]] InstRef last_inst() const {
        return {static_cast<std::uint32_t>(insts_.size()) - 1};
    }

    void set_loop_block(InstRef loop, Block block) {
        insts_[loop.index].data.get<Inst::Loop>().block = block;
    }

    void set_if_blocks(
        InstRef if_, Block then_block, std::optional<Block> else_block
    ) {
        auto& if_inst = insts_[if_.index].data.get<Inst::If>();
        if_inst.then_block = then_block;
        if_inst.else_block = else_block;
    }

    void set_logical_block(InstRef logical, Block right) {
        insts_[logical.index].data.get<Inst::Logical>().right = right;
    }

    InstRefs add(std::span<const InstRef> refs) {
        InstRefs inst_refs {
            .start = static_cast<std::uint32_t>(inst_refs_.size()),
            .count = static_cast<std::uint32_t>(refs.size())
        };
        inst_refs_.append_range(refs);
        return inst_refs;
    }

    void set_comparators(
        InstRef comparison, std::span<const Comparator> comparators
    ) {
        Comparators comp {
            .start = static_cast<std::uint32_t>(inst_refs_.size()),
            .count = static_cast<std::uint32_t>(comparators.size())
        };
        comparators_.append_range(comparators);
        insts_[comparison.index].data.get<Inst::Comparison>().comparators =
            comp;
    }

private:
    std::string_view name_;
    std::vector<Param> params_;
    types::TypeId return_type_;
    std::vector<Var> vars_;
    std::vector<Inst> insts_;
    std::vector<InstRef> inst_refs_;
    std::vector<Comparator> comparators_;
};

class Module {
public:
    Module() = default;
    Func& func(FuncRef ref) { return funcs_[ref.index]; }
    [[nodiscard]] std::span<const Func> funcs() const { return funcs_; }
    [[nodiscard]] const Func& func(FuncRef ref) const {
        return funcs_[ref.index];
    }

    FuncRef add(Func&& func) {
        FuncRef ref {static_cast<std::uint32_t>(funcs_.size())};
        funcs_.push_back(std::move(func));
        return ref;
    }

    types::TypePool& types() { return types_; }

    types::TypeId func_type(FuncRef ref);

private:
    std::vector<Func> funcs_;
    types::TypePool types_;
};

std::string to_string(const Func& func);
std::string to_string(const Module& module);
}
