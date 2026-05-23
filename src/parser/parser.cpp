#include "parser.h"

#include <memory>
#include <stdexcept>
#include <string_view>

#include "lexer.h"
#include "nodes.h"
#include "tokens.h"

namespace acu::parser {
namespace {

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Parser {
public:
    Parser(Lexer& lexer, ErrorHandler& err_handler)
        : err_handler_(&err_handler), source_(&lexer.source()) {
        Token token = lexer.next_token();
        tokens_.push_back(token);
        while (token.type != TokenType::EndOfFile) {
            token = lexer.next_token();
            tokens_.push_back(token);
        }
    }

    nodes::Module parse() {
        std::vector<nodes::Item> items;

        while (check(TokenType::Using) || check(TokenType::From)) {
            try {
                if (match(TokenType::Using)) {
                    items.push_back(parse_use_stmt());
                } else if (match(TokenType::From)) {
                    items.push_back(parse_from_use_stmt());
                }
            } catch (const ParseError& e) {
                synchronize_imports();
            }
        }

        while (!at_end()) {
            try {
                if (match(TokenType::NewLine)) {
                    continue;
                }
                if (match(TokenType::Public)) {
                    expect(TokenType::Func, "Expected 'func' after 'public'");
                    items.push_back(parse_function(true, false));
                } else if (match(TokenType::Extern)) {
                    expect(TokenType::Func, "Expected 'func' after 'extern'");
                    items.push_back(parse_function(false, true));
                } else if (match(TokenType::Func)) {
                    items.push_back(parse_function(false, false));
                } else if (match(TokenType::Struct)) {
                    items.push_back(parse_struct());
                } else {
                    Token token = peek();
                    err_handler_->error(
                        *source_, token.location, "Expected function or struct"
                    );
                    synchronize_items();
                }
            } catch (const ParseError& e) {
                synchronize_items();
            }
        }
        return nodes::Module {.source = source_, .items = std::move(items)};
    }

private:
    Source* source_;
    ErrorHandler* err_handler_;
    std::vector<Token> tokens_;
    std::size_t current_ = 0;

    [[nodiscard]] Token peek(int rel_pos = 0) const {
        std::ptrdiff_t pos = static_cast<std::ptrdiff_t>(current_) + rel_pos;
        if (pos < 0) {
            return tokens_[0];
        }
        if (static_cast<std::size_t>(pos) >= tokens_.size()) {
            return tokens_.back();
        }
        return tokens_[pos];
    }

    nodes::Expr::BinaryOp get_binary_op(TokenType type) {
        switch (type) {
            case TokenType::Plus: return nodes::Expr::BinaryOp::Add;
            case TokenType::Minus: return nodes::Expr::BinaryOp::Sub;
            case TokenType::Star: return nodes::Expr::BinaryOp::Mul;
            case TokenType::Slash: return nodes::Expr::BinaryOp::Div;
            case TokenType::Percent: return nodes::Expr::BinaryOp::Mod;
            case TokenType::LessLess: return nodes::Expr::BinaryOp::LShift;
            case TokenType::GreaterGreater:
                return nodes::Expr::BinaryOp::RShift;
            case TokenType::Pipe: return nodes::Expr::BinaryOp::BitOr;
            case TokenType::Amp: return nodes::Expr::BinaryOp::BitAnd;
            case TokenType::Caret: return nodes::Expr::BinaryOp::BitXor;
            case TokenType::And: return nodes::Expr::BinaryOp::LogicalAnd;
            case TokenType::Or: return nodes::Expr::BinaryOp::LogicalOr;
            default: throw ParseError("Invalid binary operation token");
        }
    }

    nodes::Expr::UnaryOp get_unary_op(TokenType type) {
        switch (type) {
            case TokenType::Not: return nodes::Expr::UnaryOp::Not;
            case TokenType::Minus: return nodes::Expr::UnaryOp::Neg;
            case TokenType::Tilde: return nodes::Expr::UnaryOp::BitNot;
            case TokenType::Amp: return nodes::Expr::UnaryOp::AddressOf;
            case TokenType::Star: return nodes::Expr::UnaryOp::Deref;
            default: throw ParseError("Invalid unary operation token");
        }
    }

    nodes::Expr::ComparisonOp get_comparison_op(TokenType type) {
        switch (type) {
            case TokenType::Less: return nodes::Expr::ComparisonOp::Less;
            case TokenType::Greater: return nodes::Expr::ComparisonOp::Greater;
            case TokenType::LessEqual:
                return nodes::Expr::ComparisonOp::LessEqual;
            case TokenType::GreaterEqual:
                return nodes::Expr::ComparisonOp::GreaterEqual;
            case TokenType::EqualEqual: return nodes::Expr::ComparisonOp::Equal;
            case TokenType::NotEqual:
                return nodes::Expr::ComparisonOp::NotEqual;
            default: throw ParseError("Invalid comparison operation token");
        }
    }

    nodes::Stmt::AssignOp get_assign_op(TokenType type) {
        switch (type) {
            case TokenType::PlusEqual: return nodes::Stmt::AssignOp::Add;
            case TokenType::MinusEqual: return nodes::Stmt::AssignOp::Sub;
            case TokenType::StarEqual: return nodes::Stmt::AssignOp::Mul;
            case TokenType::SlashEqual: return nodes::Stmt::AssignOp::Div;
            case TokenType::PercentEqual: return nodes::Stmt::AssignOp::Mod;
            case TokenType::LessLessEqual: return nodes::Stmt::AssignOp::LShift;
            case TokenType::GreaterGreaterEqual:
                return nodes::Stmt::AssignOp::RShift;
            case TokenType::AmpEqual: return nodes::Stmt::AssignOp::BitAnd;
            case TokenType::PipeEqual: return nodes::Stmt::AssignOp::BitOr;
            case TokenType::CaretEqual: return nodes::Stmt::AssignOp::BitXor;
            default: throw ParseError("Invalid assignment operation token");
        }
    }

    [[nodiscard]] bool check(TokenType type) const {
        return peek().type == type;
    }

    [[nodiscard]] bool at_end() const { return check(TokenType::EndOfFile); }

    Token next() {
        if (!at_end()) {
            current_++;
        }
        return peek(-1);
    }

    bool match(TokenType type) {
        if (check(type)) {
            next();
            return true;
        }
        return false;
    }

    Token expect(TokenType type, const std::string& message) {
        if (check(type)) {
            return next();
        }
        Token token = peek();
        err_handler_->error(*source_, token.location, message);
        throw ParseError(message);
    }

    void synchronize_imports() {
        while (!at_end()) {
            TokenType type = peek().type;
            if (type == TokenType::Using || type == TokenType::From ||
                type == TokenType::Func || type == TokenType::Struct) {
                return;
            }
            next();
        }
    }

    void synchronize_items() {
        while (!at_end()) {
            TokenType type = peek().type;
            if (type == TokenType::Func || type == TokenType::Struct ||
                type == TokenType::Using || type == TokenType::From) {
                return;
            }
            next();
        }
    }

    std::unique_ptr<nodes::Expr> parse_type() { return parse_spec(); }

    nodes::Item parse_use_stmt() {
        Location location = peek(-1).location;
        PackageName module_name = parse_module_name();
        expect(TokenType::NewLine, "Expected newline after import statement");

        return {
            .location = location, .data = nodes::Use {std::move(module_name)}
        };
    }

    PackageName parse_module_name() {
        PackageName name;
        Token module_token =
            expect(TokenType::Identifier, "Expected module name after 'using'");
        name.push_back(module_token.value.get<std::string_view>());

        while (match(TokenType::Dot)) {
            Token token = expect(
                TokenType::Identifier, "Expected module name after 'using'"
            );
            name.push_back(token.value.get<std::string_view>());
        }
        return name;
    }

    nodes::Item parse_from_use_stmt() {
        Location location = peek(-1).location;
        PackageName module_name = parse_module_name();
        expect(TokenType::Using, "Expected 'using' after module name");

        std::vector<nodes::UseItem> items;
        bool need_rparen = match(TokenType::LParen);
        while (true) {
            Token name_token = expect(
                TokenType::Identifier, "Expected identifier in import list"
            );
            std::string_view name = name_token.value.get<std::string_view>();
            std::optional<std::string_view> alias;

            if (match(TokenType::As)) {
                Token alias_token = expect(
                    TokenType::Identifier, "Expected alias name after 'as'"
                );
                alias = alias_token.value.get<std::string_view>();
            }

            items.emplace_back(
                nodes::UseItem {
                    .location = name_token.location,
                    .name = name,
                    .alias = alias
                }
            );

            if (!match(TokenType::Comma)) {
                break;
            }
        }
        if (need_rparen) {
            expect(TokenType::RParen, "Expected ')' after import list");
        }
        expect(TokenType::NewLine, "Expected newline after import statement");
        return {
            .location = location,
            .data = nodes::FromUse {
                .module_name = std::move(module_name), .items = std::move(items)
            }
        };
    }

    nodes::Item parse_function(bool is_public, bool is_extern) {
        Token name = expect(TokenType::Identifier, "expected function name");
        expect(TokenType::LParen, "Expected '(' after function name");
        std::vector<nodes::FuncArg> args;
        std::optional<std::uint32_t> min_pos_args;
        std::optional<std::uint32_t> max_pos_args;
        while (!match(TokenType::RParen)) {
            try {
                if (match(TokenType::Slash)) {
                    if (min_pos_args.has_value()) {
                        err_handler_->error(
                            *source_,
                            peek(-1).location,
                            "разедлитель позиционных параметров уже указан"
                        );
                    }
                    min_pos_args = static_cast<std::uint32_t>(args.size());
                } else if (match(TokenType::Star)) {
                    if (max_pos_args.has_value()) {
                        err_handler_->error(
                            *source_,
                            peek(-1).location,
                            "разедлитель параметров со значением уже указан"
                        );
                    }
                    max_pos_args = static_cast<std::uint32_t>(args.size());
                } else {
                    Token param = expect(
                        TokenType::Identifier, "Expected parameter name"
                    );
                    expect(
                        TokenType::Colon, "Expected ':' after parameter name"
                    );
                    std::unique_ptr<nodes::Expr> type = parse_type();
                    args.emplace_back(
                        nodes::FuncArg {
                            .location = param.location,
                            .name =
                                std::get<std::string_view>(param.value.value),
                            .type = std::move(type)
                        }
                    );
                }
            } catch (const ParseError& e) {
                if (synchronize_func_arg()) {
                    expect(TokenType::RParen, "Expected ')' after parameters");
                    break;
                }
            }
            if (!match(TokenType::Comma)) {
                expect(TokenType::RParen, "Expected ')' after parameters");
                break;
            }
        }

        std::unique_ptr<nodes::Expr> return_type = nullptr;
        std::unique_ptr<nodes::Stmt> body = nullptr;

        if (is_extern) {
            if (!match(TokenType::NewLine)) {
                return_type = parse_type();
                expect(
                    TokenType::NewLine,
                    "Expected newline after extern function declaration"
                );
            }
        } else {
            if (!match(TokenType::Colon)) {
                return_type = parse_type();
                expect(TokenType::Colon, "Expected ':' before function body");
            }
            body = parse_body();
        }
        std::uint32_t args_size = args.size();
        return {
            .location = name.location,
            .data = nodes::Func {
                .is_public = is_public,
                .is_extern = is_extern,
                .name = name.value.get<std::string_view>(),
                .args = std::move(args),
                .min_pos_args = min_pos_args.value_or(0),
                .max_pos_args = max_pos_args.value_or(args_size),
                .return_type = std::move(return_type),
                .body = std::move(body)
            }
        };
    }

    bool synchronize_func_arg() {
        while (!at_end()) {
            TokenType type = peek().type;
            if (type == TokenType::Comma) {
                return false;
            }
            if (type == TokenType::RParen || type == TokenType::NewLine ||
                type == TokenType::Colon) {
                return true;
            }
            next();
        }
        return true;
    }

    bool synchronize_struct_field() { return synchronize_func_arg(); }

    nodes::Item parse_struct() {
        Token name = expect(TokenType::Identifier, "expected struct name");
        std::vector<nodes::StructField> fields;
        expect(TokenType::LParen, "Expected '(' after struct name");
        while (!match(TokenType::RParen) && !at_end()) {
            try {
                fields.push_back(parse_struct_field());
            } catch (const ParseError& e) {
                if (synchronize_struct_field()) {
                    expect(TokenType::RParen, "Expected ')' after fields");
                    break;
                }
            }
            if (!match(TokenType::Comma)) {
                expect(TokenType::RParen, "Expected ')' after fields");
                break;
            }
        }
        expect(TokenType::NewLine, "Expected new line after struct");
        return {
            .location = name.location,
            .data = nodes::Struct {
                .name = name.value.get<std::string_view>(),
                .fields = std::move(fields)
            }
        };
    }

    nodes::StructField parse_struct_field() {
        Token name = expect(TokenType::Identifier, "expected field name");
        expect(TokenType::Colon, "Expected ':' after field name");
        std::unique_ptr<nodes::Expr> type = parse_type();
        return nodes::StructField {
            .location = name.location,
            .name = name.value.get<std::string_view>(),
            .type = std::move(type)
        };
    }

    std::unique_ptr<nodes::Stmt> parse_body() {
        std::unique_ptr<nodes::Stmt> result;
        if (match(TokenType::NewLine)) {
            Location location =
                expect(
                    TokenType::Indent, "Expected indented block after new line"
                )
                    .location;
            std::vector<std::unique_ptr<nodes::Stmt>> stmts;
            while (!match(TokenType::Dedent) && !at_end()) {
                try {
                    stmts.push_back(parse_stmt());
                } catch (const ParseError& e) {
                    synchronize_stmt();
                }
            }
            result = std::make_unique<nodes::Stmt>(
                location, nodes::Stmt::Block {std::move(stmts)}
            );
        } else {
            result = parse_stmt();
        }
        return result;
    }

    void synchronize_stmt() {
        while (!at_end()) {
            TokenType type = peek().type;
            if (type == TokenType::NewLine) {
                next();
                return;
            }
            switch (type) {
                case TokenType::Let:
                case TokenType::While:
                case TokenType::If:
                case TokenType::Else:
                case TokenType::Dedent:
                case TokenType::Break:
                case TokenType::Continue:
                case TokenType::Return:
                case TokenType::Using:
                case TokenType::From: return;
                default: next(); break;
            }
        }
    }

    std::unique_ptr<nodes::Stmt> parse_stmt() {
        if (match(TokenType::Let)) {
            return parse_let_stmt();
        }
        if (match(TokenType::If)) {
            return parse_if_stmt();
        }
        if (match(TokenType::While)) {
            return parse_while_stmt();
        }
        if (match(TokenType::Return)) {
            return parse_return_stmt();
        }
        if (match(TokenType::Break)) {
            expect(TokenType::NewLine, "Expected new line after break");
            return std::make_unique<nodes::Stmt>(
                peek(-2).location, nodes::Stmt::Break {}
            );
        }
        if (match(TokenType::Continue)) {
            expect(TokenType::NewLine, "Expected new line after continue");
            return std::make_unique<nodes::Stmt>(
                peek(-2).location, nodes::Stmt::Continue {}
            );
        }
        return parse_assign();
    }

    std::unique_ptr<nodes::Stmt> parse_let_stmt() {
        Token name = expect(TokenType::Identifier, "expected variable name");
        std::unique_ptr<nodes::Expr> type = nullptr;
        if (match(TokenType::Colon)) {
            type = parse_type();
        }
        std::unique_ptr<nodes::Expr> init = nullptr;
        if (match(TokenType::Equal)) {
            init = parse_expr();
        }
        expect(
            TokenType::NewLine, "expected new line after variable declaration"
        );
        return std::make_unique<nodes::Stmt>(
            name.location,
            nodes::Stmt::Var {
                .name = std::get<std::string_view>(name.value.value),
                .type = std::move(type),
                .init = std::move(init)
            }
        );
    }

    std::unique_ptr<nodes::Stmt> parse_if_stmt() {
        Location location = peek(-1).location;
        std::unique_ptr<nodes::Expr> condition = parse_expr();
        expect(TokenType::Colon, "Expected ':' after if condition");
        std::unique_ptr<nodes::Stmt> then_block = parse_body();
        std::unique_ptr<nodes::Stmt> else_block = nullptr;
        if (match(TokenType::Else)) {
            if (match(TokenType::If)) {
                else_block = parse_if_stmt();
            } else {
                expect(TokenType::Colon, "Expected ':' after else");
                else_block = parse_body();
            }
        }
        return std::make_unique<nodes::Stmt>(
            location,
            nodes::Stmt::If {
                .cond = std::move(condition),
                .then_block = std::move(then_block),
                .else_block = std::move(else_block)
            }
        );
    }

    std::unique_ptr<nodes::Stmt> parse_while_stmt() {
        Location location = peek(-1).location;
        std::unique_ptr<nodes::Expr> condition = parse_expr();
        expect(TokenType::Colon, "expected ':' after while condition");
        std::unique_ptr<nodes::Stmt> body = parse_body();
        return std::make_unique<nodes::Stmt>(
            location,
            nodes::Stmt::While {
                .cond = std::move(condition), .body = std::move(body)
            }
        );
    }

    std::unique_ptr<nodes::Stmt> parse_return_stmt() {
        Location location = peek(-1).location;
        std::unique_ptr<nodes::Expr> value = nullptr;
        if (!match(TokenType::NewLine)) {
            value = parse_expr();
            expect(TokenType::NewLine, "expected new line after");
        }
        return std::make_unique<nodes::Stmt>(
            location, nodes::Stmt::Return {std::move(value)}
        );
    }

    std::unique_ptr<nodes::Stmt> parse_assign() {
        if (peek().type == TokenType::Identifier &&
            peek(1).type == TokenType::Colon) {
            auto name = next();
            expect(TokenType::Colon, "");
            auto type = parse_type();
            std::unique_ptr<nodes::Expr> init = nullptr;
            if (match(TokenType::Equal)) {
                init = parse_expr();
            }
            expect(TokenType::NewLine, "");
            return std::make_unique<nodes::Stmt>(
                name.location,
                nodes::Stmt::Var {
                    .name = name.value.get<std::string_view>(),
                    .type = std::move(type),
                    .init = std::move(init)
                }
            );
        }
        std::unique_ptr<nodes::Expr> expr = parse_expr();
        if (peek().type == TokenType::PlusEqual ||
            peek().type == TokenType::MinusEqual ||
            peek().type == TokenType::StarEqual ||
            peek().type == TokenType::SlashEqual ||
            peek().type == TokenType::PercentEqual ||
            peek().type == TokenType::LessLessEqual ||
            peek().type == TokenType::GreaterGreaterEqual ||
            peek().type == TokenType::AmpEqual ||
            peek().type == TokenType::PipeEqual ||
            peek().type == TokenType::CaretEqual) {
            Token op = next();
            std::unique_ptr<nodes::Expr> value = parse_expr();
            expect(TokenType::NewLine, "Expected new line");
            return std::make_unique<nodes::Stmt>(
                op.location,
                nodes::Stmt::OpAssign {
                    .target = std::move(expr),
                    .value = std::move(value),
                    .op = get_assign_op(op.type)
                }
            );
        }

        std::vector<std::unique_ptr<nodes::Expr>> targets;
        Location location = expr->location;
        targets.push_back(std::move(expr));

        while (match(TokenType::Equal)) {
            location = peek(-1).location;
            targets.push_back(parse_expr());
        }
        expect(TokenType::NewLine, "Expected new line");
        std::unique_ptr<nodes::Expr> expr_val = std::move(targets.back());
        targets.pop_back();
        if (targets.empty()) {
            auto location = expr_val->location;
            return std::make_unique<nodes::Stmt>(
                location, nodes::Stmt::Expr {std::move(expr_val)}
            );
        }
        return std::make_unique<nodes::Stmt>(
            location,
            nodes::Stmt::Assign {
                .targets = std::move(targets), .value = std::move(expr_val)
            }
        );
    }

    std::optional<nodes::Expr::Specifier> get_spec(TokenType type) {
        switch (type) {
            case TokenType::Let: return nodes::Expr::Specifier::Let;
            case TokenType::Var: return nodes::Expr::Specifier::Var;
            case TokenType::Val: return nodes::Expr::Specifier::Val;
            default: return std::nullopt;
        }
    }

    bool has_expr() {
        switch(peek().type) {
            case TokenType::Identifier:
            case TokenType::Minus:
            case TokenType::Amp:
            case TokenType::Tilde:
            case TokenType::Not:
            case TokenType::True:
            case TokenType::False:
            case TokenType::Integer:
            case TokenType::Float:
            case TokenType::Char:
            case TokenType::String:
            case TokenType::LParen:
            case TokenType::LBracket:
                return true;
            default: return false;
        }
    }

    std::unique_ptr<nodes::Expr> parse_spec() {
        return get_spec(peek().type)
            .transform([&](auto spec) {
                auto token = next();
                auto expr = has_expr() ? parse_expr() : nullptr;
                return std::make_unique<nodes::Expr>(
                    token.location,
                    nodes::Expr::Spec {
                        .type = std::move(expr),
                        .specifier = nodes::Expr::Specifier::Let
                    }
                );
            })
            .or_else([&] { return std::optional {has_expr() ? parse_expr() : nullptr}; })
            .value();
    }

    enum class Precedence : std::uint8_t {
        Lowest,
        LogicalOr,
        LogicalAnd,
        Comparison,
        BitOr,
        BitXor,
        BitAnd,
        Shift,
        Sum,
        Product,
        Prefix,
        Call,
    };

    [[nodiscard]] Precedence get_precedence(TokenType type) const {
        switch (type) {
            case TokenType::Or: return Precedence::LogicalOr;
            case TokenType::And: return Precedence::LogicalAnd;
            case TokenType::Less:
            case TokenType::Greater:
            case TokenType::LessEqual:
            case TokenType::GreaterEqual:
            case TokenType::EqualEqual:
            case TokenType::NotEqual: return Precedence::Comparison;
            case TokenType::Pipe: return Precedence::BitOr;
            case TokenType::Caret: return Precedence::BitXor;
            case TokenType::Amp: return Precedence::BitAnd;
            case TokenType::LessLess:
            case TokenType::GreaterGreater: return Precedence::Shift;
            case TokenType::Plus:
            case TokenType::Minus: return Precedence::Sum;
            case TokenType::Star:
            case TokenType::Slash:
            case TokenType::Percent: return Precedence::Product;
            case TokenType::LParen:
            case TokenType::LBracket:
            case TokenType::Dot:
            case TokenType::As: return Precedence::Call;
            default: return Precedence::Lowest;
        }
    }

    std::unique_ptr<nodes::Expr> parse_expr(
        Precedence precedence = Precedence::Lowest
    ) {
        Token token = next();
        std::unique_ptr<nodes::Expr> left = parse_prefix(token);

        while (precedence < get_precedence(peek().type)) {
            Token op_token = next();
            left = parse_infix(std::move(left), op_token);
        }

        return left;
    }

    std::unique_ptr<nodes::Expr> parse_prefix(Token token) {
        switch (token.type) {
            case TokenType::Integer:
            case TokenType::Float:
            case TokenType::String:
            case TokenType::Char:
                return std::make_unique<nodes::Expr>(
                    token.location, nodes::Expr::Literal {token.value}
                );
            case TokenType::True:
                return std::make_unique<nodes::Expr>(
                    token.location, nodes::Expr::Literal {true}
                );
            case TokenType::False:
                return std::make_unique<nodes::Expr>(
                    token.location, nodes::Expr::Literal {false}
                );
            case TokenType::Identifier:
                return std::make_unique<nodes::Expr>(
                    token.location,
                    nodes::Expr::Name {token.value.get<std::string_view>()}
                );
            case TokenType::LParen: {
                auto result = parse_expr();
                expect(
                    TokenType::RParen, "expected ')' after expression in '()'"
                );
                return result;
            }
            case TokenType::LBracket: {
                std::vector<std::unique_ptr<nodes::Expr>> items;
                Location location = token.location;
                while (!match(TokenType::RBracket)) {
                    items.push_back(parse_expr());
                    if (!match(TokenType::Comma)) {
                        expect(
                            TokenType::RBracket,
                            "expected ']' after array items"
                        );
                        break;
                    }
                }
                return std::make_unique<nodes::Expr>(
                    location, nodes::Expr::Array {std::move(items)}
                );
            }
            case TokenType::Minus:
            case TokenType::Tilde:
            case TokenType::Not:
            case TokenType::Amp: {
                auto operand = parse_expr(Precedence::Prefix);
                return std::make_unique<nodes::Expr>(
                    token.location,
                    nodes::Expr::Unary {
                        .operand = std::move(operand),
                        .op = get_unary_op(token.type)
                    }
                );
            }
            default:
                err_handler_->error(
                    *source_, token.location, "expected expression"
                );
                throw ParseError("expected expression");
        }
    }

    std::unique_ptr<nodes::Expr> parse_infix(
        std::unique_ptr<nodes::Expr> left, Token op_token
    ) {
        Precedence precedence = get_precedence(op_token.type);

        switch (op_token.type) {
            case TokenType::Plus:
            case TokenType::Minus:
            case TokenType::Star:
            case TokenType::Slash:
            case TokenType::Percent:
            case TokenType::LessLess:
            case TokenType::GreaterGreater:
            case TokenType::Pipe:
            case TokenType::Amp:
            case TokenType::Caret:
            case TokenType::And:
            case TokenType::Or: {
                auto right = parse_expr(precedence);
                return std::make_unique<nodes::Expr>(
                    op_token.location,
                    nodes::Expr::Binary {
                        .left = std::move(left),
                        .right = std::move(right),
                        .op = get_binary_op(op_token.type)
                    }
                );
            }
            case TokenType::Less:
            case TokenType::Greater:
            case TokenType::LessEqual:
            case TokenType::GreaterEqual:
            case TokenType::EqualEqual:
            case TokenType::NotEqual: {
                std::vector<std::unique_ptr<nodes::Expr>> operands;
                std::vector<nodes::Expr::ComparisonOp> operators;
                Location location = op_token.location;

                operands.push_back(std::move(left));
                operators.push_back(get_comparison_op(op_token.type));
                operands.push_back(parse_expr(precedence));

                while (true) {
                    Precedence next_prec = get_precedence(peek().type);
                    if (next_prec != Precedence::Comparison) break;

                    Token next_op = next();
                    operators.push_back(get_comparison_op(next_op.type));
                    operands.push_back(parse_expr(precedence));
                }

                return std::make_unique<nodes::Expr>(
                    location,
                    nodes::Expr::Comparison {
                        .operands = std::move(operands),
                        .operators = std::move(operators)
                    }
                );
            }
            case TokenType::LParen:
                return parse_call(std::move(left), op_token.location);
            case TokenType::LBracket:
                return parse_get_item(std::move(left), op_token.location);
            case TokenType::Dot: {
                if (match(TokenType::Star)) {
                    return std::make_unique<nodes::Expr>(
                        op_token.location,
                        nodes::Expr::Unary {
                            .operand = std::move(left),
                            .op = nodes::Expr::UnaryOp::Deref
                        }
                    );
                } else {
                    Token name = expect(
                        TokenType::Identifier,
                        "expected attribute name after '.'"
                    );
                    return std::make_unique<nodes::Expr>(
                        name.location,
                        nodes::Expr::GetAttr {
                            .value = std::move(left),
                            .name = name.value.get<std::string_view>()
                        }
                    );
                }
            }
            case TokenType::As: {
                std::unique_ptr<nodes::Expr> type = parse_type();
                auto location = type->location;
                return std::make_unique<nodes::Expr>(
                    location,
                    nodes::Expr::As {
                        .value = std::move(left), .type = std::move(type)
                    }
                );
            }
            default: return left;
        }
    }

    std::unique_ptr<nodes::Expr> parse_call(
        std::unique_ptr<nodes::Expr> expr, Location location
    ) {
        std::vector<nodes::Expr::CallArg> args;
        while (!match(TokenType::RParen)) {
            if (peek().type == TokenType::Identifier &&
                peek(1).type == TokenType::Equal) {
                args.push_back({
                    .name = next().value.get<std::string_view>(),
                    .value = parse_expr(),
                });
            } else {
                args.push_back({.value = parse_expr()});
            }

            if (!match(TokenType::Comma)) {
                expect(TokenType::RParen, "expected ')' after arguments");
                break;
            }
        }
        return std::make_unique<nodes::Expr>(
            location,
            nodes::Expr::Call {
                .value = std::move(expr), .args = std::move(args)
            }
        );
    }

    std::unique_ptr<nodes::Expr> parse_get_item(
        std::unique_ptr<nodes::Expr> expr, Location location
    ) {
        std::vector<std::unique_ptr<nodes::Expr>> args;
        while (!match(TokenType::RBracket)) {
            args.push_back(parse_expr());
            if (!match(TokenType::Comma)) {
                expect(TokenType::RBracket, "expected ']' after arguments");
                break;
            }
        }
        return std::make_unique<nodes::Expr>(
            location,
            nodes::Expr::GetItem {
                .value = std::move(expr), .args = std::move(args)
            }
        );
    }
};
}

nodes::Module parse(Source& source, ErrorHandler& err_handler) {
    Lexer lexer(source, err_handler);
    Parser parser(lexer, err_handler);
    return parser.parse();
}

}
