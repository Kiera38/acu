#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "../index.h"
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

struct ParamRef {
    std::uint32_t index;
};

struct Block {
    InstRef start;
    InstRef end;
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

    struct VarDecl {
        std::string_view name;
        std::optional<types::SpecType> type;
    };

    struct LoadVar {
        InstRef var;
    };

    struct LoadParam {
        ParamRef param;
    };

    struct Store {
        InstRef var;
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
        types::SpecType type;
    };

    using Value = utils::Variant<
        Const,
        VarDecl,
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

struct Param {
    std::string_view name;
    types::SpecType type;
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
    types::TypeId type;
};

class Func {
public:
    Func(std::string_view name, const Source& source, Location location, bool is_extern = false)
        : name_(name), source_(&source), location_(location), is_extern_(is_extern) {}
    [[nodiscard]] const Source& source() const { return *source_; }
    [[nodiscard]] Location location() const { return location_; }
    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] bool is_extern() const { return is_extern_; }
    [[nodiscard]] Param param(ParamRef ref) const { return params_[ref]; }
    [[nodiscard]] IndexSpan<const Param, ParamRef> params() const {
        return params_.data();
    }
    [[nodiscard]] types::SpecType return_type() const { return return_type_; }
    void set_type(std::span<const Param> params, types::SpecType return_type) {
        params_.clear();
        params_.append_range(params);
        return_type_ = return_type;
    }
    [[nodiscard]] Inst inst(InstRef ref) const { return insts_[ref]; }
    [[nodiscard]] IndexSpan<const Inst, InstRef> insts() const {
        return insts_.data();
    }
    [[nodiscard]] IndexSpan<const Inst, InstRef> block(Block block) const {
        return insts_.data().subspan(
            block.start, block.end.index - block.start.index + 1
        );
    }
    [[nodiscard]] std::span<const InstRef> inst_refs(InstRefs refs) const {
        return std::span(inst_refs_).subspan(refs.start, refs.count);
    }

    [[nodiscard]] std::span<const Comparator> comparators(
        Comparators comparators
    ) const {
        return std::span(comparators_)
            .subspan(comparators.start, comparators.count);
    }

    [[nodiscard]] std::span<Comparator> comparators(Comparators comparators) {
        return std::span(comparators_)
            .subspan(comparators.start, comparators.count);
    }

    InstRef add(const Inst& inst) { return insts_.push_back(inst); }

    [[nodiscard]] InstRef last_inst() const { return insts_.last_index(); }

    void set_loop_block(InstRef loop, Block block) {
        insts_[loop].data.get<Inst::Loop>().block = block;
    }

    void set_if_blocks(
        InstRef if_, Block then_block, std::optional<Block> else_block
    ) {
        auto& if_inst = insts_[if_].data.get<Inst::If>();
        if_inst.then_block = then_block;
        if_inst.else_block = else_block;
    }

    void set_logical_block(InstRef logical, Block right) {
        insts_[logical].data.get<Inst::Logical>().right = right;
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
            .start = static_cast<std::uint32_t>(comparators_.size()),
            .count = static_cast<std::uint32_t>(comparators.size())
        };
        comparators_.append_range(comparators);
        insts_[comparison].data.get<Inst::Comparison>().comparators = comp;
    }

private:
    const Source* source_;
    Location location_;
    std::string_view name_;
    bool is_extern_ = false;
    IndexVector<Param, ParamRef> params_;
    types::SpecType return_type_;
    IndexVector<Inst, InstRef> insts_;
    std::vector<InstRef> inst_refs_;
    std::vector<Comparator> comparators_;
};

class Package {
public:
    Package() = default;
    Func& func(FuncRef ref) { return funcs_[ref]; }
    [[nodiscard]] IndexSpan<const Func, FuncRef> funcs() const { return funcs_.data(); }
    [[nodiscard]] const Func& func(FuncRef ref) const { return funcs_[ref]; }

    FuncRef add(Func&& func) { return funcs_.push_back(std::move(func)); }

    types::TypePool& types() { return types_; }
    [[nodiscard]] const types::TypePool& types() const { return types_; }

    types::TypeId func_type(FuncRef ref);

private:
    IndexVector<Func, FuncRef> funcs_;
    types::TypePool types_;
};

std::string to_string(const Func& func);
std::string to_string(const Package& package);
}
