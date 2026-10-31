#include "codegen.hpp"

#include <cstdint>
#include <sstream>
#include <vector>

namespace minic {

namespace {

// Every expression leaves its result in x0. While evaluating a binary operation or a call,
// intermediate values wait on the stack, pushed 16 bytes at a time because ARM64 requires sp to
// stay 16-byte aligned. Variables live below the frame pointer, at x29 - 8 * (slot + 1).
class Generator {
public:
    std::string run(const Program& program) {
        out_ << ".section __TEXT,__cstring\n"
             << "Lprint_format:\n"
             << "  .asciz \"%lld\\n\"\n"
             << ".text\n";
        for (const auto& function : program.functions) {
            emit_function(function);
        }
        return out_.str();
    }

private:
    struct Loop {
        int continue_label;
        int break_label;
    };

    int new_label() { return next_label_++; }
    void label(int id) { out_ << "L" << id << ":\n"; }
    void line(const std::string& text) { out_ << "  " << text << "\n"; }
    void push(const std::string& reg) { line("str " + reg + ", [sp, #-16]!"); }
    void pop(const std::string& reg) { line("ldr " + reg + ", [sp], #16"); }

    // User functions get a prefix, so a function named 'printf' can't collide with libc.
    static std::string symbol(const std::string& name) { return name == "main" ? "_main" : "_mc_" + name; }

    // Loads any 64-bit value, 16 bits at a time.
    void load_immediate(const std::string& reg, std::int64_t value) {
        auto bits = static_cast<std::uint64_t>(value);
        line("movz " + reg + ", #" + std::to_string(bits & 0xFFFF));
        for (int shift = 16; shift < 64; shift += 16) {
            const auto part = (bits >> shift) & 0xFFFF;
            if (part != 0) {
                line("movk " + reg + ", #" + std::to_string(part) + ", lsl #" + std::to_string(shift));
            }
        }
    }

    // Puts the address of a slot in x9. A register offset handles frames of any size,
    // unlike the small immediate that load and store instructions accept.
    void slot_address(int slot) {
        load_immediate("x9", 8 * (static_cast<std::int64_t>(slot) + 1));
        line("sub x9, x29, x9");
    }

    void emit_function(const Function& function) {
        return_label_ = new_label();
        const std::string name = symbol(function.name);
        out_ << ".globl " << name << "\n.p2align 2\n" << name << ":\n";
        line("stp x29, x30, [sp, #-16]!");
        line("mov x29, sp");
        const std::int64_t frame = ((static_cast<std::int64_t>(function.slot_count) * 8) + 15) / 16 * 16;
        if (frame > 0) {
            load_immediate("x9", frame);
            line("sub sp, sp, x9");
        }
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            slot_address(static_cast<int>(i));
            line("str x" + std::to_string(i) + ", [x9]");
        }
        emit_stmt(*function.body);
        line("movz x0, #0");  // Falling off the end returns 0.
        label(return_label_);
        line("mov sp, x29");
        line("ldp x29, x30, [sp], #16");
        line("ret");
    }

    void emit_stmt(const Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::Block:
                for (const auto& inner : stmt.body) {
                    emit_stmt(*inner);
                }
                break;
            case Stmt::Kind::Declare:
                if (stmt.expr) {
                    emit_expr(*stmt.expr);
                } else {
                    line("movz x0, #0");  // Declared variables always start at 0.
                }
                slot_address(stmt.slot);
                line("str x0, [x9]");
                break;
            case Stmt::Kind::Assign:
                emit_expr(*stmt.expr);
                slot_address(stmt.slot);
                line("str x0, [x9]");
                break;
            case Stmt::Kind::If: {
                const int else_label = new_label();
                const int end_label = new_label();
                emit_expr(*stmt.expr);
                line("cbz x0, L" + std::to_string(else_label));
                emit_stmt(*stmt.then_branch);
                line("b L" + std::to_string(end_label));
                label(else_label);
                if (stmt.else_branch) {
                    emit_stmt(*stmt.else_branch);
                }
                label(end_label);
                break;
            }
            case Stmt::Kind::While: {
                const int start_label = new_label();
                const int end_label = new_label();
                label(start_label);
                emit_expr(*stmt.expr);
                line("cbz x0, L" + std::to_string(end_label));
                loops_.push_back({start_label, end_label});
                emit_stmt(*stmt.then_branch);
                loops_.pop_back();
                line("b L" + std::to_string(start_label));
                label(end_label);
                break;
            }
            case Stmt::Kind::Return:
                if (stmt.expr) {
                    emit_expr(*stmt.expr);
                } else {
                    line("movz x0, #0");
                }
                line("b L" + std::to_string(return_label_));
                break;
            case Stmt::Kind::Break:
                line("b L" + std::to_string(loops_.back().break_label));
                break;
            case Stmt::Kind::Continue:
                line("b L" + std::to_string(loops_.back().continue_label));
                break;
            case Stmt::Kind::Print:
                emit_expr(*stmt.expr);
                // On macOS, variadic arguments such as printf's go on the stack, not in registers.
                push("x0");
                line("adrp x0, Lprint_format@PAGE");
                line("add x0, x0, Lprint_format@PAGEOFF");
                line("bl _printf");
                line("add sp, sp, #16");
                break;
            case Stmt::Kind::ExprStmt:
                emit_expr(*stmt.expr);
                break;
        }
    }

