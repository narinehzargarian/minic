#include "lexer.hpp"

#include <cctype>
#include <limits>
#include <unordered_map>

namespace minic {

namespace {

const std::unordered_map<std::string, TokenKind> kKeywords = {
    {"int", TokenKind::KwInt},           {"if", TokenKind::KwIf},
    {"else", TokenKind::KwElse},         {"while", TokenKind::KwWhile},
    {"return", TokenKind::KwReturn},     {"break", TokenKind::KwBreak},
    {"continue", TokenKind::KwContinue}, {"print", TokenKind::KwPrint},
};

bool is_identifier_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_identifier_part(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

}  // namespace

std::vector<Token> tokenize(std::string_view source) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    Location here;

    const auto advance = [&](std::size_t count = 1) {
        for (std::size_t n = 0; n < count && i < source.size(); ++n, ++i) {
            if (source[i] == '\n') {
                ++here.line;
                here.column = 1;
            } else {
                ++here.column;
            }
        }
    };
    const auto peek = [&](std::size_t offset = 0) { return i + offset < source.size() ? source[i + offset] : '\0'; };

    while (i < source.size()) {
        const char c = peek();
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            advance();
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            while (i < source.size() && peek() != '\n') {
                advance();
            }
            continue;
        }

        Token token;
        token.location = here;

        if (is_digit(c)) {
            std::uint64_t value = 0;
            const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            while (is_digit(peek())) {
                const auto digit = static_cast<std::uint64_t>(peek() - '0');
                if (value > (limit - digit) / 10) {
                    throw CompileError(token.location, "number is too large for a 64-bit int");
                }
                value = value * 10 + digit;
                token.text += peek();
                advance();
            }
            if (is_identifier_start(peek())) {
                throw CompileError(token.location, "invalid number '" + token.text + peek() + "'");
            }
            token.kind = TokenKind::Number;
            token.value = static_cast<std::int64_t>(value);
        } else if (is_identifier_start(c)) {
            while (is_identifier_part(peek())) {
                token.text += peek();
                advance();
            }
            const auto keyword = kKeywords.find(token.text);
            token.kind = keyword != kKeywords.end() ? keyword->second : TokenKind::Identifier;
        } else {
            const char next = peek(1);
            const auto two = [&](TokenKind kind) {
                token.kind = kind;
                advance(2);
            };
            const auto one = [&](TokenKind kind) {
                token.kind = kind;
                advance();
            };
            switch (c) {
                case '(': one(TokenKind::LParen); break;
                case ')': one(TokenKind::RParen); break;
                case '{': one(TokenKind::LBrace); break;
                case '}': one(TokenKind::RBrace); break;
                case ',': one(TokenKind::Comma); break;
                case ';': one(TokenKind::Semicolon); break;
                case '+': one(TokenKind::Plus); break;
                case '-': one(TokenKind::Minus); break;
                case '*': one(TokenKind::Star); break;
                case '/': one(TokenKind::Slash); break;
                case '%': one(TokenKind::Percent); break;
                case '=': next == '=' ? two(TokenKind::EqEq) : one(TokenKind::Assign); break;
                case '!': next == '=' ? two(TokenKind::NotEq) : one(TokenKind::Bang); break;
                case '<': next == '=' ? two(TokenKind::LessEq) : one(TokenKind::Less); break;
                case '>': next == '=' ? two(TokenKind::GreaterEq) : one(TokenKind::Greater); break;
                case '&':
                    if (next != '&') throw CompileError(here, "expected '&&'");
                    two(TokenKind::AndAnd);
                    break;
                case '|':
                    if (next != '|') throw CompileError(here, "expected '||'");
                    two(TokenKind::OrOr);
                    break;
                default:
                    throw CompileError(here, std::string("unexpected character '") + c + "'");
            }
        }
        tokens.push_back(std::move(token));
    }

    Token end;
    end.location = here;
    tokens.push_back(std::move(end));
    return tokens;
}

}  // namespace minic
