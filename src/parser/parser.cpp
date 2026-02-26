#include "parser.h"

#include "lexer.h"
#include "nodes.h"

namespace acu::parser {
namespace {
class Parser {
public:
    Parser(Lexer& lexer) : lexer_(&lexer) {}

    nodes::Module parse() {
        std::vector<std::unique_ptr<nodes::Stmt>> imports;
        std::vector<nodes::Func> funcs;
        std::vector<nodes::Struct> structs;

        // Сначала парсим импорты (они должны быть в начале)
        while (check(TokenType::Using) || check(TokenType::From)) {
            if (match(TokenType::Using)) {
                imports.push_back(parse_use_stmt());
            } else if (match(TokenType::From)) {
                imports.push_back(parse_from_use_stmt());
            }
        }

        // Затем парсим функции и структуры
        while (!at_end()) {
            if (match(TokenType::Func)) {
                funcs.push_back(parse_function());
            } else if (match(TokenType::Struct)) {
                structs.push_back(parse_struct());
            } else {
                Token token = peek();
                throw std::runtime_error("Expected function or struct");
            }
        }
        return nodes::Module {
            .imports = std::move(imports),
            .funcs = std::move(funcs),
            .structs = std::move(structs)
        };
    }

private:
    Lexer* lexer_;
    std::vector<Token> tokens_;
    std::size_t current_ = 0;

    Token peek(int rel_pos = 0) {
        std::size_t pos = current_ + rel_pos;
        if (pos >= tokens_.size()) {
            std::size_t count = pos - tokens_.size() + 1;
            for (std::size_t i = 0; i < count; ++i) {
                tokens_.push_back(lexer_->next_token());
            }
        }
        return tokens_[pos];
    }

    // Helper functions to convert TokenType to operation enums using switch
    // statements
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
            default: throw std::runtime_error("Invalid binary operation token");
        }
    }

    nodes::Expr::UnaryOp get_unary_op(TokenType type) {
        switch (type) {
            case TokenType::Not: return nodes::Expr::UnaryOp::Not;
            case TokenType::Minus: return nodes::Expr::UnaryOp::Neg;
            case TokenType::Tilde: return nodes::Expr::UnaryOp::BitNot;
            case TokenType::Amp: return nodes::Expr::UnaryOp::AddressOf;
            case TokenType::Star: return nodes::Expr::UnaryOp::Deref;
            default: throw std::runtime_error("Invalid unary operation token");
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
            default:
                throw std::runtime_error("Invalid comparison operation token");
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
            default:
                throw std::runtime_error("Invalid assignment operation token");
        }
    }

    bool check(TokenType type) { return peek().type == type; }

