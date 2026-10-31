// Turns source text into tokens.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ast.hpp"

namespace minic {

enum class TokenKind {
    Number, Identifier,
    KwInt, KwIf, KwElse, KwWhile, KwReturn, KwBreak, KwContinue, KwPrint,
    LParen, RParen, LBrace, RBrace, Comma, Semicolon, Assign,
    Plus, Minus, Star, Slash, Percent, Bang,
    EqEq, NotEq, Less, LessEq, Greater, GreaterEq, AndAnd, OrOr,
    End,
};

struct Token {
    TokenKind kind = TokenKind::End;
    Location location;
    std::string text;
    std::int64_t value = 0;
};

std::vector<Token> tokenize(std::string_view source);

}  // namespace minic