    void emit_expr(const Expr& expr) {
        switch (expr.kind) {
            case Expr::Kind::Number:
                load_immediate("x0", expr.value);
                break;
            case Expr::Kind::Variable:
                slot_address(expr.slot);
                line("ldr x0, [x9]");
                break;
            case Expr::Kind::Unary:
                emit_expr(*expr.left);
                if (expr.unary_op == UnaryOp::Neg) {
                    line("neg x0, x0");
                } else {
                    line("cmp x0, #0");
                    line("cset x0, eq");
                }
                break;
            case Expr::Kind::Binary:
                emit_binary(expr);
                break;
            case Expr::Kind::Call:
                // Arguments may contain calls of their own, so they wait on the stack until all
                // are ready, and only then move into registers.
                for (const auto& arg : expr.args) {
                    emit_expr(*arg);
                    push("x0");
                }
                for (std::size_t i = expr.args.size(); i > 0; --i) {
                    pop("x" + std::to_string(i - 1));
                }
                line("bl " + symbol(expr.name));
                break;
        }
    }

    void emit_short_circuit(const Expr& expr) {
        const int short_label = new_label();
        const int end_label = new_label();
        const bool is_and = expr.binary_op == BinaryOp::And;
        emit_expr(*expr.left);
        line(std::string(is_and ? "cbz" : "cbnz") + " x0, L" + std::to_string(short_label));
        emit_expr(*expr.right);
        line("cmp x0, #0");
        line("cset x0, ne");
        line("b L" + std::to_string(end_label));
        label(short_label);
        line(is_and ? "movz x0, #0" : "movz x0, #1");
        label(end_label);
    }

    void emit_division_check() {
        const int ok_label = new_label();
        line("cbnz x0, L" + std::to_string(ok_label));
        line("bl _abort");  // ARM64 division by zero silently gives 0, so stop the program instead.
        label(ok_label);
    }

    void emit_compare(const char* condition) {
        line("cmp x1, x0");
        line(std::string("cset x0, ") + condition);
    }

    void emit_binary(const Expr& expr) {
        if (expr.binary_op == BinaryOp::And || expr.binary_op == BinaryOp::Or) {
            emit_short_circuit(expr);
            return;
        }
        emit_expr(*expr.left);
        push("x0");
        emit_expr(*expr.right);
        pop("x1");  // Now x1 is the left value and x0 is the right value.
        switch (expr.binary_op) {
            case BinaryOp::Add: line("add x0, x1, x0"); break;
            case BinaryOp::Sub: line("sub x0, x1, x0"); break;
            case BinaryOp::Mul: line("mul x0, x1, x0"); break;
            case BinaryOp::Div:
                emit_division_check();
                line("sdiv x0, x1, x0");
                break;
            case BinaryOp::Mod:
                emit_division_check();
                line("sdiv x2, x1, x0");
                line("msub x0, x2, x0, x1");
                break;
            case BinaryOp::Eq: emit_compare("eq"); break;
            case BinaryOp::Ne: emit_compare("ne"); break;
            case BinaryOp::Lt: emit_compare("lt"); break;
            case BinaryOp::Le: emit_compare("le"); break;
            case BinaryOp::Gt: emit_compare("gt"); break;
            case BinaryOp::Ge: emit_compare("ge"); break;
            case BinaryOp::And:
            case BinaryOp::Or:
                break;  // Handled above.
        }
    }

    std::ostringstream out_;
    std::vector<Loop> loops_;
    int next_label_ = 0;
    int return_label_ = 0;
};

}  // namespace

std::string generate(const Program& program) { return Generator().run(program); }

}  // namespace minic