    bool at_end() { return check(TokenType::EndOfFile); }

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
        throw std::runtime_error(message);
    }

    std::unique_ptr<nodes::Expr> parse_type() { return parse_expr(); }

    std::unique_ptr<nodes::Stmt> parse_use_stmt() {
        Location location = peek(-1).location;  // 'using' token location
        std::vector<std::string_view> module_name = parse_module_name();
        expect(TokenType::NewLine, "Expected newline after import statement");

        return std::make_unique<nodes::Stmt>(
            location, nodes::Stmt::Use {std::move(module_name)}
        );
    }

    std::vector<std::string_view> parse_module_name() {
        std::vector<std::string_view> name;
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

    std::unique_ptr<nodes::Stmt> parse_from_use_stmt() {
        Location location = peek(-1).location;  // 'from' token location
        std::vector<std::string_view> module_name = parse_module_name();
        expect(TokenType::Using, "Expected 'using' after module name");

        std::vector<nodes::Stmt::UseItem> items;
        bool need_rparen = match(TokenType::LParen);
        while (true) {
            Token name_token = expect(
                TokenType::Identifier, "Expected identifier in import list"
            );
            std::string_view name = name_token.value.get<std::string_view>();
            std::optional<std::string_view> alias;

            // Проверяем наличие 'as' для альяса
            if (match(TokenType::As)) {
                Token alias_token = expect(
                    TokenType::Identifier, "Expected alias name after 'as'"
                );
                alias = alias_token.value.get<std::string_view>();
            }

            items.emplace_back(
                nodes::Stmt::UseItem {
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
        return std::make_unique<nodes::Stmt>(nodes::Stmt {
            .location = location,
            .value = nodes::Stmt::FromUse {
                .module_name = std::move(module_name), .items = std::move(items)
            }
        });
    }

    nodes::Func parse_function() {
        Token name = expect(TokenType::Identifier, "expected function name");
        expect(TokenType::LParen, "Expected '(' after function name");
        std::vector<nodes::FuncArg> args;
        while (!match(TokenType::RParen)) {
            Token param =
                expect(TokenType::Identifier, "Expected parameter name");
            expect(TokenType::Colon, "Expected ':' after parameter name");
            std::unique_ptr<nodes::Expr> type = parse_type();
            args.emplace_back(
                nodes::FuncArg {
                    .location = param.location,
                    .name = std::get<std::string_view>(param.value.value),
                    .type = std::move(type)
                }
            );
            if (!match(TokenType::Comma)) {
                expect(TokenType::RParen, "Expected ')' after parameters");
                break;
            }
        }

        std::unique_ptr<nodes::Expr> return_type = nullptr;
        if (!match(TokenType::Colon)) {
            return_type = parse_type();
            expect(TokenType::Colon, "Expected ':' before function body");
        }

        return nodes::Func {
            .location = name.location,
            .name = std::get<std::string_view>(name.value.value),
            .args = std::move(args),
            .return_type = std::move(return_type),
            .body = parse_body()
        };
    }

    nodes::Struct parse_struct() {
        Token name = expect(TokenType::Identifier, "expected struct name");
        expect(TokenType::Colon, "Expected ':' before struct body");
        std::vector<nodes::StructField> fields;
        if (match(TokenType::NewLine)) {
            expect(TokenType::Indent, "Expected indented block after new line");
            while (!match(TokenType::Dedent)) {
                fields.push_back(parse_struct_field());
            }
        } else {
            fields.push_back(parse_struct_field());
        }
        return nodes::Struct {
            .name = std::get<std::string_view>(name.value.value),
            .fields = std::move(fields)
        };
    }

    nodes::StructField parse_struct_field() {
        expect(TokenType::Var, "Field must starts with 'var'");
        Token name = expect(TokenType::Identifier, "expected field name");
        expect(TokenType::Colon, "Expected ':' after field name");
        std::unique_ptr<nodes::Expr> type = parse_type();
        expect(TokenType::NewLine, "Expected new line after field");
        return nodes::StructField {
            .location = name.location,
            .name = std::get<std::string_view>(name.value.value),
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
                stmts.push_back(parse_stmt());
            }
            result = std::make_unique<nodes::Stmt>(
                location, nodes::Stmt::Block {std::move(stmts)}
            );
        } else {
            result = parse_stmt();
        }
        return result;
    }

    std::unique_ptr<nodes::Stmt> parse_stmt() {
        if (match(TokenType::Var)) {
            return parse_var_decl();
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

    std::unique_ptr<nodes::Stmt> parse_var_decl() {
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
                else_block = parse_if_stmt();  // Вложенный if
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

    std::unique_ptr<nodes::Expr> parse_expr() { return parse_logical_or(); }

    std::unique_ptr<nodes::Expr> parse_logical_or() {
        std::unique_ptr<nodes::Expr> expr = parse_logical_and();
        while (match(TokenType::Or)) {
            Location location = peek(-1).location;
            std::unique_ptr<nodes::Expr> right = parse_logical_and();
            expr = std::make_unique<nodes::Expr>(
                location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = nodes::Expr::BinaryOp::LogicalOr
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_logical_and() {
        std::unique_ptr<nodes::Expr> expr = parse_not();
        while (match(TokenType::And)) {
            Location location = peek(-1).location;
            std::unique_ptr<nodes::Expr> right = parse_not();
            expr = std::make_unique<nodes::Expr>(
                location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = nodes::Expr::BinaryOp::LogicalAnd
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_not() {
        if (match(TokenType::Not)) {
            Location location = peek(-1).location;
            std::unique_ptr<nodes::Expr> operand = parse_not();
            return std::make_unique<nodes::Expr>(
                location,
                nodes::Expr::Unary {
                    .operand = std::move(operand),
                    .op = nodes::Expr::UnaryOp::Not
                }
            );
        }
        return parse_comparison();
    }

    std::unique_ptr<nodes::Expr> parse_comparison() {
        std::unique_ptr<nodes::Expr> expr = parse_bit_or();
        std::vector<std::unique_ptr<nodes::Expr>> operands;
        Location location = expr->location;
        operands.push_back(std::move(expr));
        std::vector<nodes::Expr::ComparisonOp> operators;

        while (peek().type == TokenType::Less ||
               peek().type == TokenType::Greater ||
               peek().type == TokenType::LessEqual ||
               peek().type == TokenType::GreaterEqual ||
               peek().type == TokenType::EqualEqual ||
               peek().type == TokenType::NotEqual) {
            Token op = next();
            location = op.location;
            operators.push_back(get_comparison_op(op.type));
            operands.push_back(parse_bit_or());
        }
        if (operators.empty()) {
            return std::move(operands[0]);
        }
        return std::make_unique<nodes::Expr>(
            location,
            nodes::Expr::Comparison {
                .operands = std::move(operands),
                .operators = std::move(operators)
            }
        );
    }

    std::unique_ptr<nodes::Expr> parse_bit_or() {
        std::unique_ptr<nodes::Expr> expr = parse_bit_xor();
        while (match(TokenType::Pipe)) {
            Location location = peek(-1).location;
            std::unique_ptr<nodes::Expr> right = parse_bit_xor();
            expr = std::make_unique<nodes::Expr>(
                location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = nodes::Expr::BinaryOp::BitOr
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_bit_xor() {
        std::unique_ptr<nodes::Expr> expr = parse_bit_and();
        while (match(TokenType::Caret)) {
            Location location = peek(-1).location;
            std::unique_ptr<nodes::Expr> right = parse_bit_and();
            expr = std::make_unique<nodes::Expr>(
                location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = nodes::Expr::BinaryOp::BitXor
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_bit_and() {
        std::unique_ptr<nodes::Expr> expr = parse_shift();
        while (match(TokenType::Amp)) {
            Location location = peek(-1).location;
            std::unique_ptr<nodes::Expr> right = parse_shift();
            expr = std::make_unique<nodes::Expr>(
                location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = nodes::Expr::BinaryOp::BitAnd
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_shift() {
        std::unique_ptr<nodes::Expr> expr = parse_addition();
        while (peek().type == TokenType::LessLess ||
               peek().type == TokenType::GreaterGreater) {
            Token op = next();
            std::unique_ptr<nodes::Expr> right = parse_addition();
            expr = std::make_unique<nodes::Expr>(
                op.location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = get_binary_op(op.type)
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_addition() {
        std::unique_ptr<nodes::Expr> expr = parse_multiplication();
        while (peek().type == TokenType::Plus ||
               peek().type == TokenType::Minus) {
            Token op = next();
            std::unique_ptr<nodes::Expr> right = parse_multiplication();
            expr = std::make_unique<nodes::Expr>(
                op.location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = get_binary_op(op.type)
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_multiplication() {
        std::unique_ptr<nodes::Expr> expr = parse_unary();
        while (peek().type == TokenType::Star ||
               peek().type == TokenType::Slash ||
               peek().type == TokenType::Percent) {
            Token op = next();
            std::unique_ptr<nodes::Expr> right = parse_unary();
            expr = std::make_unique<nodes::Expr>(
                op.location,
                nodes::Expr::Binary {
                    .left = std::move(expr),
                    .right = std::move(right),
                    .op = get_binary_op(op.type)
                }
            );
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_unary() {
        if (peek().type == TokenType::Minus ||
            peek().type == TokenType::Tilde) {
            Token op = next();
            std::unique_ptr<nodes::Expr> operand = parse_unary();
            return std::make_unique<nodes::Expr>(
                op.location,
                nodes::Expr::Unary {
                    .operand = std::move(operand), .op = get_unary_op(op.type)
                }
            );
        }
        return parse_postfix();
    }

    std::unique_ptr<nodes::Expr> parse_postfix() {
        std::unique_ptr<nodes::Expr> expr = parse_primary();
        while (true) {
            if (peek().type == TokenType::LParen) {
                expr = parse_call(std::move(expr));
            } else if (peek().type == TokenType::LBracket) {
                expr = parse_get_item(std::move(expr));
            } else if ((peek().type == TokenType::Amp ||
                        peek().type == TokenType::Star) &&
                       check_next_end()) {
                Token op = next();
                expr = std::make_unique<nodes::Expr>(
                    op.location,
                    nodes::Expr::Unary {
                        .operand = std::move(expr), .op = get_unary_op(op.type)
                    }
                );
            } else if (peek().type == TokenType::Dot) {
                next();
                Token name = expect(
                    TokenType::Identifier, "expected attribute name after '.'"
                );
                expr = std::make_unique<nodes::Expr>(
                    name.location,
                    nodes::Expr::GetAttr {
                        .value = std::move(expr),
                        .name = name.value.get<std::string_view>()
                    }
                );
            } else if (peek().type == TokenType::As) {
                next();
                std::unique_ptr<nodes::Expr> type = parse_type();
                auto location = type->location;
                expr = std::make_unique<nodes::Expr>(
                    location,
                    nodes::Expr::As {
                        .value = std::move(expr), .type = std::move(type)
                    }
                );
            } else {
                break;
            }
        }
        return expr;
    }

    std::unique_ptr<nodes::Expr> parse_call(std::unique_ptr<nodes::Expr> expr) {
        Location location = next().location;
        std::vector<std::unique_ptr<nodes::Expr>> args;
        while (!match(TokenType::RParen)) {
            args.push_back(parse_expr());
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
        std::unique_ptr<nodes::Expr> expr
    ) {
        Location location = next().location;
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

    bool check_next_end() {
        // todo: это не нормально
        TokenType next_type = peek(1).type;
        return next_type == TokenType::LParen ||
               next_type == TokenType::RParen ||
               next_type == TokenType::LBracket ||
               next_type == TokenType::RBracket ||
               next_type == TokenType::NewLine ||
               next_type == TokenType::Colon || next_type == TokenType::Comma ||
               next_type == TokenType::Amp ||
               next_type == TokenType::AmpEqual ||
               next_type == TokenType::And || next_type == TokenType::Caret ||
               next_type == TokenType::CaretEqual ||
               next_type == TokenType::Dot || next_type == TokenType::Equal ||
               next_type == TokenType::EqualEqual ||
               next_type == TokenType::Greater ||
               next_type == TokenType::GreaterEqual ||
               next_type == TokenType::GreaterGreater ||
               next_type == TokenType::GreaterGreaterEqual ||
               next_type == TokenType::Less ||
               next_type == TokenType::LessEqual ||
               next_type == TokenType::LessLess ||
               next_type == TokenType::LessLessEqual ||
               next_type == TokenType::LParen ||
               next_type == TokenType::Minus ||
               next_type == TokenType::MinusEqual ||
               next_type == TokenType::NotEqual || next_type == TokenType::Or ||
               next_type == TokenType::Percent ||
               next_type == TokenType::PercentEqual ||
               next_type == TokenType::Pipe ||
               next_type == TokenType::PipeEqual ||
               next_type == TokenType::Plus ||
               next_type == TokenType::PlusEqual ||
               next_type == TokenType::Semicolon ||
               next_type == TokenType::Slash ||
               next_type == TokenType::SlashEqual ||
               next_type == TokenType::Star ||
               next_type == TokenType::StarEqual;
    }

    std::unique_ptr<nodes::Expr> parse_primary() {
        if (peek().type == TokenType::Integer ||
            peek().type == TokenType::Float ||
            peek().type == TokenType::String ||
            peek().type == TokenType::Char) {
            Token token = next();
            return std::make_unique<nodes::Expr>(
                token.location, nodes::Expr::Literal {token.value}
            );
        }
        if (match(TokenType::True)) {
            Token token = peek(-1);
            return std::make_unique<nodes::Expr>(
                token.location, nodes::Expr::Literal {true}
            );
        }
        if (match(TokenType::False)) {
            Token token = peek(-1);
            return std::make_unique<nodes::Expr>(
                token.location, nodes::Expr::Literal {false}
            );
        }

        if (match(TokenType::Identifier)) {
            Token token = peek(-1);
            return std::make_unique<nodes::Expr>(
                token.location,
                nodes::Expr::Name {token.value.get<std::string_view>()}
            );
        }

        if (match(TokenType::LParen)) {
            std::unique_ptr<nodes::Expr> result = parse_expr();
            expect(TokenType::RParen, "expected ')' after expression in '()'");
            return result;
        }

        if (match(TokenType::LBracket)) {
            std::vector<std::unique_ptr<nodes::Expr>> items;
            Location location = peek(-1).location;
            while (!match(TokenType::RBracket)) {
                items.push_back(parse_expr());
                if (!match(TokenType::Comma)) {
                    expect(
                        TokenType::RBracket, "expected ']' after array items"
                    );
                    break;
                }
            }
            return std::make_unique<nodes::Expr>(
                location, nodes::Expr::Array {std::move(items)}
            );
        }

        throw std::runtime_error("expected expression");
    }
};
}

nodes::Module parse(Source& source) {
    Lexer lexer(source);
    Parser parser(lexer);
    return parser.parse();
}

}
