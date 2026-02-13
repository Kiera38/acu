#include "lexer.h"
#include <stdexcept>
#include <unordered_map>
#include <cctype>
#include <algorithm>
#include <string_view>
#include <optional>
#include <vector>
#include <locale>
#include <codecvt>

namespace acu::parser {

// Helper functions for UTF-8 handling
inline bool Lexer::is_utf8_start_byte(unsigned char c) const {
    return (c & 0x80) == 0 || (c & 0xE0) == 0xC0 || (c & 0xF0) == 0xE0 || (c & 0xF8) == 0xF0;
}

inline std::pair<char32_t, size_t> Lexer::decode_utf8(const std::string& str, size_t pos) const {
    if (pos >= str.size()) {
        return {U'\0', 0};
    }
    
    unsigned char byte1 = static_cast<unsigned char>(str[pos]);
    
    if ((byte1 & 0x80) == 0) { // ASCII
        return {static_cast<char32_t>(byte1), 1};
    } else if ((byte1 & 0xE0) == 0xC0) { // 2-byte sequence
        if (pos + 1 >= str.size()) return {U'\0', 0};
        unsigned char byte2 = static_cast<unsigned char>(str[pos + 1]);
        char32_t ch = ((byte1 & 0x1F) << 6) | (byte2 & 0x3F);
        return {ch, 2};
    } else if ((byte1 & 0xF0) == 0xE0) { // 3-byte sequence
        if (pos + 2 >= str.size()) return {U'\0', 0};
        unsigned char byte2 = static_cast<unsigned char>(str[pos + 1]);
        unsigned char byte3 = static_cast<unsigned char>(str[pos + 2]);
        char32_t ch = ((byte1 & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
        return {ch, 3};
    } else if ((byte1 & 0xF8) == 0xF0) { // 4-byte sequence
        if (pos + 3 >= str.size()) return {U'\0', 0};
        unsigned char byte2 = static_cast<unsigned char>(str[pos + 1]);
        unsigned char byte3 = static_cast<unsigned char>(str[pos + 2]);
        unsigned char byte4 = static_cast<unsigned char>(str[pos + 3]);
        char32_t ch = ((byte1 & 0x07) << 18) | ((byte2 & 0x3F) << 12) | ((byte3 & 0x3F) << 6) | (byte4 & 0x3F);
        return {ch, 4};
    }
    
    return {U'\0', 0}; // Invalid UTF-8
}

inline size_t Lexer::encode_utf8(char32_t ch, char* buffer) const {
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
    return 0; // Invalid Unicode
}

Token Lexer::make_token(TokenType type, const Position& start_pos) const {
    Location loc;
    if (start_pos.byte_index != 0 || start_pos.line != 1 || start_pos.column != 1) {
        loc.start = static_cast<std::uint32_t>(start_pos.byte_index);
        loc.end = static_cast<std::uint32_t>(pos_.byte_index);
    } else {
        loc.start = static_cast<std::uint32_t>(pos_.byte_index > 0 ? pos_.byte_index - 1 : pos_.byte_index);
        loc.end = static_cast<std::uint32_t>(pos_.byte_index);
    }
    return Token{type, loc, false};
}

Lexer::Lexer(const Source& source) : source_(&source), source_text_(source.content), pos_({0, 1, 1}), begin_of_line_(true), dedents_(0) {
    indent_stack_.emplace_back(std::string_view(""));
}

char32_t Lexer::peek() const {
    if (at_end()) {
        return U'\0';
    }
    auto [ch, len] = decode_utf8(std::string(source_text_), pos_.byte_index);
    return ch;
}

char32_t Lexer::next() {
    if (at_end()) {
        return U'\0';
    }
    auto [ch, len] = decode_utf8(std::string(source_text_), pos_.byte_index);
    if (ch == U'\n') {
        pos_.byte_index += len;
        pos_.line++;
        pos_.column = 1;
    } else {
        pos_.byte_index += len;
        pos_.column++;
    }
    return ch;
}

bool Lexer::at_end() const {
    return pos_.byte_index >= source_text_.size();
}

bool Lexer::at_end_index(std::size_t idx) const {
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
    while (!at_end() && (peek() == U' ' || peek() == U'\t')) {
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
        Position start = pos_;
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
        
        std::string_view indent = source_text_.substr(start.byte_index, pos_.byte_index - start.byte_index);
        std::string_view prev_indent = indent_stack_.back();
        
        if (indent == prev_indent) {
            begin_of_line_ = false;
            return std::nullopt;
        }
        
        if (indent.length() > prev_indent.length()) {
            if (indent.substr(0, prev_indent.length()) != prev_indent) {
                throw std::runtime_error("Incorrect indentation: inconsistent tabs and spaces");
            }
            indent_stack_.push_back(indent);
            begin_of_line_ = false;
            return make_token(TokenType::Indent, start);
        }
        
        while (indent.length() < prev_indent.length()) {
            if (prev_indent.substr(0, indent.length()) != indent) {
                throw std::runtime_error("Incorrect indentation: inconsistent tabs and spaces");
            }
            dedents_++;
            indent_stack_.pop_back();
            prev_indent = indent_stack_.back();
        }
        
        if (indent.length() != prev_indent.length()) {
            throw std::runtime_error("Incorrect indentation size");
        }
        
        if (prev_indent != indent) {
            throw std::runtime_error("Incorrect indentation: inconsistent tabs and spaces");
        }
        
        dedents_--;
        if (dedents_ == 0) {
            begin_of_line_ = false;
        }
        return make_token(TokenType::Dedent, start);
    }
    
    return std::nullopt;
}

// Helper function to check if a Unicode character is alphabetic
bool Lexer::is_unicode_alpha(char32_t c) const {
    // Basic Latin letters
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_') {
        return true;
    }
    // Cyrillic letters (basic range)
    if ((c >= 0x0410 && c <= 0x042F) || (c >= 0x0430 && c <= 0x044F)) { // А-Я, а-я
        return true;
    }
    // Greek letters (basic range)
    if ((c >= 0x0391 && c <= 0x03A9) || (c >= 0x03B1 && c <= 0x03C9)) { // Α-Ω, α-ω
        return true;
    }
    // Common accented Latin letters
    if ((c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0xFF)) {
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
bool Lexer::is_unicode_alnum(char32_t c) const {
    return is_unicode_alpha(c) || (c >= U'0' && c <= U'9');
}

// Helper function to check if a Unicode character is a digit
bool Lexer::is_unicode_digit(char32_t c) const {
    return c >= U'0' && c <= U'9';
}

Token Lexer::identifier_or_keyword() {
    Position start = pos_;
    while (is_unicode_alnum(peek()) || peek() == U'_') {
        next();
    }
    std::string_view id = std::string_view(source_text_).substr(start.byte_index, pos_.byte_index - start.byte_index);
    
    static const std::unordered_map<std::string_view, TokenType> keywords = {
        {"func", TokenType::Func},
        {"if", TokenType::If},
        {"else", TokenType::Else},
        {"while", TokenType::While},
        {"var", TokenType::Var},
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
    TokenType type = (it != keywords.end()) ? it->second : TokenType::Identifier;
    return make_token(type, start, id);
}

Token Lexer::number() {
    Position start = pos_;
    std::string text;
    bool is_float = false;
    
    while (is_unicode_digit(peek()) || peek() == U'.' || peek() == U'_') {
        if (peek() == U'.') {
            text += '.';
            if (is_float) {
                throw std::runtime_error("Invalid number: multiple decimal points");
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
        return make_token(TokenType::Float, start, std::stod(text));
    } else {
        return make_token(TokenType::Integer, start, std::stoll(text));
    }
}

Token Lexer::hex_number() {
    Position start = pos_;
    std::string text;
    next(); // consume '0'
    next(); // consume 'x'
    
    while (is_unicode_digit(peek()) || 
           (peek() >= U'a' && peek() <= U'f') || 
           (peek() >= U'A' && peek() <= U'F') ||
           peek() == U'_') {
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
    
    return make_token(TokenType::Integer, start, std::stoll(text, nullptr, 16));
}

Token Lexer::oct_number() {
    Position start = pos_;
    std::string text;
    next(); // consume '0'
    next(); // consume 'o'
    
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
    
    return make_token(TokenType::Integer, start, std::stoll(text, nullptr, 8));
}

Token Lexer::bin_number() {
    Position start = pos_;
    std::string text;
    next(); // consume '0'
    next(); // consume 'b'
    
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
    
    return make_token(TokenType::Integer, start, std::stoll(text, nullptr, 2));
}

Token Lexer::character() {
    Position start = pos_;
    next(); // consume opening '
    
    char32_t value;
    if (match(U'\\')) {
        char32_t escaped = next();
        switch (escaped) {
            case U'n': value = U'\n'; break;
            case U't': value = U'\t'; break;
            case U'0': value = U'\0'; break;
            case U'\'': value = U'\''; break;
            case U'\\': value = U'\\'; break;
            default: throw std::runtime_error("Unknown escape sequence in character literal");
        }
    } else {
        value = next();
    }
    
    if (!match(U'\'')) {
        throw std::runtime_error("Unterminated character literal");
    }
    
    return make_token(TokenType::Char, start, value);
}

Token Lexer::string() {
    Position start = pos_;
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
                default: throw std::runtime_error("Unknown escape sequence in string literal");
            }
        } else {
            // Convert the Unicode character to UTF-8 bytes and append to value
            char utf8_buffer[4];
            size_t len = encode_utf8(c, utf8_buffer);
            for (size_t i = 0; i < len; ++i) {
                value += utf8_buffer[i];
            }
        }
    }
    
    if (at_end()) {
        throw std::runtime_error("Unterminated string literal");
    }
    
    next(); // consume closing "
    
    return make_token(TokenType::String, start, std::string_view(value));
}

Token Lexer::operator_() {
    Position start = pos_;
    
    static const std::unordered_map<std::string_view, TokenType> operators = {
        {"(", TokenType::LParen},
        {")", TokenType::RParen},
        {"[", TokenType::LBracket},
        {"]", TokenType::RBracket},
        {"{", TokenType::LBrace},
        {"}", TokenType::RBrace},
        {":", TokenType::Colon},
        {";", TokenType::Semicolon},
        {",", TokenType::Comma},
        {".", TokenType::Dot},
        {"~", TokenType::Tilde},
        {"+", TokenType::Plus},
        {"+=", TokenType::PlusEqual},
        {"-", TokenType::Minus},
        {"-=", TokenType::MinusEqual},
        {"*", TokenType::Star},
        {"*=", TokenType::StarEqual},
        {"/", TokenType::Slash},
        {"/=", TokenType::SlashEqual},
        {"%", TokenType::Percent},
        {"%=", TokenType::PercentEqual},
        {"=", TokenType::Equal},
        {"==", TokenType::EqualEqual},
        {"!=", TokenType::NotEqual},
        {"<", TokenType::Less},
        {"<=", TokenType::LessEqual},
        {">", TokenType::Greater},
        {">=", TokenType::GreaterEqual},
        {"|", TokenType::Pipe},
        {"|=", TokenType::PipeEqual},
        {"&", TokenType::Amp},
        {"&=", TokenType::AmpEqual},
        {"^", TokenType::Caret},
        {"^=", TokenType::CaretEqual}
    };
    
    // Look ahead to find the longest matching operator
    std::string_view text_remaining = source_text_.substr(pos_.byte_index);
    std::string_view longest_match;
    
    for (const auto& [op, type] : operators) {
        if (text_remaining.substr(0, op.length()) == op && op.length() > longest_match.length()) {
            longest_match = op;
        }
    }
    
    if (!longest_match.empty()) {
        // Advance position by the length of the matched operator
        for (size_t i = 0; i < longest_match.length(); ++i) {
            next();
        }
        auto it = operators.find(longest_match);
        return make_token(it->second, start);
    }
    
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
        if (c == U'0' && !at_end_index(pos_.byte_index + 1)) {
            // For checking the next character, we need to decode the next UTF-8 sequence
            auto [next_ch, next_len] = decode_utf8(std::string(source_text_), pos_.byte_index + 1);
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
    
    if (c == U'\n') {
        Position start = pos_;
        begin_of_line_ = true;
        Token token = make_token(TokenType::NewLine, start);
        next();
        return token;
    }
    
    return operator_();
}

} // namespace acu::parser