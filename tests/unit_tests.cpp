#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "checker.hpp"
#include "lexer.hpp"
#include "optimizer.hpp"
#include "parser.hpp"

using namespace minic;

namespace {

Program compile_front(const std::string& source) {
    Program program = parse(source);
    check(program);
    return program;
}

// The error message and location from compiling a program, or "" if it compiled.
std::string error_of(const std::string& source) {
    try {
        compile_front(source);
    } catch (const CompileError& error) {
        return std::to_string(error.location.line) + ":" + std::to_string(error.location.column) + " " + error.what();
    }
    return "";
}

// Folds 'return <text>;' and returns the resulting expression.
Program fold_return(const std::string& text) {
    Program program = compile_front("int main() { return " + text + "; }");
    optimize(program);
    return program;
}

const Expr& returned(const Program& program) { return *program.functions[0].body->body[0]->expr; }

}  // namespace

TEST(Lexer, RecognizesTokensAndTracksLocations) {
    const auto tokens = tokenize("int x\n  <= 42 // comment\n&&");
    ASSERT_EQ(tokens.size(), 6U);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwInt);
    EXPECT_EQ(tokens[1].text, "x");
    EXPECT_EQ(tokens[2].kind, TokenKind::LessEq);
    EXPECT_EQ(tokens[2].location.line, 2);
    EXPECT_EQ(tokens[2].location.column, 3);
    EXPECT_EQ(tokens[3].value, 42);
    EXPECT_EQ(tokens[4].kind, TokenKind::AndAnd);
    EXPECT_EQ(tokens[4].location.line, 3);
    EXPECT_EQ(tokens[5].kind, TokenKind::End);
}

TEST(Lexer, AcceptsLargestIntAndRejectsOverflow) {
    EXPECT_EQ(tokenize("9223372036854775807")[0].value, std::numeric_limits<std::int64_t>::max());
    EXPECT_THROW(tokenize("9223372036854775808"), CompileError);
    EXPECT_THROW(tokenize("99999999999999999999999"), CompileError);
}

TEST(Lexer, RejectsBadInput) {
    EXPECT_THROW(tokenize("@"), CompileError);
    EXPECT_THROW(tokenize("a & b"), CompileError);
    EXPECT_THROW(tokenize("12abc"), CompileError);
}

TEST(Parser, MultiplicationBindsTighterThanAddition) {
    const Program program = parse("int main() { return 1 + 2 * 3; }");
    const Expr& expr = returned(program);
    EXPECT_EQ(expr.binary_op, BinaryOp::Add);
    EXPECT_EQ(expr.right->binary_op, BinaryOp::Mul);
}

TEST(Parser, SubtractionIsLeftAssociative) {
    const Program program = parse("int main() { return 10 - 4 - 3; }");
    const Expr& expr = returned(program);
    EXPECT_EQ(expr.left->kind, Expr::Kind::Binary);
    EXPECT_EQ(expr.right->value, 3);
}

TEST(Parser, ParenthesesOverridePrecedence) {
    const Program program = parse("int main() { return (1 + 2) * 3; }");
    EXPECT_EQ(returned(program).binary_op, BinaryOp::Mul);
}

TEST(Parser, ReportsLocationOfSyntaxErrors) {
    EXPECT_EQ(error_of("int main() {\n  int x = ;\n}"), "2:11 expected an expression, found ';'");
    EXPECT_EQ(error_of("int main() {"), "1:13 expected '}', found end of file");
}

TEST(Parser, RejectsExtremeNestingWithoutCrashing) {
    std::string deep = "int main() { return ";
    deep += std::string(100000, '(');
    EXPECT_NE(error_of(deep).find("nested too deeply"), std::string::npos);
    std::string negatives = "int main() { return ";
    negatives += std::string(100000, '-');
    EXPECT_NE(error_of(negatives).find("nested too deeply"), std::string::npos);
}

