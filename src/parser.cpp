#include "parser.hpp"

#include <array>

#include "lexer.hpp"

namespace minic {

namespace {

// Deep nesting would otherwise overflow the compiler's own stack on hostile input.
constexpr int kMaxNesting = 200;

struct BinaryLevel {
    TokenKind token;
    BinaryOp op;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program program() {
        Program result;
        while (!at(TokenKind::End)) {
            result.functions.push_back(function());
        }
        return result;
    }

private:
    class NestingGuard {
    public:
        explicit NestingGuard(Parser& parser) : parser_(parser) {
            if (++parser_.nesting_ > kMaxNesting) {
                throw CompileError(parser_.current().location, "code is nested too deeply");
            }
        }
        ~NestingGuard() { --parser_.nesting_; }
        NestingGuard(const NestingGuard&) = delete;
        NestingGuard& operator=(const NestingGuard&) = delete;

    private:
        Parser& parser_;
    };

    const Token& current() const { return tokens_[position_]; }
    bool at(TokenKind kind) const { return current().kind == kind; }
    const Token& next_token() const { return tokens_[position_ + 1 < tokens_.size() ? position_ + 1 : position_]; }

    Token take() {
        Token token = current();
        if (position_ + 1 < tokens_.size()) {
            ++position_;
        }
        return token;
    }

    bool accept(TokenKind kind) {
        if (at(kind)) {
            take();
            return true;
        }
        return false;
    }

    Token expect(TokenKind kind, const char* what) {
        if (!at(kind)) {
            const std::string found = at(TokenKind::End) ? "end of file" : "'" + describe(current()) + "'";
            throw CompileError(current().location, std::string("expected ") + what + ", found " + found);
        }
        return take();
    }

    static std::string describe(const Token& token) {
        if (!token.text.empty()) {
            return token.text;
        }
        switch (token.kind) {
            case TokenKind::LParen: return "(";
            case TokenKind::RParen: return ")";
            case TokenKind::LBrace: return "{";
            case TokenKind::RBrace: return "}";
            case TokenKind::Comma: return ",";
            case TokenKind::Semicolon: return ";";
            case TokenKind::Assign: return "=";
            default: return "operator";
        }
    }

