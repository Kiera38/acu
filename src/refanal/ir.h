#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "semanal/types.h"
#include "source.h"
#include "variant.h"

namespace acu::refanal::ir {

struct FuncRef {
    std::uint32_t index;
};

struct InstRef {
    std::uint32_t index;
};

struct BlockRef {
    std::uint32_t index;
};

struct ParamRef {
    std::uint32_t index;
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
            FuncRef>;
        Value value;
    };

    struct VarDecl {
        std::string_view name;
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

    enum class UnaryOp : std::uint8_t {
        Not,
        Neg,
        BitNot,
    };

    struct Unary {
        InstRef value;
        UnaryOp op;
    };

    enum class ComparisonOp : std::uint8_t {
        Less,
        Greater,
        LessEqual,
        GreaterEqual,
        Equal,
        NotEqual
    };

    struct Comparison {
        InstRef left;
        InstRef right;
        ComparisonOp op;
    };

    struct Call {
        InstRef value;
        InstRefs args;
    };

    struct Cast {
        InstRef value;
        types::SpecType target_type;
    };

    struct CreateStruct {
        types::TypeId struct_type;
        InstRefs args;
    };

    struct GetField {
        InstRef value;
        std::uint32_t index;
    };

    struct SetField {
        InstRef var;
        std::uint32_t index;
        InstRef value;
    };

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

    struct Deref {
        InstRef value;
    };

    struct Array {
        InstRefs items;
    };

    struct Jump {
        BlockRef target;
    };

    struct Branch {
        InstRef condition;
        BlockRef true_target;
        BlockRef false_target;
    };

    struct Return {
        std::optional<InstRef> value;
    };

    using Value = utils::Variant<
        Const,
        VarDecl,
        LoadVar,
        LoadParam,
        Store,
        Binary,
        Unary,
        Comparison,
        Call,
        Cast,
        CreateStruct,
        GetField,
        SetField,
        AddressOf,
        GetItem,
        SetItem,
        Deref,
        Array,
        Jump,
        Branch,
        Return>;

    Value data;
    types::SpecType type;
    Location location;
};

struct Block {
    std::vector<InstRef> insts;
};

struct Param {
    std::string_view name;
    types::SpecType type;
};

class Func {
public:
    Func(std::string_view name) : name_(name) {}

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] Param param(ParamRef ref) const { return params_[ref.index]; }
    [[nodiscard]] std::span<const Param> params() const { return params_; }
    [[nodiscard]] types::SpecType return_type() const { return return_type_; }
    void set_type(std::span<const Param> params, types::SpecType return_type) {
        params_.append_range(params);
        return_type_ = return_type;
    }

    [[nodiscard]] const Inst& inst(InstRef ref) const {
        return insts_[ref.index];
    }
    [[nodiscard]] Inst& inst(InstRef ref) { return insts_[ref.index]; }
    [[nodiscard]] std::span<const Inst> insts() const { return insts_; }
    [[nodiscard]] std::span<Inst> insts() { return insts_; }

    [[nodiscard]] const Block& block(BlockRef ref) const {
        return blocks_[ref.index];
    }
    [[nodiscard]] Block& block(BlockRef ref) { return blocks_[ref.index]; }
    [[nodiscard]] std::span<const Block> blocks() const { return blocks_; }

    [[nodiscard]] std::span<const InstRef> inst_refs(InstRefs refs) const {
        return std::span(inst_refs_).subspan(refs.start, refs.count);
    }
    [[nodiscard]] std::span<InstRef> inst_refs(InstRefs refs) {
        return std::span(inst_refs_).subspan(refs.start, refs.count);
    }

    InstRef add(const Inst& inst) {
        InstRef ref {static_cast<std::uint32_t>(insts_.size())};
        insts_.push_back(inst);
        return ref;
    }

    BlockRef add_block(Block block) {
        BlockRef ref {static_cast<std::uint32_t>(blocks_.size())};
        blocks_.push_back(std::move(block));
        return ref;
    }

    void set_block(BlockRef ref, Block block) {
        blocks_[ref.index] = std::move(block);
    }

    void replace_blocks(std::vector<Block> blocks) {
        blocks_ = std::move(blocks);
    }

    InstRefs add(std::span<const InstRef> refs) {
        InstRefs inst_refs {
            .start = static_cast<std::uint32_t>(inst_refs_.size()),
            .count = static_cast<std::uint32_t>(refs.size())
        };
        inst_refs_.append_range(refs);
        return inst_refs;
    }

private:
    std::string_view name_;
    std::vector<Param> params_;
    types::SpecType return_type_;
    std::vector<Inst> insts_;
    std::vector<Block> blocks_;
    std::vector<InstRef> inst_refs_;
};

class Module {
public:
    Module() = default;
    Func& func(FuncRef ref) { return funcs_[ref.index]; }
    [[nodiscard]] std::span<const Func> funcs() const { return funcs_; }
    [[nodiscard]] std::span<Func> funcs() { return funcs_; }
    [[nodiscard]] const Func& func(FuncRef ref) const {
        return funcs_[ref.index];
    }

    FuncRef add(Func&& func) {
        FuncRef ref {static_cast<std::uint32_t>(funcs_.size())};
        funcs_.push_back(std::move(func));
        return ref;
    }

private:
    std::vector<Func> funcs_;
};

}
