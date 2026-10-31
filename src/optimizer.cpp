#include "optimizer.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace minic {

namespace {

// Wraps on overflow, exactly as the generated code does. Signed overflow in C++ itself would be
// undefined behavior, so the arithmetic is done on unsigned values.
std::int64_t wrap_add(std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b)); }
std::int64_t wrap_sub(std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b)); }
std::int64_t wrap_mul(std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b)); }

std::optional<std::int64_t> evaluate(BinaryOp op, std::int64_t a, std::int64_t b) {
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    switch (op) {
        case BinaryOp::Add: return wrap_add(a, b);
        case BinaryOp::Sub: return wrap_sub(a, b);
        case BinaryOp::Mul: return wrap_mul(a, b);
        case BinaryOp::Div:
            if (b == 0) return std::nullopt;  // Left for the runtime check, in case it's never reached.
            if (a == kMin && b == -1) return kMin;  // ARM64's sdiv gives this result.
            return a / b;
        case BinaryOp::Mod:
            if (b == 0) return std::nullopt;
            if (a == kMin && b == -1) return 0;
            return a % b;
        case BinaryOp::Eq: return a == b ? 1 : 0;
        case BinaryOp::Ne: return a != b ? 1 : 0;
        case BinaryOp::Lt: return a < b ? 1 : 0;
        case BinaryOp::Le: return a <= b ? 1 : 0;
        case BinaryOp::Gt: return a > b ? 1 : 0;
        case BinaryOp::Ge: return a >= b ? 1 : 0;
        case BinaryOp::And: return a != 0 && b != 0 ? 1 : 0;
        case BinaryOp::Or: return a != 0 || b != 0 ? 1 : 0;
    }
    return std::nullopt;
}

void make_number(Expr& expr, std::int64_t value) {
    expr.kind = Expr::Kind::Number;
    expr.value = value;
    expr.left.reset();
    expr.right.reset();
}

bool is_number(const Expr& expr) { return expr.kind == Expr::Kind::Number; }

void fold(Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::Number:
        case Expr::Kind::Variable:
            break;
        case Expr::Kind::Call:
            for (auto& arg : expr.args) {
                fold(*arg);
            }
            break;
        case Expr::Kind::Unary:
            fold(*expr.left);
            if (is_number(*expr.left)) {
                const std::int64_t value = expr.left->value;
                make_number(expr, expr.unary_op == UnaryOp::Neg ? wrap_sub(0, value) : (value == 0 ? 1 : 0));
            }
            break;
        case Expr::Kind::Binary:
            fold(*expr.left);
            fold(*expr.right);
            // A known left side can decide && or || alone. The right side never runs at all,
            // so dropping it keeps the program's behavior, including any calls it contains.
            if (is_number(*expr.left)) {
                const bool left_true = expr.left->value != 0;
                if (expr.binary_op == BinaryOp::And && !left_true) {
                    make_number(expr, 0);
                    break;
                }
                if (expr.binary_op == BinaryOp::Or && left_true) {
                    make_number(expr, 1);
                    break;
                }
            }
            if (is_number(*expr.left) && is_number(*expr.right)) {
                if (const auto value = evaluate(expr.binary_op, expr.left->value, expr.right->value)) {
                    make_number(expr, *value);
                }
            }
            break;
    }
}

void make_empty(Stmt& stmt) {
    stmt.kind = Stmt::Kind::Block;
    stmt.expr.reset();
    stmt.then_branch.reset();
    stmt.else_branch.reset();
    stmt.body.clear();
}

void optimize_stmt(Stmt& stmt) {
    if (stmt.expr) {
        fold(*stmt.expr);
    }
    if (stmt.then_branch) {
        optimize_stmt(*stmt.then_branch);
    }
    if (stmt.else_branch) {
        optimize_stmt(*stmt.else_branch);
    }
    for (auto& inner : stmt.body) {
        optimize_stmt(*inner);
    }

    if (stmt.kind == Stmt::Kind::If && is_number(*stmt.expr)) {
        auto taken = std::move(stmt.expr->value != 0 ? stmt.then_branch : stmt.else_branch);
        make_empty(stmt);
        if (taken) {
            stmt.body.push_back(std::move(taken));
        }
    } else if (stmt.kind == Stmt::Kind::While && is_number(*stmt.expr) && stmt.expr->value == 0) {
        make_empty(stmt);
    }
}

}  // namespace

void optimize(Program& program) {
    for (auto& function : program.functions) {
        optimize_stmt(*function.body);
    }
}

}  // namespace minic
