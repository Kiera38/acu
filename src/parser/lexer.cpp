#include "lexer.h"

#include <array>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace acu::parser {
namespace {
// Helper functions for UTF-8 handling (moved outside class)
inline bool is_utf8_start_byte(unsigned char c) {
    return (c & 0x80) == 0 || (c & 0xE0) == 0xC0 || (c & 0xF0) == 0xE0 ||
           (c & 0xF8) == 0xF0;
}

inline std::pair<char32_t, size_t> decode_utf8(
    std::string_view str, size_t pos
) {
    if (pos >= str.size()) {
        return {U'\0', 0};
    }

    auto byte1 = static_cast<unsigned char>(str[pos]);

    if ((byte1 & 0x80) == 0) {  // ASCII
        return {static_cast<char32_t>(byte1), 1};
    } else if ((byte1 & 0xE0) == 0xC0) {  // 2-byte sequence
        if (pos + 1 >= str.size()) return {U'\0', 0};
        auto byte2 = static_cast<unsigned char>(str[pos + 1]);
        char32_t ch = ((byte1 & 0x1F) << 6) | (byte2 & 0x3F);
        return {ch, 2};
    } else if ((byte1 & 0xF0) == 0xE0) {  // 3-byte sequence
        if (pos + 2 >= str.size()) return {U'\0', 0};
        auto byte2 = static_cast<unsigned char>(str[pos + 1]);
        auto byte3 = static_cast<unsigned char>(str[pos + 2]);
        char32_t ch =
            ((byte1 & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
        return {ch, 3};
    } else if ((byte1 & 0xF8) == 0xF0) {  // 4-byte sequence
        if (pos + 3 >= str.size()) return {U'\0', 0};
        auto byte2 = static_cast<unsigned char>(str[pos + 1]);
        auto byte3 = static_cast<unsigned char>(str[pos + 2]);
        auto byte4 = static_cast<unsigned char>(str[pos + 3]);
        char32_t ch = ((byte1 & 0x07) << 18) | ((byte2 & 0x3F) << 12) |
                      ((byte3 & 0x3F) << 6) | (byte4 & 0x3F);
        return {ch, 4};
    }

    return {U'\0', 0};  // Invalid UTF-8
}

inline size_t encode_utf8(char32_t ch, std::span<char, 4> buffer) {
    if (ch < 0x80) {
        buffer[0] = static_cast<char>(ch);
        return 1;
    } else if (ch < 0x800) {
        buffer[0] = static_cast<char>(0xC0 | (ch >> 6));
        buffer[1] = static_cast<char>(0x80 | (ch & 0x3F));
        return 2;
    } else if (ch < 0x10000) {
        buffer[0] = static_cast<char>(0xE0 | (ch >> 12));
        buffer[1] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | (ch & 0x3F));
        return 3;
    } else if (ch < 0x110000) {
        buffer[0] = static_cast<char>(0xF0 | (ch >> 18));
        buffer[1] = static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buffer[3] = static_cast<char>(0x80 | (ch & 0x3F));
        return 4;
    }
    return 0;  // Invalid Unicode
}

// Helper function to check if a Unicode character is alphabetic
bool is_unicode_alpha(char32_t c) {
    // Basic Latin letters
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_') {
        return true;
    }
    // Cyrillic letters (basic range)
    if ((c >= 0x0410 && c <= 0x042F) ||
        (c >= 0x0430 && c <= 0x044F)) {  // А-Я, а-я
        return true;
    }
    // Greek letters (basic range)
    if ((c >= 0x0391 && c <= 0x03A9) ||
        (c >= 0x03B1 && c <= 0x03C9)) {  // Α-Ω, α-ω
        return true;
    }
    // Common accented Latin letters
    if ((c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) ||
        (c >= 0xF8 && c <= 0xFF)) {
        return true;
    }
    // Extended Latin letters
    if ((c >= 0x100 && c <= 0x17F) || (c >= 0x180 && c <= 0x24F)) {
        return true;
    }
    // CJK letters (simplified)
    if ((c >= 0x4E00 && c <= 0x9FFF)) {
        return true;
    }
    // Arabic letters
    if ((c >= 0x0600 && c <= 0x06FF)) {
        return true;
    }
    // Hebrew letters
    if ((c >= 0x0590 && c <= 0x05FF)) {
        return true;
    }
    return false;
}

// Helper function to check if a Unicode character is alphanumeric
bool is_unicode_alnum(char32_t c) {
    return is_unicode_alpha(c) || (c >= U'0' && c <= U'9');
}

// Helper function to check if a Unicode character is a digit
bool is_unicode_digit(char32_t c) { return c >= U'0' && c <= U'9'; }

}

