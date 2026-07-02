#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "../index.h"
#include "semanal/types.h"
#include "source.h"
#include "variant.h"

namespace acu {
class Project;
}

namespace acu::refanal::ir {
class Func;
using FuncRef = Ref<Func>;

struct Statement;
using StatementRef = Ref<Statement>;

struct Block;
using BlockRef = Ref<Block>;

struct UsedFunc;
using UsedFuncRef = Ref<UsedFunc>;

using StatementRefs = RefRange<StatementRef>;

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

enum class UnaryOp : std::uint8_t {
    Not,
    Neg,
    BitNot,
};

enum class ComparisonOp : std::uint8_t {
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Equal,
    NotEqual
};

struct Const {
    using Value = utils::Variant<
        bool,
        std::int64_t,
        double,
        char32_t,
        std::string_view,
        FuncRef,
        UsedFuncRef>;
    Value value;
};

struct Local {
    std::string_view name;
    types::SpecType type;
};
using LocalRef = Ref<Local>;

struct Projection;
using ProjectionRef = Ref<Projection>;
using Projections = RefRange<Projection>;

struct Place {
    LocalRef local;
    Projections projections;
};

struct Operand {
    using Value = utils::Variant<Const, Place>;
    Value data;
    types::Specifier specifier {};
};
using OperandRef = Ref<Operand>;
using Operands = RefRange<OperandRef>;

struct Projection {
    struct Index {
        OperandRef index;
    };
    struct Field {
        std::uint32_t field;
    };
    struct Deref {};
    using Value = utils::Variant<Index, Field, Deref>;
    Value data;
};

struct RValue {
    struct Use {
        OperandRef operand;
    };
    struct Unary {
        OperandRef operand;
        UnaryOp op;
    };
    struct Binary {
        OperandRef left;
        OperandRef right;
        BinaryOp op;
    };
    struct Comparison {
        OperandRef left;
        OperandRef right;
        ComparisonOp op;
    };
    struct Call {
        OperandRef callee;
        Operands args;
    };
    struct AddressOf {
        Place place;
    };
    struct Cast {
        OperandRef operand;
    };
    struct CreateStruct {
        types::TypeId type;
        Operands args;
    };
    struct Array {
        Operands items;
    };

    using Value = utils::Variant<
        Use,
        Unary,
        Binary,
        Comparison,
        Call,
        AddressOf,
        Cast,
        CreateStruct,
        Array>;
    Value data;
};

struct Statement {
    struct Assign {
        Place place;
        RValue rvalue;
    };
    struct Nop {};

    using Value = utils::Variant<Assign, Nop>;
    Value data;
    Location location;
};

struct Terminator {
    struct Jump {
        BlockRef target;
    };

    struct Branch {
        OperandRef condition;
        BlockRef true_target;
        BlockRef false_target;
    };

    struct Return {
        std::optional<OperandRef> value;
    };

    struct Unreachable {};

    using Value = utils::Variant<Jump, Branch, Return, Unreachable>;

    Value data;
    Location location;
};

struct Block {
    std::vector<StatementRef> statements;
    std::optional<Terminator> terminator;
    std::vector<BlockRef> preds;
    std::vector<BlockRef> succs;
};

class Func {
public:
    Func(
        std::string_view name,
        const Source& source,
        Location location,
        bool is_extern,
        bool is_public,
        types::TypeId type,
        std::span<const Local> params
    )
        : name_(name),
          source_(&source),
          location_(location),
          is_extern_(is_extern),
          is_public_(is_public),
          type_(type),
          arg_count_(params.size()),
          locals_(params.begin(), params.end()) {}

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] const Source& source() const { return *source_; }
    [[nodiscard]] types::TypeId type() const { return type_; }
    [[nodiscard]] std::string mangle_name() const {
        if (is_extern_ || source_->module_name.empty()) {
            return std::string(name_);
        }
        return source_->module_name + '.' + std::string(name_);
    }
    [[nodiscard]] Location location() const { return location_; }
    [[nodiscard]] bool is_extern() const { return is_extern_; }
    [[nodiscard]] bool is_public() const { return is_public_; }
    [[nodiscard]] std::uint32_t arg_count() const { return arg_count_; }
    [[nodiscard]] const Local& param(LocalRef ref) const {
        return locals_[ref];
    }
    [[nodiscard]] IndexSpan<const Local, LocalRef> params() const {
        return locals_.data().subspan(LocalRef {0}, arg_count_);
    }
    [[nodiscard]] IndexSpan<Local, LocalRef> params() {
        return locals_.data().subspan(LocalRef {0}, arg_count_);
    }

