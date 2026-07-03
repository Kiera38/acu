#include "lexer.h"

#include <array>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "unicode.h"

namespace acu::parser {

Token Lexer::make_token(
    TokenType type, std::uint32_t start_byte_index, Value value
) const {
    Location loc;
    if (start_byte_index != 0) {
        loc.start = start_byte_index;
        loc.end = byte_index_;
    } else {
        loc.start = byte_index_ > 0 ? byte_index_ - 1 : byte_index_;
        loc.end = byte_index_;
    }
    return Token {.type = type, .location = loc, .value = {value}};
}

Lexer::Lexer(Source& source, ErrorHandler& err_handler)
    : source_(&source),
      source_text_(source.content),
      begin_of_line_(true),
      dedents_(0),
      err_handler_(&err_handler) {
    indent_stack_.emplace_back("");
}

char32_t Lexer::peek() const {
    if (at_end()) {
        return U'\0';
    }
    auto [ch, len] = unicode::decode_utf8(source_text_, byte_index_);
    return ch;
}

char32_t Lexer::next() {
    if (at_end()) {
        return U'\0';
    }
    auto [ch, len] = unicode::decode_utf8(source_text_, byte_index_);
    byte_index_ += len;
    return ch;
}

bool Lexer::at_end() const { return byte_index_ >= source_text_.size(); }

bool Lexer::at_end_index(std::uint32_t idx) const {
    return idx >= source_text_.size();
}

bool Lexer::match(char32_t c) {
    if (peek() == c) {
        next();
        return true;
    }
    return false;
}

void Lexer::skip_whitespace() {
    while (!at_end() &&
           (peek() == U' ' || peek() == U'\t' || peek() == U'\r')) {
        next();
    }
}

void Lexer::skip_comment() {
    while (!at_end() && peek() != U'\n') {
        next();
    }
}

std::optional<Token> Lexer::check_indent() {
    if (dedents_ > 0) {
        dedents_--;
        if (dedents_ == 0) {
            begin_of_line_ = false;
        }
        return make_token(TokenType::Dedent);
    }

    if (at_end() && indent_stack_.size() > 1) {
        indent_stack_.pop_back();
        return make_token(TokenType::Dedent);
    }

    while (!at_end()) {
        std::uint32_t start_byte_index = byte_index_;

        while (peek() == U' ' || peek() == U'\t') {
            next();
        }

        if (match(U'#')) {
            skip_comment();
            continue;
        }

        if (peek() == U'\n' || peek() == U'\r') {
            next();
            continue;
        }

        if (at_end()) {
            return std::nullopt;
        }

        std::string_view indent = source_text_.substr(
            start_byte_index, byte_index_ - start_byte_index
        );
        std::string_view prev_indent = indent_stack_.back();

        if (indent == prev_indent) {
            begin_of_line_ = false;
            return std::nullopt;
        }

        if (indent.length() > prev_indent.length()) {
            if (!indent.starts_with(prev_indent)) {
                err_handler_->error(
                    *source_,
                    {.start = start_byte_index, .end = byte_index_},
                    "Incorrect indentation: inconsistent tabs and spaces"
                );
            }
            indent_stack_.push_back(indent);
            begin_of_line_ = false;
            return make_token(TokenType::Indent, start_byte_index);
        }

        while (indent.length() < prev_indent.length()) {
            if (!prev_indent.starts_with(indent)) {
                err_handler_->error(
                    *source_,
                    {.start = start_byte_index, .end = byte_index_},
                    "Incorrect indentation: inconsistent tabs and spaces"
                );
            }
            dedents_++;
            indent_stack_.pop_back();
            prev_indent = indent_stack_.back();
        }

        if (indent.length() != prev_indent.length()) {
            err_handler_->error(
                *source_,
                {.start = start_byte_index, .end = byte_index_},
                "Incorrect indentation size"
            );
        }

        if (prev_indent != indent) {
            err_handler_->error(
                *source_,
                {.start = start_byte_index, .end = byte_index_},
                "Incorrect indentation: inconsistent tabs and spaces"
            );
        }

        dedents_--;
        if (dedents_ == 0) {
            begin_of_line_ = false;
        }
        return make_token(TokenType::Dedent, start_byte_index);
    }

    return std::nullopt;
}

Token Lexer::identifier_or_keyword() {
    std::uint32_t start_byte_index = byte_index_;

    while (unicode::is_alnum(peek()) || peek() == U'_') {
        next();
    }
    std::string_view id =
        source_text_.substr(start_byte_index, byte_index_ - start_byte_index);

    static const std::unordered_map<std::string_view, TokenType> keywords = {
        {"func", TokenType::Func},
        {"extern", TokenType::Extern},
        {"if", TokenType::If},
        {"else", TokenType::Else},
        {"while", TokenType::While},
        {"let", TokenType::Let},
        {"var", TokenType::Var},
        {"val", TokenType::Val},
        {"struct", TokenType::Struct},
        {"or", TokenType::Or},
        {"and", TokenType::And},
        {"not", TokenType::Not},
        {"return", TokenType::Return},
        {"break", TokenType::Break},
        {"continue", TokenType::Continue},
        {"as", TokenType::As},
        {"true", TokenType::True},
        {"false", TokenType::False},
        {"using", TokenType::Using},
        {"from", TokenType::From},
        {"public", TokenType::Public}
    };

    auto it = keywords.find(id);
    TokenType type =
        (it != keywords.end()) ? it->second : TokenType::Identifier;
    return make_token(type, start_byte_index, {id});
}

Token Lexer::number() {
    std::uint32_t start_byte_index = byte_index_;

    std::string text;
    bool is_float = false;

    while (unicode::is_digit(peek()) || peek() == U'.' || peek() == U'_') {
        if (peek() == U'.') {
            text += '.';
            if (is_float) {
                err_handler_->error(
                    *source_,
                    {.start = start_byte_index, .end = byte_index_},
                    "Invalid number: multiple decimal points"
                );
            }
            is_float = true;
        } else if (peek() != U'_') {
            // Convert the Unicode digit back to ASCII for numeric parsing
            char ascii_char = static_cast<char>(peek());
            text += ascii_char;
        }
        next();
        if (at_end()) {
            break;
        }
    }

    if (is_float) {
        return make_token(
            TokenType::Float, start_byte_index, {std::stod(text)}
        );
    } else {
        return make_token(
            TokenType::Integer, start_byte_index, {std::stoll(text)}
        );
    }
}

Token Lexer::hex_number() {
    std::uint32_t start_byte_index = byte_index_;

    std::string text;
    next();  // consume '0'
    next();  // consume 'x'

    while (unicode::is_digit(peek()) || (peek() >= U'a' && peek() <= U'f') ||
           (peek() >= U'A' && peek() <= U'F') || peek() == U'_') {
        if (peek() != U'_') {
            // Convert the Unicode character back to ASCII for numeric parsing
            char ascii_char = static_cast<char>(peek());
            text += ascii_char;
        }
        next();
        if (at_end()) {
            break;
        }
    }

    return make_token(
        TokenType::Integer, start_byte_index, {std::stoll(text, nullptr, 16)}
    );
}

Token Lexer::oct_number() {
    std::uint32_t start_byte_index = byte_index_;

    std::string text;
    next();  // consume '0'
    next();  // consume 'o'

    while ((peek() >= U'0' && peek() <= U'7') || peek() == U'_') {
        if (peek() != U'_') {
            // Convert the Unicode character back to ASCII for numeric parsing
            char ascii_char = static_cast<char>(peek());
            text += ascii_char;
        }
        next();
        if (at_end()) {
            break;
        }
    }

    return make_token(
        TokenType::Integer, start_byte_index, {std::stoll(text, nullptr, 8)}
    );
}

Token Lexer::bin_number() {
    std::uint32_t start_byte_index = byte_index_;

    std::string text;
    next();  // consume '0'
    next();  // consume 'b'

    while ((peek() == U'0' || peek() == U'1') || peek() == U'_') {
        if (peek() != U'_') {
            // Convert the Unicode character back to ASCII for numeric parsing
            char ascii_char = static_cast<char>(peek());
            text += ascii_char;
        }
        next();
        if (at_end()) {
            break;
        }
    }

    return make_token(
        TokenType::Integer, start_byte_index, {std::stoll(text, nullptr, 2)}
    );
}

Token Lexer::character() {
    std::uint32_t start_byte_index = byte_index_;

    next();  // consume opening '

    char32_t value = [&] {
        if (match(U'\\')) {
            char32_t escaped = peek();
            switch (escaped) {
                case U'n':
                    next();
                    return U'\n';
                case U't':
                    next();
                    return U'\t';
                case U'0':
                    next();
                    return U'\0';
                case U'\'':
                    next();
                    return U'\'';
                case U'\\':
                    next();
                    return U'\\';
                default:
                    err_handler_->error(
                        *source_,
                        {.start = start_byte_index, .end = byte_index_},
                        "Unknown escape sequence in character literal"
                    );
                    return U'\\';
            }
        } else {
            return next();
        }
    }();

    if (!match(U'\'')) {
        err_handler_->error(
            *source_,
            {.start = start_byte_index, .end = byte_index_},
            "Unterminated character literal"
        );
    }

    return make_token(TokenType::Char, start_byte_index, {value});
}

Token Lexer::string() {
    std::uint32_t start_byte_index = byte_index_;
    next();
    std::string value;
    while (peek() != U'"' && !at_end()) {
        char32_t c = next();
        if (c == U'\\') {
            switch (next()) {
                case U'n': {
                    value += '\n';
                    break;
                }
                case U't': {
                    value += '\t';
                    break;
                }
                case U'0': {
                    value += '\0';
                    break;
                }
                case U'"': {
                    value += '"';
                    break;
                }
                case U'\\': {
                    value += '\\';
                    break;
                }
                default:
                    err_handler_->error(
                        *source_,
                        {.start = start_byte_index, .end = byte_index_},
                        "Unknown escape sequence in string literal"
                    );
            }
        } else {
            std::array<char, 4> utf8_buffer {};
            size_t len = unicode::encode_utf8(c, utf8_buffer);
            for (size_t i = 0; i < len; ++i) {
                value += utf8_buffer.at(i);
            }
        }
    }

    if (at_end()) {
        err_handler_->error(
            *source_,
            {.start = start_byte_index, .end = byte_index_},
            "Unterminated string literal"
        );
    }
    next();
    return make_token(
        TokenType::String, start_byte_index, {source_->strings.intern(value)}
    );
}

std::optional<Token> Lexer::operator_() {
    std::uint32_t start_byte_index = byte_index_;

    switch (next()) {
        case '(': return make_token(TokenType::LParen, start_byte_index);
        case ')': return make_token(TokenType::RParen, start_byte_index);
        case '[': return make_token(TokenType::LBracket, start_byte_index);
        case ']': return make_token(TokenType::RBracket, start_byte_index);
        case '{': return make_token(TokenType::LBrace, start_byte_index);
        case '}': return make_token(TokenType::RBrace, start_byte_index);
        case ',': return make_token(TokenType::Comma, start_byte_index);
        case '.': return make_token(TokenType::Dot, start_byte_index);
        case ':': return make_token(TokenType::Colon, start_byte_index);
        case '~': return make_token(TokenType::Tilde, start_byte_index);
        case '+':
            if (match('='))
                return make_token(TokenType::PlusEqual, start_byte_index);
            else
                return make_token(TokenType::Plus, start_byte_index);
        case '-':
            if (match('='))
                return make_token(TokenType::MinusEqual, start_byte_index);
            else
                return make_token(TokenType::Minus, start_byte_index);
        case '*':
            if (match('='))
                return make_token(TokenType::StarEqual, start_byte_index);
            else
                return make_token(TokenType::Star, start_byte_index);
        case '/':
            if (match('='))
                return make_token(TokenType::SlashEqual, start_byte_index);
            else
                return make_token(TokenType::Slash, start_byte_index);
        case '%':
            if (match('='))
                return make_token(TokenType::PercentEqual, start_byte_index);
            else
                return make_token(TokenType::Percent, start_byte_index);
        case '&':
            if (match('='))
                return make_token(TokenType::AmpEqual, start_byte_index);
            else
                return make_token(TokenType::Amp, start_byte_index);
        case '|':
            if (match('='))
                return make_token(TokenType::PipeEqual, start_byte_index);
            else
                return make_token(TokenType::Pipe, start_byte_index);
        case '^':
            if (match('='))
                return make_token(TokenType::CaretEqual, start_byte_index);
            else
                return make_token(TokenType::Caret, start_byte_index);
        case '=':
            if (match('='))
                return make_token(TokenType::EqualEqual, start_byte_index);
            else
                return make_token(TokenType::Equal, start_byte_index);
        case '!':
            if (!match('=')) {
                err_handler_->error(
                    *source_,
                    {.start = start_byte_index, .end = byte_index_},
                    "unknown operator, maybe use 'not'"
                );
            }
            return make_token(TokenType::NotEqual, start_byte_index);
        case '<':
            if (match('='))
                return make_token(TokenType::LessEqual, start_byte_index);
            else if (match('<')) {
                if (match('='))
                    return make_token(
                        TokenType::LessLessEqual, start_byte_index
                    );
                else
                    return make_token(TokenType::LessLess, start_byte_index);
            }
            return make_token(TokenType::Less, start_byte_index);
        case '>':
            if (match('='))
                return make_token(TokenType::GreaterEqual, start_byte_index);
            else if (match('>')) {
                if (match('='))
                    return make_token(
                        TokenType::GreaterGreaterEqual, start_byte_index
                    );
                else
                    return make_token(
                        TokenType::GreaterGreater, start_byte_index
                    );
            }
            return make_token(TokenType::Greater, start_byte_index);
        default:
            err_handler_->error(
                *source_,
                {.start = start_byte_index, .end = byte_index_},
                "Unknown character"
            );
            return std::nullopt;
    }
}

Token Lexer::next_token() {
    while (!at_end()) {
        if (begin_of_line_) {
            if (auto token = check_indent(); token) {
                return *token;
            }
        }

        skip_whitespace();

        if (at_end()) {
            return make_token(TokenType::EndOfFile);
        }

        char32_t c = peek();

        if (unicode::is_digit(c)) {
            if (c == U'0' && !at_end_index(byte_index_ + 1)) {
                auto [next_ch, next_len] =
                    unicode::decode_utf8(source_text_, byte_index_ + 1);
                if (next_ch == U'x' || next_ch == U'X') {
                    return hex_number();
                } else if (next_ch == U'o' || next_ch == U'O') {
                    return oct_number();
                } else if (next_ch == U'b' || next_ch == U'B') {
                    return bin_number();
                }
            }
            return number();
        }

        if (unicode::is_alpha(c)) {
            return identifier_or_keyword();
        }

        if (c == U'\'') {
            return character();
        }

        if (c == U'"') {
            return string();
        }

        if (c == U'\n' || c == U'\r') {
            std::uint32_t start_byte_index = byte_index_;
            begin_of_line_ = true;
            if (c == U'\r' && peek() == U'\n') {
                next();
            }
            Token token = make_token(TokenType::NewLine, start_byte_index);
            return token;
        }
        if (auto token = operator_()) {
            return *token;
        }
    }
    return make_token(TokenType::EndOfFile, byte_index_);
}

std::string_view to_string(TokenType type) {
    switch (type) {
        case TokenType::Func: return "FUNC";
        case TokenType::Extern: return "EXTERN";
        case TokenType::If: return "IF";
        case TokenType::Else: return "ELSE";
        case TokenType::While: return "WHILE";
        case TokenType::Let: return "LET";
        case TokenType::Var: return "VAR";
        case TokenType::Val: return "VAL";
        case TokenType::Struct: return "STRUCT";
        case TokenType::And: return "AND";
        case TokenType::Or: return "OR";
        case TokenType::Not: return "NOT";
        case TokenType::Return: return "RETURN";
        case TokenType::Break: return "BREAK";
        case TokenType::Continue: return "CONTINUE";
        case TokenType::As: return "AS";
        case TokenType::True: return "TRUE";
        case TokenType::False: return "FALSE";
        case TokenType::Using: return "USING";
        case TokenType::From: return "FROM";
        case TokenType::Plus: return "PLUS";
        case TokenType::Minus: return "MINUS";
        case TokenType::Star: return "STAR";
        case TokenType::Slash: return "SLASH";
        case TokenType::Percent: return "PERCENT";
        case TokenType::Equal: return "EQUAL";
        case TokenType::PlusEqual: return "PLUSEQUAL";
        case TokenType::MinusEqual: return "MINUSEQUAL";
        case TokenType::StarEqual: return "STAREQUAL";
        case TokenType::SlashEqual: return "SLASHEQUAL";
        case TokenType::PercentEqual: return "PERCENTEQUAL";
        case TokenType::Less: return "LESS";
        case TokenType::LessEqual: return "LESSEQUAL";
        case TokenType::Greater: return "GREATER";
        case TokenType::GreaterEqual: return "GREATEREQUAL";
        case TokenType::NotEqual: return "NOTEQUAL";
        case TokenType::EqualEqual: return "EQUALEQUAL";
        case TokenType::Pipe: return "PIPE";
        case TokenType::Tilde: return "TILDE";
        case TokenType::Amp: return "AMP";
        case TokenType::Caret: return "CARET";
        case TokenType::LessLess: return "LESSLESS";
        case TokenType::GreaterGreater: return "GREATERGREATER";
        case TokenType::PipeEqual: return "PIPEEQUAL";
        case TokenType::TildeEqual: return "TILDEEQUAL";
        case TokenType::AmpEqual: return "AMPEQUAL";
        case TokenType::CaretEqual: return "CARETEQUAL";
        case TokenType::LessLessEqual: return "LESSLESSEQUAL";
        case TokenType::GreaterGreaterEqual: return "GREATERGREATEREQUAL";
        case TokenType::LParen: return "LPAREN";
        case TokenType::RParen: return "RPAREN";
        case TokenType::LBracket: return "LBRACKET";
        case TokenType::RBracket: return "RBRACKET";
        case TokenType::LBrace: return "LBRACE";
        case TokenType::RBrace: return "RBRACE";
        case TokenType::Colon: return "COLON";
        case TokenType::Semicolon: return "SEMICOLON";
        case TokenType::Comma: return "COMMA";
        case TokenType::Dot: return "DOT";
        case TokenType::Integer: return "INTEGER";
        case TokenType::Float: return "FLOAT";
        case TokenType::Char: return "CHAR";
        case TokenType::String: return "STRING";
        case TokenType::Identifier: return "IDENTIFIER";
        case TokenType::Indent: return "INDENT";
        case TokenType::Dedent: return "DEDENT";
        case TokenType::NewLine: return "NEWLINE";
        case TokenType::EndOfFile: return "END_OF_FILE";
        default: return "UNKNOWN";
    }
}

std::string to_string(const Value& value) {
    return value.visit(
        [](std::monostate) -> std::string { return "None"; },
        [](bool val) -> std::string { return val ? "true" : "false"; },
        [](std::int64_t val) { return std::to_string(val); },
        [](double val) { return std::to_string(val); },
        [](char32_t val) {
            std::array<char, 4> buffer {};
            auto count = unicode::encode_utf8(val, buffer);
            return std::string(buffer.data(), count);
        },
        [](std::string_view val) { return std::format("\"{}\"", val); }
    );
}

std::string to_string(const Token& token) {
    auto type = to_string(token.type);
    if (token.type == TokenType::Identifier ||
        token.type == TokenType::Integer || token.type == TokenType::Float ||
        token.type == TokenType::Char || token.type == TokenType::String) {
        return std::format("{} ({})", type, to_string(token.value));
    }
    return std::string(type);
}

}
