// The syntax tree, shared by every compiler stage.
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace minic {

struct Location {
    int line = 1;
    int column = 1;
};

class CompileError : public std::runtime_error {
public:
    CompileError(Location where, const std::string& message) : std::runtime_error(message), location(where) {}
    Location location;
};

enum class BinaryOp { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or };
enum class UnaryOp { Neg, Not };

struct Expr {
    enum class Kind { Number, Variable, Unary, Binary, Call };

    Kind kind = Kind::Number;
    Location location;
    std::int64_t value = 0;                    // Number
    std::string name;                          // Variable, Call
    int slot = -1;                             // Variable: stack slot, set by the checker
    UnaryOp unary_op = UnaryOp::Neg;           // Unary
    BinaryOp binary_op = BinaryOp::Add;        // Binary
    std::unique_ptr<Expr> left;                // Binary, and the operand of Unary
    std::unique_ptr<Expr> right;               // Binary
    std::vector<std::unique_ptr<Expr>> args;   // Call
};

struct Stmt {
    enum class Kind { Declare, Assign, If, While, Return, Break, Continue, Print, Block, ExprStmt };

    Kind kind = Kind::Block;
    Location location;
    std::string name;                          // Declare, Assign
    int slot = -1;                             // Declare, Assign: set by the checker
    std::unique_ptr<Expr> expr;                // value or condition; may be null for Declare and Return
    std::unique_ptr<Stmt> then_branch;         // If, and the body of While
    std::unique_ptr<Stmt> else_branch;         // If; may be null
    std::vector<std::unique_ptr<Stmt>> body;   // Block
};

struct Function {
    std::string name;
    Location location;
    std::vector<std::string> params;
    std::unique_ptr<Stmt> body;
    int slot_count = 0;  // Set by the checker. Parameters take the first slots.
};

struct Program {
    std::vector<Function> functions;
};

}  // namespace minic