    [[nodiscard]] const Statement& statement(StatementRef ref) const {
        return statements_[ref];
    }
    [[nodiscard]] Statement& statement(StatementRef ref) {
        return statements_[ref];
    }
    [[nodiscard]] IndexSpan<const Statement, StatementRef> statements() const {
        return statements_.data();
    }
    [[nodiscard]] IndexSpan<Statement, StatementRef> statements() {
        return statements_.data();
    }

    [[nodiscard]] const Block& block(BlockRef ref) const {
        return blocks_[ref];
    }
    [[nodiscard]] Block& block(BlockRef ref) { return blocks_[ref]; }
    [[nodiscard]] IndexSpan<const Block, BlockRef> blocks() const {
        return blocks_.data();
    }
    [[nodiscard]] IndexSpan<Block, BlockRef> blocks() { return blocks_.data(); }

    [[nodiscard]] std::span<const Projection> projections(
        Projections refs
    ) const {
        return projections_.range(refs);
    }
    [[nodiscard]] const Operand& operand(OperandRef ref) const {
        return operands_[ref];
    }
    [[nodiscard]] Operand& operand(OperandRef ref) { return operands_[ref]; }
    [[nodiscard]] std::span<const OperandRef> operands(Operands refs) const {
        return operand_refs_.range(refs);
    }

    StatementRef add(const Statement& statement) {
        return statements_.push_back(statement);
    }

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

    LocalRef add_local(Local local) { return locals_.push_back(local); }

    Projections add_projections(std::span<const Projection> projections) {
        return projections_.append_range(projections);
    }

    OperandRef add_operand(const Operand& operand) {
        return operands_.push_back(operand);
    }
    Operands add_operands(std::span<const OperandRef> operands) {
        return operand_refs_.append_range(operands);
    }

    [[nodiscard]] const Local& local(LocalRef ref) const {
        return locals_[ref];
    }
    [[nodiscard]] Local& local(LocalRef ref) { return locals_[ref]; }
    [[nodiscard]] IndexSpan<const Local, LocalRef> locals() const {
        return locals_.data();
    }
    [[nodiscard]] IndexSpan<Local, LocalRef> locals() { return locals_.data(); }

    [[nodiscard]] types::SpecType place_type(
        Place place, const types::TypePool& types
    ) const {
        auto type = locals_[place.local].type;
        for (auto projection : projections(place.projections)) {
            type = projection.data.visit(
                [&](Projection::Index i) {
                    if (auto ptr = types.get(type.type)
                                       .data.get_if<types::Type::Ptr>()) {
                        return ptr->type;
                    } else {
                        return types.get(type.type)
                            .data.get<types::Type::Array>()
                            .item;
                    }
                },
                [&](Projection::Field f) {
                    return types.get(type.type)
                        .data.get<types::Type::Struct>()
                        .fields[f.field]
                        .type;
                },
                [&](Projection::Deref) {
                    return types.get(type.type)
                        .data.get<types::Type::Ptr>()
                        .type;
                }
            );
        }
        return type;
    }