    Function function() {
        Function result;
        result.location = current().location;
        expect(TokenKind::KwInt, "'int' to start a function");
        result.name = expect(TokenKind::Identifier, "a function name").text;
        expect(TokenKind::LParen, "'('");
        if (!at(TokenKind::RParen)) {
            do {
                expect(TokenKind::KwInt, "'int' before a parameter");
                result.params.push_back(expect(TokenKind::Identifier, "a parameter name").text);
            } while (accept(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "')'");
        result.body = block();
        return result;
    }

    std::unique_ptr<Stmt> make_stmt(Stmt::Kind kind) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = kind;
        stmt->location = current().location;
        return stmt;
    }

    std::unique_ptr<Stmt> block() {
        const NestingGuard guard(*this);
        auto result = make_stmt(Stmt::Kind::Block);
        expect(TokenKind::LBrace, "'{'");
        while (!at(TokenKind::RBrace)) {
            if (at(TokenKind::End)) {
                throw CompileError(current().location, "expected '}', found end of file");
            }
            result->body.push_back(statement());
        }
        take();
        return result;
    }

    std::unique_ptr<Stmt> statement() {
        const NestingGuard guard(*this);
        switch (current().kind) {
            case TokenKind::LBrace:
                return block();
            case TokenKind::KwInt: {
                auto stmt = make_stmt(Stmt::Kind::Declare);
                take();
                stmt->name = expect(TokenKind::Identifier, "a variable name").text;
                if (accept(TokenKind::Assign)) {
                    stmt->expr = expression();
                }
                expect(TokenKind::Semicolon, "';'");
                return stmt;
            }
            case TokenKind::KwIf: {
                auto stmt = make_stmt(Stmt::Kind::If);
                take();
                expect(TokenKind::LParen, "'('");
                stmt->expr = expression();
                expect(TokenKind::RParen, "')'");
                stmt->then_branch = statement();
                if (accept(TokenKind::KwElse)) {
                    stmt->else_branch = statement();
                }
                return stmt;
            }
            case TokenKind::KwWhile: {
                auto stmt = make_stmt(Stmt::Kind::While);
                take();
                expect(TokenKind::LParen, "'('");
                stmt->expr = expression();
                expect(TokenKind::RParen, "')'");
                stmt->then_branch = statement();
                return stmt;
            }
            case TokenKind::KwReturn: {
                auto stmt = make_stmt(Stmt::Kind::Return);
                take();
                if (!at(TokenKind::Semicolon)) {
                    stmt->expr = expression();
                }
                expect(TokenKind::Semicolon, "';'");
                return stmt;
            }
            case TokenKind::KwBreak:
            case TokenKind::KwContinue: {
                auto stmt = make_stmt(at(TokenKind::KwBreak) ? Stmt::Kind::Break : Stmt::Kind::Continue);
                take();
                expect(TokenKind::Semicolon, "';'");
                return stmt;
            }
            case TokenKind::KwPrint: {
                auto stmt = make_stmt(Stmt::Kind::Print);
                take();
                expect(TokenKind::LParen, "'('");
                stmt->expr = expression();
                expect(TokenKind::RParen, "')'");
                expect(TokenKind::Semicolon, "';'");
                return stmt;
            }
            default:
                break;
        }
        if (at(TokenKind::Identifier) && next_token().kind == TokenKind::Assign) {
            auto stmt = make_stmt(Stmt::Kind::Assign);
            stmt->name = take().text;
            take();
            stmt->expr = expression();
            expect(TokenKind::Semicolon, "';'");
            return stmt;
        }
        auto stmt = make_stmt(Stmt::Kind::ExprStmt);
        stmt->expr = expression();
        expect(TokenKind::Semicolon, "';'");
        return stmt;
    }

    std::unique_ptr<Expr> expression() { return binary(0); }

    // Each entry is one precedence level, lowest first. All are left-associative.
    std::unique_ptr<Expr> binary(std::size_t level) {
        static const std::array<std::vector<BinaryLevel>, 6> kLevels = {{
            {{TokenKind::OrOr, BinaryOp::Or}},
            {{TokenKind::AndAnd, BinaryOp::And}},
            {{TokenKind::EqEq, BinaryOp::Eq}, {TokenKind::NotEq, BinaryOp::Ne}},
            {{TokenKind::Less, BinaryOp::Lt}, {TokenKind::LessEq, BinaryOp::Le},
             {TokenKind::Greater, BinaryOp::Gt}, {TokenKind::GreaterEq, BinaryOp::Ge}},
            {{TokenKind::Plus, BinaryOp::Add}, {TokenKind::Minus, BinaryOp::Sub}},
            {{TokenKind::Star, BinaryOp::Mul}, {TokenKind::Slash, BinaryOp::Div}, {TokenKind::Percent, BinaryOp::Mod}},
        }};
        if (level == kLevels.size()) {
            return unary();
        }
        auto left = binary(level + 1);
        while (true) {
            const BinaryLevel* match = nullptr;
            for (const auto& candidate : kLevels[level]) {
                if (at(candidate.token)) {
                    match = &candidate;
                }
            }
            if (match == nullptr) {
                return left;
            }
            auto node = std::make_unique<Expr>();
            node->kind = Expr::Kind::Binary;
            node->location = take().location;
            node->binary_op = match->op;
            node->left = std::move(left);
            node->right = binary(level + 1);
            left = std::move(node);
        }
    }

    std::unique_ptr<Expr> unary() {
        const NestingGuard guard(*this);
        if (at(TokenKind::Minus) || at(TokenKind::Bang)) {
            auto node = std::make_unique<Expr>();
            node->kind = Expr::Kind::Unary;
            node->location = current().location;
            node->unary_op = at(TokenKind::Minus) ? UnaryOp::Neg : UnaryOp::Not;
            take();
            node->left = unary();
            return node;
        }
        return primary();
    }

    std::unique_ptr<Expr> primary() {
        auto node = std::make_unique<Expr>();
        node->location = current().location;
        if (at(TokenKind::Number)) {
            node->kind = Expr::Kind::Number;
            node->value = take().value;
            return node;
        }
        if (at(TokenKind::Identifier)) {
            node->name = take().text;
            if (!accept(TokenKind::LParen)) {
                node->kind = Expr::Kind::Variable;
                return node;
            }
            node->kind = Expr::Kind::Call;
            if (!at(TokenKind::RParen)) {
                do {
                    node->args.push_back(expression());
                } while (accept(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "')'");
            return node;
        }
        if (accept(TokenKind::LParen)) {
            const NestingGuard guard(*this);
            auto inner = expression();
            expect(TokenKind::RParen, "')'");
            return inner;
        }
        const std::string found = at(TokenKind::End) ? "end of file" : "'" + describe(current()) + "'";
        throw CompileError(current().location, "expected an expression, found " + found);
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    int nesting_ = 0;
};

}  // namespace

Program parse(std::string_view source) { return Parser(tokenize(source)).program(); }

}  // namespace minic
