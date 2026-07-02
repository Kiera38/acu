#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "source.h"
#include "variant.h"

namespace acu::nodes {
struct Expr {
    using Literal =
        utils::Variant<bool, std::int64_t, double, char32_t, std::string_view>;

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

    [[nodiscard]] bool is_literal() const { return value.is<Literal>(); }
    [[nodiscard]] bool is_name() const { return value.is<Name>(); }
    [[nodiscard]] bool is_binary() const { return value.is<Binary>(); }
    [[nodiscard]] bool is_comparison() const { return value.is<Comparison>(); }
    [[nodiscard]] bool is_unary() const { return value.is<Unary>(); }
    [[nodiscard]] bool is_call() const { return value.is<Call>(); }
    [[nodiscard]] bool is_get_item() const { return value.is<GetItem>(); }
    [[nodiscard]] bool is_get_attr() const { return value.is<GetAttr>(); }
    [[nodiscard]] bool is_array() const { return value.is<Array>(); }
    [[nodiscard]] bool is_as() const { return value.is<As>(); }
    [[nodiscard]] bool is_spec() const { return value.is<Spec>(); }

    [[nodiscard]] const Literal& literal() const {
        return value.get<Literal>();
    }
    [[nodiscard]] const Name& name() const { return value.get<Name>(); }
    [[nodiscard]] const Binary& binary() const { return value.get<Binary>(); }
    [[nodiscard]] const Comparison& comparison() const {
        return value.get<Comparison>();
    }
    [[nodiscard]] const Unary& unary() const { return value.get<Unary>(); }
    [[nodiscard]] const Call& call() const { return value.get<Call>(); }
    [[nodiscard]] const GetItem& get_item() const {
        return value.get<GetItem>();
    }
    [[nodiscard]] const GetAttr& get_attr() const {
        return value.get<GetAttr>();
    }
    [[nodiscard]] const Array& array() const { return value.get<Array>(); }
    [[nodiscard]] const As& as() const { return value.get<As>(); }
    [[nodiscard]] const Spec& spec() const { return value.get<Spec>(); }

    [[nodiscard]] const Literal* as_literal() const {
        return value.get_if<Literal>();
    }
    [[nodiscard]] const Name* as_name() const { return value.get_if<Name>(); }
    [[nodiscard]] const Binary* as_binary() const {
        return value.get_if<Binary>();
    }
    [[nodiscard]] const Comparison* as_comparison() const {
        return value.get_if<Comparison>();
    }
    [[nodiscard]] const Unary* as_unary() const {
        return value.get_if<Unary>();
    }
    [[nodiscard]] const Call* as_call() const { return value.get_if<Call>(); }
    [[nodiscard]] const GetItem* as_get_item() const {
        return value.get_if<GetItem>();
    }
    [[nodiscard]] const GetAttr* as_get_attr() const {
        return value.get_if<GetAttr>();
    }
    [[nodiscard]] const Array* as_array() const {
        return value.get_if<Array>();
    }
    [[nodiscard]] const As* as_as() const { return value.get_if<As>(); }
    [[nodiscard]] const Spec* as_spec() const { return value.get_if<Spec>(); }

    template <typename... Args>
    auto visit(Args&&... args) const {
        return value.visit(std::forward<Args>(args)...);
    }
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

    [[nodiscard]] bool is_expr() const { return value.is<Expr>(); }
    [[nodiscard]] bool is_var() const { return value.is<Var>(); }
    [[nodiscard]] bool is_block() const { return value.is<Block>(); }
    [[nodiscard]] bool is_if() const { return value.is<If>(); }
    [[nodiscard]] bool is_while() const { return value.is<While>(); }
    [[nodiscard]] bool is_return() const { return value.is<Return>(); }
    [[nodiscard]] bool is_break() const { return value.is<Break>(); }
    [[nodiscard]] bool is_continue() const { return value.is<Continue>(); }
    [[nodiscard]] bool is_assign() const { return value.is<Assign>(); }
    [[nodiscard]] bool is_op_assign() const { return value.is<OpAssign>(); }
    [[nodiscard]] const Expr& expr() const { return value.get<Expr>(); }
    [[nodiscard]] const Var& var() const { return value.get<Var>(); }
    [[nodiscard]] const Block& block() const { return value.get<Block>(); }
    [[nodiscard]] const If& if_() const { return value.get<If>(); }
    [[nodiscard]] const While& while_() const { return value.get<While>(); }
    [[nodiscard]] const Return& return_() const { return value.get<Return>(); }
    [[nodiscard]] const Break& break_() const { return value.get<Break>(); }
    [[nodiscard]] const Continue& continue_() const {
        return value.get<Continue>();
    }
    [[nodiscard]] const Assign& assign() const { return value.get<Assign>(); }
    [[nodiscard]] const OpAssign& op_assign() const {
        return value.get<OpAssign>();
    }
    [[nodiscard]] const Expr* as_expr() const { return value.get_if<Expr>(); }
    [[nodiscard]] const Var* as_var() const { return value.get_if<Var>(); }
    [[nodiscard]] const Block* as_block() const {
        return value.get_if<Block>();
    }
    [[nodiscard]] const If* as_if() const { return value.get_if<If>(); }
    [[nodiscard]] const While* as_while() const {
        return value.get_if<While>();
    }
    [[nodiscard]] const Return* as_return() const {
        return value.get_if<Return>();
    }
    [[nodiscard]] const Break* as_break() const {
        return value.get_if<Break>();
    }
    [[nodiscard]] const Continue* as_continue() const {
        return value.get_if<Continue>();
    }
    [[nodiscard]] const Assign* as_assign() const {
        return value.get_if<Assign>();
    }
    [[nodiscard]] const OpAssign* as_op_assign() const {
        return value.get_if<OpAssign>();
    }
    template <typename... Args>
    auto visit(Args&&... args) const {
        return value.visit(std::forward<Args>(args)...);
    }
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
    std::unique_ptr<Expr> type;
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

    [[nodiscard]] bool is_using() const { return data.is<Use>(); }
    [[nodiscard]] bool is_from_using() const { return data.is<FromUse>(); }
    [[nodiscard]] bool is_func() const { return data.is<Func>(); }
    [[nodiscard]] bool is_struct() const { return data.is<Struct>(); }

    [[nodiscard]] const Use& using_() const { return data.get<Use>(); }
    [[nodiscard]] const FromUse& from_using() const {
        return data.get<FromUse>();
    }
    [[nodiscard]] const Func& func() const { return data.get<Func>(); }
    [[nodiscard]] const Struct& struct_() const { return data.get<Struct>(); }

    [[nodiscard]] const Use* get_using() const { return data.get_if<Use>(); }
    [[nodiscard]] const FromUse* get_from_using() const {
        return data.get_if<FromUse>();
    }
    [[nodiscard]] const Func& get_func() const { return data.get<Func>(); }
    [[nodiscard]] const Struct& get_struct() const {
        return data.get<Struct>();
    }
    template <typename... Args>
    auto visit(Args&&... args) const {
        return data.visit(std::forward<Args>(args)...);
    }
};

struct Module {
    Source* source;
    std::vector<Item> items;
};

std::string to_string(const Expr& expr);
std::string to_string(const Stmt& stmt);
std::string to_string(const Item& item);
std::string to_string(const Module& module);
}