    void rebuild_cfg() {
        for (auto i : blocks_.indices()) {
            auto& block = blocks_[i];
            block.preds.clear();
            block.succs.clear();
        }

        for (auto i : blocks_.indices()) {
            auto& block = blocks_[i];
            if (!block.terminator) continue;

            block.terminator->data.visit(
                [&](const Terminator::Jump& j) {
                    block.succs.push_back(j.target);
                    blocks_[j.target].preds.push_back(i);
                },
                [&](const Terminator::Branch& b) {
                    block.succs.push_back(b.true_target);
                    blocks_[b.true_target].preds.push_back(i);
                    block.succs.push_back(b.false_target);
                    blocks_[b.false_target].preds.push_back(i);
                },
                [&](const Terminator::Return&) {},
                [&](const Terminator::Unreachable&) {},
                [&](auto&) {}
            );
        }
    }

private:
    const Source* source_;
    Location location_;
    std::string_view name_;
    bool is_extern_ = false;
    bool is_public_;
    types::TypeId type_;
    std::uint32_t arg_count_ = 0;
    IndexVector<Statement, StatementRef> statements_;
    IndexVector<Block, BlockRef> blocks_;
    IndexVector<Local, LocalRef> locals_;
    IndexVector<Projection, ProjectionRef> projections_;
    IndexVector<Operand, OperandRef> operands_;
    IndexVector<OperandRef> operand_refs_;
};

class Module;

struct ModuleRef {
    std::uint32_t index;
};

struct UsedFunc {
    ModuleRef module;
    FuncRef func;
    types::TypeId type;

    [[nodiscard]] std::string_view name(const Project& project) const;
    [[nodiscard]] const Source& source(const Project& project) const;
    [[nodiscard]] std::string mangle_name(const Project& project) const;
    [[nodiscard]] Location location(const Project& project) const;
    [[nodiscard]] bool is_extern(const Project& project) const;
    [[nodiscard]] const Local& param(
        const Project& project, LocalRef ref
    ) const;
    [[nodiscard]] IndexSpan<const Local, LocalRef> params(
        const Project& project
    ) const;
};

class Module {
public:
    Module() = default;
    Module(PackageNameRef name, types::TypePool& types)
        : name_(name), types_(&types) {}
    [[nodiscard]] PackageNameRef name() const { return name_; }
    Func& func(FuncRef ref) { return funcs_[ref]; }
    [[nodiscard]] IndexSpan<const Func, FuncRef> funcs() const {
        return funcs_.data();
    }
    [[nodiscard]] IndexSpan<Func, FuncRef> funcs() { return funcs_.data(); }
    [[nodiscard]] const Func& func(FuncRef ref) const { return funcs_[ref]; }

    [[nodiscard]] IndexSpan<UsedFunc, UsedFuncRef> used_funcs() {
        return used_funcs_.data();
    }
    [[nodiscard]] IndexSpan<const UsedFunc, UsedFuncRef> used_funcs() const {
        return used_funcs_.data();
    }
    [[nodiscard]] const UsedFunc& used_func(UsedFuncRef ref) const {
        return used_funcs_[ref];
    }

    FuncRef add(Func&& func) { return funcs_.push_back(std::move(func)); }
    UsedFuncRef add(UsedFunc func) { return used_funcs_.push_back(func); }

    [[nodiscard]] types::TypePool& types() const { return *types_; }

    [[nodiscard]] types::SpecType const_type(const Const& v) const {
        return v.value.visit(
            [&](bool) {
                return types::SpecType {
                    .type = types::Bool, .specifier = types::Specifier::Val
                };
            },
            [&](int64_t) {
                return types::SpecType {
                    .type = types::Int, .specifier = types::Specifier::Val
                };
            },
            [&](double) {
                return types::SpecType {
                    .type = types::Float, .specifier = types::Specifier::Val
                };
            },
            [&](char32_t) {
                return types::SpecType {
                    .type = types::UInt32, .specifier = types::Specifier::Val
                };
            },
            [&](std::string_view value) {
                return types::SpecType {
                    .type = types().add_array(
                        {.type = types::UInt8,
                         .specifier = types::Specifier::Val},
                        value.length() + 1
                    ),
                    .specifier = types::Specifier::Val
                };
            },
            [&](FuncRef ref) {
                return types::SpecType {
                    .type = funcs_[ref].type(),
                    .specifier = types::Specifier::Val
                };
            },
            [&](UsedFuncRef ref) {
                return types::SpecType {
                    .type = used_funcs_[ref].type,
                    .specifier = types::Specifier::Val
                };
            }
        );
    }

private:
    PackageNameRef name_;
    IndexVector<Func, FuncRef> funcs_;
    IndexVector<UsedFunc, UsedFuncRef> used_funcs_;
    types::TypePool* types_ {};
};

}
