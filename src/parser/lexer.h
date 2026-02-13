#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "source.h"
#include "tokens.h"

namespace acu::parser {
class Lexer {
public:
    explicit Lexer(const Source& source);

    Token next_token();

private:
    // Internal implementation methods
    [[nodiscard]] char32_t peek() const;
    char32_t next();
    [[nodiscard]] bool at_end() const;
    [[nodiscard]] bool at_end_index(std::uint32_t idx) const;
    bool match(char32_t c);
    [[nodiscard]] Token make_token(
        TokenType type, std::uint32_t start_byte_index = 0
    ) const;
    [[nodiscard]] Token make_token(
        TokenType type, std::uint32_t start_byte_index, Token::Value value
    ) const;
    void skip_whitespace();
    void skip_comment();
    std::optional<Token> check_indent();

    // Helper function to check if a Unicode character is alphabetic
    [[nodiscard]] bool is_unicode_alpha(char32_t c) const;

    // Helper function to check if a Unicode character is alphanumeric
    [[nodiscard]] bool is_unicode_alnum(char32_t c) const;

    // Helper function to check if a Unicode character is a digit
    [[nodiscard]] bool is_unicode_digit(char32_t c) const;

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
    std::uint32_t byte_index_ = 0;  // Index in the original UTF-8 string
    bool begin_of_line_;
    std::uint32_t dedents_;
    std::vector<std::string_view> indent_stack_;
};
}