TEST(Checker, FindsNameAndArityErrors) {
    EXPECT_NE(error_of("int main() { return y; }").find("'y' is not declared"), std::string::npos);
    EXPECT_NE(error_of("int main() { int x; int x; }").find("already declared"), std::string::npos);
    EXPECT_NE(error_of("int main() { return f(); }").find("not defined"), std::string::npos);
    EXPECT_NE(error_of("int f(int a) { return a; } int main() { return f(); }").find("takes 1"), std::string::npos);
    EXPECT_NE(error_of("int main() { break; }").find("outside a loop"), std::string::npos);
    EXPECT_NE(error_of("int f() { return 1; }").find("no 'main'"), std::string::npos);
    EXPECT_NE(error_of("int f(){} int f(){} int main(){}").find("defined twice"), std::string::npos);
    EXPECT_NE(error_of("int main(int a) {}").find("must not take"), std::string::npos);
    EXPECT_NE(error_of("int f(int a,int b,int c,int d,int e,int f2,int g,int h,int i){} int main(){}").find("8 parameters"), std::string::npos);
}

TEST(Checker, ScopesAndShadowing) {
    EXPECT_EQ(error_of("int main() { int x = 1; { int x = 2; } return x; }"), "");
    EXPECT_NE(error_of("int main() { { int x = 1; } return x; }").find("not declared"), std::string::npos);
    EXPECT_NE(error_of("int main() { int x = x; }").find("not declared"), std::string::npos);
    EXPECT_NE(error_of("int f(int a) { int a; } int main() {}").find("already declared"), std::string::npos);
}

TEST(Checker, InnerShadowUsesItsOwnSlot) {
    const Program program = compile_front("int main() { int x = 1; { int x = 2; } return x; }");
    const auto& body = program.functions[0].body->body;
    EXPECT_NE(body[0]->slot, body[1]->body[0]->slot);
    EXPECT_EQ(body[2]->expr->slot, body[0]->slot);
    EXPECT_EQ(program.functions[0].slot_count, 2);
}

TEST(Checker, FunctionsCanCallOnesDefinedLater) {
    EXPECT_EQ(error_of("int main() { return later(); } int later() { return 1; }"), "");
}

TEST(Optimizer, FoldsArithmeticAndComparisons) {
    EXPECT_EQ(returned(fold_return("2 + 3 * 4")).value, 14);
    EXPECT_EQ(returned(fold_return("-(7 % 3)")).value, -1);
    EXPECT_EQ(returned(fold_return("3 < 4 == 1")).value, 1);
    EXPECT_EQ(returned(fold_return("!5")).value, 0);
}

TEST(Optimizer, TruncatesDivisionTowardZero) {
    EXPECT_EQ(returned(fold_return("-7 / 2")).value, -3);
    EXPECT_EQ(returned(fold_return("-7 % 2")).value, -1);
}

TEST(Optimizer, WrapsOnOverflowLikeTheHardware) {
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    EXPECT_EQ(returned(fold_return("9223372036854775807 + 1")).value, kMin);
    EXPECT_EQ(returned(fold_return("(-9223372036854775807 - 1) / -1")).value, kMin);
    EXPECT_EQ(returned(fold_return("(-9223372036854775807 - 1) % -1")).value, 0);
}

TEST(Optimizer, LeavesDivisionByZeroForRuntime) {
    const Program program = fold_return("1 / 0");
    EXPECT_EQ(returned(program).kind, Expr::Kind::Binary);
}

TEST(Optimizer, ShortCircuitDropsTheUnreachableSide) {
    Program program = compile_front("int f() { return 1; } int main() { return 0 && f(); }");
    optimize(program);
    const Expr& result = *program.functions[1].body->body[0]->expr;
    EXPECT_EQ(result.kind, Expr::Kind::Number);
    EXPECT_EQ(result.value, 0);
}

TEST(Optimizer, KeepsCallsThatMustStillRun) {
    Program program = compile_front("int f() { return 1; } int main() { return f() && 0; }");
    optimize(program);
    EXPECT_EQ(program.functions[1].body->body[0]->expr->kind, Expr::Kind::Binary);
}

TEST(Optimizer, RemovesBranchesThatCanNeverRun) {
    Program program = compile_front("int main() { if (0) { return 1; } else { return 2; } while (1 - 1) { return 3; } }");
    optimize(program);
    const auto& body = program.functions[0].body->body;
    ASSERT_EQ(body[0]->kind, Stmt::Kind::Block);
    ASSERT_EQ(body[0]->body.size(), 1U);
    EXPECT_EQ(body[0]->body[0]->body[0]->expr->value, 2);
    EXPECT_EQ(body[1]->kind, Stmt::Kind::Block);
    EXPECT_TRUE(body[1]->body.empty());
}
