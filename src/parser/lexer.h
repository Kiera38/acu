#pragma once

#include "source.h"
#include "tokens.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>

namespace acu::parser {

// Helper struct for position tracking
struct Position {
    std::size_t byte_index = 0;  // Index in the original UTF-8 string
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

class Lexer {
public:
    explicit Lexer(const Source& source);

    Token next_token();

private:
    // Helper functions for UTF-8 handling
    inline bool is_utf8_start_byte(unsigned char c) const;
    inline std::pair<char32_t, size_t> decode_utf8(const std::string& str, size_t pos) const;
    inline size_t encode_utf8(char32_t ch, char* buffer) const;

    // Internal implementation methods
    char32_t peek() const;
    char32_t next();
    bool at_end() const;
    bool at_end_index(std::size_t idx) const;
    bool match(char32_t c);
    Token make_token(TokenType type, const Position& start_pos = {}) const;
    template<typename T>
    Token make_token(TokenType type, const Position& start_pos, T value) const {
        Location loc;
        if (start_pos.byte_index != 0 || start_pos.line != 1 || start_pos.column != 1) {
            loc.start = static_cast<std::uint32_t>(start_pos.byte_index);
            loc.end = static_cast<std::uint32_t>(pos_.byte_index);
        } else {
            loc.start = static_cast<std::uint32_t>(pos_.byte_index > 0 ? pos_.byte_index - 1 : pos_.byte_index);
            loc.end = static_cast<std::uint32_t>(pos_.byte_index);
        }
        return Token{type, loc, utils::Variant<bool, std::int64_t, double, char32_t, std::string_view>(value)};
    }
    void skip_whitespace();
    void skip_comment();
    std::optional<Token> check_indent();
    
    // Helper function to check if a Unicode character is alphabetic
    bool is_unicode_alpha(char32_t c) const;
    
    // Helper function to check if a Unicode character is alphanumeric
    bool is_unicode_alnum(char32_t c) const;
    
    // Helper function to check if a Unicode character is a digit
    bool is_unicode_digit(char32_t c) const;
    
    Token identifier_or_keyword();
    Token number();
    Token hex_number();
    Token oct_number();
    Token bin_number();
    Token character();
    Token string();
    Token operator_();
    
    // Member variables
    const Source* source_;
    std::string_view source_text_;
    Position pos_;
    bool begin_of_line_;
    std::size_t dedents_;
    std::vector<std::string_view> indent_stack_;
};
}