Token Lexer::make_token(TokenType type, std::uint32_t start_byte_index) const {
    Location loc;
    if (start_byte_index != 0) {
        loc.start = start_byte_index;
        loc.end = byte_index_;
    } else {
        loc.start = byte_index_ > 0 ? byte_index_ - 1 : byte_index_;
        loc.end = byte_index_;
    }
    return Token {.type = type, .location = loc, .value = {false}};
}

Token Lexer::make_token(
    TokenType type, std::uint32_t start_byte_index, Token::Value value
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
    auto [ch, len] = decode_utf8(source_text_, byte_index_);
    return ch;
}

char32_t Lexer::next() {
    if (at_end()) {
        return U'\0';
    }
    auto [ch, len] = decode_utf8(source_text_, byte_index_);
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
                    make_token(TokenType::Error, start_byte_index).location,
                    "Incorrect indentation: inconsistent tabs and spaces"
                );
                throw std::runtime_error(
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
                    make_token(TokenType::Error, start_byte_index).location,
                    "Incorrect indentation: inconsistent tabs and spaces"
                );
                throw std::runtime_error(
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
                make_token(TokenType::Error, start_byte_index).location,
                "Incorrect indentation size"
            );
            throw std::runtime_error("Incorrect indentation size");
        }

        if (prev_indent != indent) {
            err_handler_->error(
                *source_, 
                make_token(TokenType::Error, start_byte_index).location,
                "Incorrect indentation: inconsistent tabs and spaces"
            );
            throw std::runtime_error(
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

    while (is_unicode_alnum(peek()) || peek() == U'_') {
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
        {"from", TokenType::From}
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

    while (is_unicode_digit(peek()) || peek() == U'.' || peek() == U'_') {
        if (peek() == U'.') {
            text += '.';
            if (is_float) {
                err_handler_->error(
                    *source_, 
                    make_token(TokenType::Error, start_byte_index).location,
                    "Invalid number: multiple decimal points"
                );
                throw std::runtime_error(
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

    while (is_unicode_digit(peek()) || (peek() >= U'a' && peek() <= U'f') ||
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
            char32_t escaped = next();
            switch (escaped) {
                case U'n': return U'\n';
                case U't': return U'\t';
                case U'0': return U'\0';
                case U'\'': return U'\'';
                case U'\\': return U'\\';
                default:
                    err_handler_->error(
                        *source_, 
                        make_token(TokenType::Error, start_byte_index).location,
                        "Unknown escape sequence in character literal"
                    );
                    throw std::runtime_error(
                        "Unknown escape sequence in character literal"
                    );
            }
        } else {
            return next();
        }
    }();

    if (!match(U'\'')) {
        err_handler_->error(
            *source_, 
            make_token(TokenType::Error, start_byte_index).location,
            "Unterminated character literal"
        );
        throw std::runtime_error("Unterminated character literal");
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
            char32_t escaped = next();
            switch (escaped) {
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
                        make_token(TokenType::Error, start_byte_index).location,
                        "Unknown escape sequence in string literal"
                    );
                    throw std::runtime_error(
                        "Unknown escape sequence in string literal"
                    );
            }
        } else {
            std::array<char, 4> utf8_buffer {};
            size_t len = encode_utf8(c, utf8_buffer);
            for (size_t i = 0; i < len; ++i) {
                value += utf8_buffer.at(i);
            }
        }
    }

    if (at_end()) {
        err_handler_->error(
            *source_, 
            make_token(TokenType::Error, start_byte_index).location,
            "Unterminated string literal"
        );
        throw std::runtime_error("Unterminated string literal");
    }
    next();
    return make_token(
        TokenType::String, start_byte_index, {source_->strings.intern(value)}
    );
}

Token Lexer::operator_() {
    std::uint32_t start_byte_index = byte_index_;

    static const std::unordered_map<std::string_view, TokenType> operators = {
        {"(", TokenType::LParen},        {")", TokenType::RParen},
        {"[", TokenType::LBracket},      {"]", TokenType::RBracket},
        {"{", TokenType::LBrace},        {"}", TokenType::RBrace},
        {":", TokenType::Colon},         {";", TokenType::Semicolon},
        {",", TokenType::Comma},         {".", TokenType::Dot},
        {"~", TokenType::Tilde},         {"+", TokenType::Plus},
        {"+=", TokenType::PlusEqual},    {"-", TokenType::Minus},
        {"-=", TokenType::MinusEqual},   {"*", TokenType::Star},
        {"*=", TokenType::StarEqual},    {"/", TokenType::Slash},
        {"/=", TokenType::SlashEqual},   {"%", TokenType::Percent},
        {"%=", TokenType::PercentEqual}, {"=", TokenType::Equal},
        {"==", TokenType::EqualEqual},   {"!=", TokenType::NotEqual},
        {"<", TokenType::Less},          {"<=", TokenType::LessEqual},
        {">", TokenType::Greater},       {">=", TokenType::GreaterEqual},
        {"|", TokenType::Pipe},          {"|=", TokenType::PipeEqual},
        {"&", TokenType::Amp},           {"&=", TokenType::AmpEqual},
        {"^", TokenType::Caret},         {"^=", TokenType::CaretEqual}
    };

    std::string_view text_remaining = source_text_.substr(byte_index_);
    std::string_view longest_match;

    for (const auto& [op, type] : operators) {
        if (text_remaining.starts_with(op) &&
            op.length() > longest_match.length()) {
            longest_match = op;
        }
    }

    if (!longest_match.empty()) {
        for (size_t i = 0; i < longest_match.length(); ++i) {
            next();
        }
        auto it = operators.find(longest_match);
        return make_token(it->second, start_byte_index);
    }

    err_handler_->error(
        *source_,
        make_token(TokenType::Error, start_byte_index).location,
        "Unknown operator"
    );
    throw std::runtime_error("Unknown operator");
}

Token Lexer::next_token() {
    if (begin_of_line_) {
        if (auto token = check_indent(); token.has_value()) {
            return token.value();
        }
    }

    skip_whitespace();

    if (at_end()) {
        return make_token(TokenType::EndOfFile);
    }

    char32_t c = peek();

    if (is_unicode_digit(c)) {
        if (c == U'0' && !at_end_index(byte_index_ + 1)) {
            auto [next_ch, next_len] =
                decode_utf8(source_text_, byte_index_ + 1);
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

    if (is_unicode_alpha(c)) {
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

    return operator_();
}

std::string token_type_to_string(TokenType type) {
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

std::string token_value_to_string(const Token& token) {
    return token.value.visit(
        [](bool val) -> std::string { return val ? "true" : "false"; },
        [](std::int64_t val) { return std::to_string(val); },
        [](double val) { return std::to_string(val); },
        [](char32_t val) {
            if (val < 0x80) {
                return std::string(1, static_cast<char>(val));
            } else {
                return "U+" + std::to_string(static_cast<uint32_t>(val));
            }
        },
        [](std::string_view val) { return std::string(val); }
    );
}

std::string token_to_string(const Token& token) {
    std::string result = token_type_to_string(token.type);
    if (token.type == TokenType::Identifier ||
        token.type == TokenType::Integer || token.type == TokenType::Float ||
        token.type == TokenType::Char || token.type == TokenType::String) {
        result += "(" + token_value_to_string(token) + ")";
    }
    result += " @ " + std::to_string(token.location.start) + "-" +
              std::to_string(token.location.end);
    return result;
}

}
