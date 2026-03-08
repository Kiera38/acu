#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "source.h"
#include "variant.h"

namespace acu::parser {
enum class TokenType : std::uint8_t {
    Func,
    If,
    Else,
    While,
    Var,
    Struct,
    And,
    Or,
    Not,
    Return,
    Break,
    Continue,
    As,
    True,
    False,
    Using,
    From,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Equal,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,

    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    NotEqual,
    EqualEqual,

    Pipe,
    Tilde,
    Amp,
    Caret,
    LessLess,
    GreaterGreater,

    PipeEqual,
    TildeEqual,
    AmpEqual,
    CaretEqual,
    LessLessEqual,
    GreaterGreaterEqual,

    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,

    Colon,
    Semicolon,
    Comma,
    Dot,

    Integer,
    Float,
    Char,
    String,
    Identifier,

    Indent,
    Dedent,
    NewLine,
    EndOfFile,
    Error,
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    Location location;
    using Value =
        utils::Variant<bool, std::int64_t, double, char32_t, std::string_view>;
    Value value {""};
};

// Helper function to get token type name as string
std::string token_type_to_string(TokenType type);

// Helper function to get token value as string
std::string token_value_to_string(const Token& token);

// Helper function to get complete token representation as string
std::string token_to_string(const Token& token);
}
