#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "source.h"
#include "variant.h"

namespace acu::nodes {
struct Expr {
    struct Literal {
        using Value = utils::
            Variant<bool, std::int64_t, double, char32_t, std::string_view>;
        Value value;
    };

    struct Name {
        std::string_view name;
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

        LogicalOr,
        LogicalAnd,
    };

    struct Binary {
        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;
        BinaryOp op;
    };

    enum class ComparisonOp : std::uint8_t {
        Less,
        Greater,
        LessEqual,
        GreaterEqual,
        Equal,
        NotEqual,
    };

    struct Comparison {
        std::vector<std::unique_ptr<Expr>> operands;
        std::vector<ComparisonOp> operators;
    };

    enum class UnaryOp : std::uint8_t {
        Not,
        Neg,
        BitNot,
        Deref,
        AddressOf,
    };
    struct Unary {
        std::unique_ptr<Expr> operand;
        UnaryOp op;
    };

    struct CallArg {
        std::optional<std::string_view> name;
        std::unique_ptr<Expr> value;
    };

    struct Call {
        std::unique_ptr<Expr> value;
        std::vector<CallArg> args;
    };

    struct GetItem {
        std::unique_ptr<Expr> value;
        std::vector<std::unique_ptr<Expr>> args;
    };

    struct GetAttr {
        std::unique_ptr<Expr> value;
        std::string_view name;
    };

    struct Array {
        std::vector<std::unique_ptr<Expr>> items;
    };

    struct As {
        std::unique_ptr<Expr> value;
        std::unique_ptr<Expr> type;
    };

    enum class Specifier : std::uint8_t { Let, Var, Val };

    struct Spec {
        std::unique_ptr<Expr> type;
        Specifier specifier;
    };

    Location location;
    utils::Variant<
        Literal,
        Name,
        Binary,
        Comparison,
        Unary,
        Call,
        GetItem,
        GetAttr,
        Array,
        As,
        Spec>
        value;
};

struct Stmt {
    struct Expr {
        std::unique_ptr<nodes::Expr> expr;
    };

    struct Var {
        std::string_view name;
        std::unique_ptr<nodes::Expr> type;
        std::unique_ptr<nodes::Expr> init;
    };

    struct Block {
        std::vector<std::unique_ptr<Stmt>> stmts;
    };

    struct If {
        std::unique_ptr<nodes::Expr> cond;
        std::unique_ptr<Stmt> then_block;
        std::unique_ptr<Stmt> else_block;
    };

    struct While {
        std::unique_ptr<nodes::Expr> cond;
        std::unique_ptr<Stmt> body;
    };

    struct Return {
        std::unique_ptr<nodes::Expr> value;
    };

    struct Break {};

    struct Continue {};

    struct Assign {
        std::vector<std::unique_ptr<nodes::Expr>> targets;
        std::unique_ptr<nodes::Expr> value;
    };

    enum class AssignOp : std::uint8_t {
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

    struct OpAssign {
        std::unique_ptr<nodes::Expr> target;
        std::unique_ptr<nodes::Expr> value;
        AssignOp op;
    };

    Location location;
    utils::Variant<
        Expr,
        Var,
        Block,
        If,
        While,
        Return,
        Break,
        Continue,
        Assign,
        OpAssign>
        value;
};

struct Use {
    PackageName module_name;
};

struct UseItem {
    Location location;
    std::string_view name;
    std::optional<std::string_view> alias;
};

struct FromUse {
    PackageName module_name;
    std::vector<UseItem> items;
};

struct FuncArg {
    Location location;
    std::string_view name;
    std::unique_ptr<nodes::Expr> type;
};

struct Func {
    bool is_public = false;
    bool is_extern = false;
    std::string_view name;
    std::vector<FuncArg> args;
    std::uint32_t min_pos_args = 0;
    std::uint32_t max_pos_args = 0;
    std::unique_ptr<Expr> return_type;
    std::unique_ptr<Stmt> body;
};

struct StructField {
    Location location;
    std::string_view name;
    std::unique_ptr<Expr> type;
};

struct Struct {
    std::string_view name;
    std::vector<StructField> fields;
};

struct Item {
    Location location;
    utils::Variant<Use, FromUse, Func, Struct> data;
};

struct Module {
    Source* source;
    std::vector<Item> items;
};

std::string to_string(const nodes::Expr& expr);
std::string to_string(const nodes::Stmt& stmt);
std::string to_string(const Func& func);
std::string to_string(const Struct& struct_def);
std::string to_string(const Module& module);
}
