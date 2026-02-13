#pragma once

#include <cstdint>
#include <string_view>
#include "variant.h"
#include "source.h"

namespace acu {
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
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    Location location;
    utils::Variant<bool, std::int64_t, double, char32_t, std::string_view> value{false}; // Initialize with a default value
    
    // Default constructor
    Token() : type(TokenType::EndOfFile), location(), value(false) {}
    
    // Constructor with type and location
    Token(TokenType t, Location loc) : type(t), location(loc), value(false) {}
    
    // Constructor with type, location, and value
    template<typename T>
    Token(TokenType t, Location loc, T val) : type(t), location(loc), value(val) {}
};
}