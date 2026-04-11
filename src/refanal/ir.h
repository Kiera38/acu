#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "../index.h"
#include "semanal/types.h"
#include "source.h"
#include "variant.h"

namespace acu::refanal::ir {

struct FuncRef {
    std::uint32_t index;
};

struct InstRef {
    std::uint32_t index;
    auto operator<=>(const InstRef& other) const = default;
};

struct BlockRef {
    std::uint32_t index;

    auto operator<=>(const BlockRef& other) const = default;
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

    [[nodiscard]] bool has_value() const {
        return !data.is<Store>() && !data.is<SetField>() &&
               !data.is<SetItem>() && !data.is<Jump>() && !data.is<Branch>() &&
               !data.is<Return>() && !data.is<VarDecl>();
    }

    [[nodiscard]] bool is_terminator() const {
        return data.is<Jump>() || data.is<Branch>() || data.is<Return>();
    }
};

struct Block {
    std::vector<InstRef> insts;
    std::vector<BlockRef> preds;
    std::vector<BlockRef> succs;
};

struct Param {
    std::string_view name;
    types::SpecType type;
};

class Func {
public:
    Func(std::string_view name, bool is_extern = false)
        : name_(name), is_extern_(is_extern) {}

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] bool is_extern() const { return is_extern_; }
    [[nodiscard]] Param param(ParamRef ref) const { return params_[ref]; }
    [[nodiscard]] IndexSpan<const Param, ParamRef> params() const {
        return params_.data();
    }
    [[nodiscard]] IndexSpan<Param, ParamRef> params() { return params_.data(); }
    [[nodiscard]] types::SpecType return_type() const { return return_type_; }
    void set_type(std::span<const Param> params, types::SpecType return_type) {
        params_.clear();
        params_.append_range(params);
        return_type_ = return_type;
    }
    void set_return_type(types::SpecType return_type) {
        return_type_ = return_type;
    }

    [[nodiscard]] const Inst& inst(InstRef ref) const { return insts_[ref]; }
    [[nodiscard]] Inst& inst(InstRef ref) { return insts_[ref]; }
    [[nodiscard]] IndexSpan<const Inst, InstRef> insts() const {
        return insts_.data();
    }
    [[nodiscard]] IndexSpan<Inst, InstRef> insts() { return insts_.data(); }

    [[nodiscard]] const Block& block(BlockRef ref) const {
        return blocks_[ref];
    }
    [[nodiscard]] Block& block(BlockRef ref) { return blocks_[ref]; }
    [[nodiscard]] IndexSpan<const Block, BlockRef> blocks() const {
        return blocks_.data();
    }
    [[nodiscard]] IndexSpan<Block, BlockRef> blocks() { return blocks_.data(); }

    [[nodiscard]] std::span<const InstRef> inst_refs(InstRefs refs) const {
        return std::span(inst_refs_).subspan(refs.start, refs.count);
    }
    [[nodiscard]] std::span<InstRef> inst_refs(InstRefs refs) {
        return std::span(inst_refs_).subspan(refs.start, refs.count);
    }

    InstRef add(const Inst& inst) { return insts_.push_back(inst); }

    BlockRef add_block(Block block) {
        return blocks_.push_back(std::move(block));
    }

    void set_block(BlockRef ref, Block block) {
        blocks_[ref] = std::move(block);
    }

    void replace_blocks(std::span<const Block> blocks) {
        blocks_.clear();
        for (const auto& b : blocks) {
            blocks_.push_back(b);
        }
    }

    InstRefs add(std::span<const InstRef> refs) {
        InstRefs inst_refs {
            .start = static_cast<std::uint32_t>(inst_refs_.size()),
            .count = static_cast<std::uint32_t>(refs.size())
        };
        inst_refs_.append_range(refs);
        return inst_refs;
    }

    void rebuild_cfg() {
        for (auto i : blocks_.indices()) {
            auto& block = blocks_[i];
            block.preds.clear();
            block.succs.clear();
        }

        for (auto i : blocks_.indices()) {
            auto& block = blocks_[i];
            if (block.insts.empty()) continue;

            const auto& last = insts_[block.insts.back()];
            last.data.visit(
                [&](const Inst::Jump& j) {
                    block.succs.push_back(j.target);
                    blocks_[j.target].preds.push_back(i);
                },
                [&](const Inst::Branch& b) {
                    block.succs.push_back(b.true_target);
                    blocks_[b.true_target].preds.push_back(i);
                    block.succs.push_back(b.false_target);
                    blocks_[b.false_target].preds.push_back(i);
                },
                [&](const Inst::Return&) {},
                [&](auto&) {}
            );
        }
    }

private:
    std::string_view name_;
    bool is_extern_ = false;
    IndexVector<Param, ParamRef> params_;
    types::SpecType return_type_;
    IndexVector<Inst, InstRef> insts_;
    IndexVector<Block, BlockRef> blocks_;
    std::vector<InstRef> inst_refs_;
};

class Module {
public:
    Module(types::TypePool& types): types_(&types) {}
    Func& func(FuncRef ref) { return funcs_[ref]; }
    [[nodiscard]] IndexSpan<const Func, FuncRef> funcs() const {
        return funcs_.data();
    }
    [[nodiscard]] IndexSpan<Func, FuncRef> funcs() { return funcs_.data(); }
    [[nodiscard]] const Func& func(FuncRef ref) const { return funcs_[ref]; }

    FuncRef add(Func&& func) { return funcs_.push_back(std::move(func)); }

    types::TypePool& types() { return *types_; }
    const types::TypePool& types() const { return *types_; }

private:
    IndexVector<Func, FuncRef> funcs_;
    types::TypePool* types_;
};

}
