#include "checker.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace minic {

namespace {

class Checker {
public:
    void run(Program& program) {
        // Collect signatures first, so functions can call ones defined later.
        for (const auto& function : program.functions) {
            if (function.params.size() > static_cast<std::size_t>(kMaxParams)) {
                throw CompileError(function.location, "function '" + function.name + "' has more than 8 parameters");
            }
            if (!arity_.emplace(function.name, function.params.size()).second) {
                throw CompileError(function.location, "function '" + function.name + "' is defined twice");
            }
        }
        const auto main_function = arity_.find("main");
        if (main_function == arity_.end()) {
            throw CompileError(Location{}, "program has no 'main' function");
        }
        if (main_function->second != 0) {
            throw CompileError(Location{}, "'main' must not take parameters");
        }
        for (auto& function : program.functions) {
            check_function(function);
        }
    }

private:
    void check_function(Function& function) {
        scopes_.clear();
        scopes_.emplace_back();
        next_slot_ = 0;
        loop_depth_ = 0;
        for (const auto& param : function.params) {
            declare(param, function.location);
        }
        // The body shares the parameters' scope, as in C.
        for (auto& stmt : function.body->body) {
            check_stmt(*stmt);
        }
        function.slot_count = next_slot_;
    }

    int declare(const std::string& name, Location where) {
        if (!scopes_.back().emplace(name, next_slot_).second) {
            throw CompileError(where, "'" + name + "' is already declared in this scope");
        }
        return next_slot_++;
    }

    int resolve(const std::string& name, Location where) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) {
                return found->second;
            }
        }
        throw CompileError(where, "'" + name + "' is not declared");
    }

    void check_stmt(Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::Declare:
                // Check the value first, so 'int x = x;' can't read the new variable.
                if (stmt.expr) {
                    check_expr(*stmt.expr);
                }
                stmt.slot = declare(stmt.name, stmt.location);
                break;
            case Stmt::Kind::Assign:
                check_expr(*stmt.expr);
                stmt.slot = resolve(stmt.name, stmt.location);
                break;
            case Stmt::Kind::If:
                check_expr(*stmt.expr);
                check_branch(*stmt.then_branch);
                if (stmt.else_branch) {
                    check_branch(*stmt.else_branch);
                }
                break;
            case Stmt::Kind::While:
                check_expr(*stmt.expr);
                ++loop_depth_;
                check_branch(*stmt.then_branch);
                --loop_depth_;
                break;
            case Stmt::Kind::Return:
                if (stmt.expr) {
                    check_expr(*stmt.expr);
                }
                break;
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue:
                if (loop_depth_ == 0) {
                    throw CompileError(stmt.location, std::string("'") +
                        (stmt.kind == Stmt::Kind::Break ? "break" : "continue") + "' outside a loop");
                }
                break;
            case Stmt::Kind::Print:
            case Stmt::Kind::ExprStmt:
                check_expr(*stmt.expr);
                break;
            case Stmt::Kind::Block:
                check_branch(stmt);
                break;
        }
    }

    // A branch gets its own scope, including a lone declaration with no braces.
    void check_branch(Stmt& stmt) {
        scopes_.emplace_back();
        if (stmt.kind == Stmt::Kind::Block) {
            for (auto& inner : stmt.body) {
                check_stmt(*inner);
            }
        } else {
            check_stmt(stmt);
        }
        scopes_.pop_back();
    }

    void check_expr(Expr& expr) {
        switch (expr.kind) {
            case Expr::Kind::Number:
                break;
            case Expr::Kind::Variable:
                expr.slot = resolve(expr.name, expr.location);
                break;
            case Expr::Kind::Unary:
                check_expr(*expr.left);
                break;
            case Expr::Kind::Binary:
                check_expr(*expr.left);
                check_expr(*expr.right);
                break;
            case Expr::Kind::Call: {
                const auto found = arity_.find(expr.name);
                if (found == arity_.end()) {
                    throw CompileError(expr.location, "function '" + expr.name + "' is not defined");
                }
                if (found->second != expr.args.size()) {
                    throw CompileError(expr.location, "function '" + expr.name + "' takes " +
                        std::to_string(found->second) + " argument(s), got " + std::to_string(expr.args.size()));
                }
                for (auto& arg : expr.args) {
                    check_expr(*arg);
                }
                break;
            }
        }
    }

    std::unordered_map<std::string, std::size_t> arity_;
    std::vector<std::unordered_map<std::string, int>> scopes_;
    int next_slot_ = 0;
    int loop_depth_ = 0;
};

}  // namespace

void check(Program& program) { Checker().run(program); }

}  // namespace minic
