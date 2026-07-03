#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "errors.h"
#include "source.h"
#include "tokens.h"

namespace acu::parser {
class Lexer {
public:
    explicit Lexer(Source& source, ErrorHandler& err_handler);

    Token next_token();
    [[nodiscard]] Source& source() { return *source_; }

private:
    // Internal implementation methods
    [[nodiscard]] char32_t peek() const;
    char32_t next();
    [[nodiscard]] bool at_end() const;
    [[nodiscard]] bool at_end_index(std::uint32_t idx) const;
    bool match(char32_t c);
    [[nodiscard]] Token make_token(
        TokenType type,
        std::uint32_t start_byte_index = 0,
        Value value = std::monostate {}
    ) const;
    void skip_whitespace();
    void skip_comment();
    std::optional<Token> check_indent();

    Token identifier_or_keyword();
    Token number();
    Token hex_number();
    Token oct_number();
    Token bin_number();
    Token character();
    Token string();
    std::optional<Token> operator_();

    // Member variables
    Source* source_;
    std::string_view source_text_;
    std::uint32_t byte_index_ = 0;  // Index in the original UTF-8 string
    bool begin_of_line_;
    std::uint32_t dedents_;
    std::vector<std::string_view> indent_stack_;
    ErrorHandler* err_handler_;
};
}
