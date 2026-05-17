#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "../index.h"
#include "package_name.h"
#include "source.h"
#include "types.h"
#include "variant.h"

namespace acu {
class Project;
}

namespace acu::ir {
class Func;
using FuncRef = Ref<Func>;

struct Inst;
using InstRef = Ref<Inst>;
using Insts = RefRange<Inst>;

struct Param;
using ParamRef = Ref<Param>;
using Params = RefRange<Param>;

struct UsedFunc;
using UsedFuncRef = Ref<UsedFunc>;

struct Block {
    ir::InstRef end;
};

struct Comparator;
using Comparators = RefRange<Comparator>;

using InstRefs = RefRange<InstRef>;

struct CallArg {
    std::string_view name;
    InstRef value;
};
using CallArgs = RefRange<CallArg>;

struct Inst {
    struct Const {
        using Value = utils::Variant<
            bool,
            std::int64_t,
            double,
            char32_t,
            std::string_view,
            FuncRef,
            UsedFuncRef,
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
        CallArgs named_args;
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

struct Func {
    const Source* source;
    Location location;
    std::string_view name;
    bool is_extern;
    Params params;
    std::uint32_t min_pos_args;
    std::uint32_t max_pos_args;
    types::SpecType return_type;
    Insts insts;
};

class Package;
using PackageRef = Ref<Package>;

struct UsedFunc {
    PackageRef package;
    FuncRef func;
    types::TypeId type;

    [[nodiscard]] const Source& source(const Project& project) const;
    [[nodiscard]] Location location(const Project& project) const;
    [[nodiscard]] std::string_view name(const Project& project) const;
    [[nodiscard]] bool is_extern(const Project& project) const;
    [[nodiscard]] Param param(const Project& project, ParamRef ref) const;
    [[nodiscard]] IndexSpan<const Param, ParamRef> params(
        const Project& project
    ) const;
    [[nodiscard]] types::SpecType return_type(const Project& project) const;
};

class Module {
public:
    Module() = default;
    void add_func(std::string_view name, FuncRef func) {
        public_funcs_.insert({name, func});
    }
    void add_struct(std::string_view name, types::TypeId type) {
        structs_.insert({name, type});
    }
    [[nodiscard]] utils::Variant<std::monostate, FuncRef, types::TypeId> find(
        std::string_view name
    ) const;

private:
    std::unordered_map<std::string_view, FuncRef> public_funcs_;
    std::unordered_map<std::string_view, types::TypeId> structs_;
};

class Package {
public:
    Package() = default;
    Package(PackageName name) : name_(std::move(name)) {}
    [[nodiscard]] PackageNameRef name() const { return name_; }
    
    Module& root_module() { return root_module_; }
    [[nodiscard]] const Module& root_module() const { return root_module_; }

    Module& add_module(std::string_view name) {
        return modules_.insert({name, Module()}).first->second;
    }
    Module& module(std::string_view name) { return modules_.at(name); }
    [[nodiscard]] const Module& module(std::string_view name) const {
        return modules_.at(name);
    }

    Func& func(FuncRef ref) { return funcs_[ref]; }
    [[nodiscard]] IndexSpan<const Func, FuncRef> funcs() const {
        return funcs_.data();
    }
    [[nodiscard]] const Func& func(FuncRef ref) const { return funcs_[ref]; }

    FuncRef add(const Func& func) { return funcs_.push_back(func); }

    types::TypePool& types() { return types_; }
    [[nodiscard]] const types::TypePool& types() const { return types_; }

    types::TypeId func_type(FuncRef ref);

    UsedFuncRef add(UsedFunc func) { return used_funcs_.push_back(func); }
    [[nodiscard]] IndexSpan<const UsedFunc, UsedFuncRef> used_funcs() const {
        return used_funcs_.data();
    }

    [[nodiscard]] const UsedFunc& used_func(UsedFuncRef ref) const {
        return used_funcs_[ref];
    }

    Params add_params(std::span<const Param> params) {
        return params_.append_range(params);
    }
    [[nodiscard]] std::span<const Param> params(Params params) const {
        return params_.range(params);
    }
    [[nodiscard]] const Param& param(ParamRef ref) const {
        return params_[ref];
    }

    InstRef add(const Inst& inst) {
        return insts_.push_back(inst);
    }
    [[nodiscard]] InstRef last_inst() const { return insts_.last_index(); }
    [[nodiscard]] const Inst& inst(InstRef ref) const {
        return insts_[ref];
    }
    [[nodiscard]] std::span<const Inst> insts(Insts insts) const {
        return insts_.range(insts);
    }
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
    void set_comparators(
        InstRef comparison, std::span<const Comparator> comparators
    ) {
        auto comp = comparators_.append_range(comparators);
        insts_[comparison].data.get<Inst::Comparison>().comparators = comp;
    }

    [[nodiscard]] std::span<const InstRef> inst_refs(InstRefs refs) const {
        return inst_refs_.range(refs);
    }
    InstRefs add(std::span<const InstRef> refs) {
        return inst_refs_.append_range(refs);
    }

    [[nodiscard]] std::span<const Comparator> comparators(
        Comparators comparators
    ) const {
        return comparators_.range(comparators);
    }
    [[nodiscard]] std::span<Comparator> comparators(Comparators comparators) {
        return comparators_.range(comparators);
    }

    CallArgs add_call_args(std::span<const CallArg> args) {
        return named_args_.append_range(args);
    }
    [[nodiscard]] std::span<const CallArg> call_args(CallArgs args) const {
        return named_args_.range(args);
    }

private:
    PackageName name_;
    Module root_module_;
    std::unordered_map<std::string_view, Module> modules_;
    IndexVector<Func, FuncRef> funcs_;
    IndexVector<UsedFunc, UsedFuncRef> used_funcs_;
    types::TypePool types_;
    IndexVector<Param, ParamRef> params_;
    IndexVector<Inst, InstRef> insts_;
    IndexVector<InstRef> inst_refs_;
    IndexVector<Comparator> comparators_;
    IndexVector<CallArg> named_args_;
};

struct AFunc {
    FuncRef func{};
    IndexMap<ir::InstRef, types::SpecType> types;
};

struct AnalyzedPackage {
    Package* ir_package{};
    IndexVector<AFunc, FuncRef> funcs_;
};

std::string to_string(const Package& package, const Func& func);
std::string to_string(const Package& package);
}
