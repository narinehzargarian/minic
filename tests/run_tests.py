#!/usr/bin/env python3
"""End-to-end tests. Compiles real programs, runs the native executables, and checks the results.

usage: run_tests.py <path-to-minic>
"""
import pathlib
import random
import subprocess
import sys
import tempfile

MIN, MAX = -(2**63), 2**63 - 1

# Each case: (name, source, expected printed values, expected exit code).
PROGRAMS = [
    ("return value", "int main() { return 42; }", [], 42),
    ("no return gives 0", "int main() { }", [], 0),
    ("print", "int main() { print(7); print(-7); }", [7, -7], 0),
    ("largest values", "int main() { print(9223372036854775807); print(-9223372036854775807 - 1); }", [MAX, MIN], 0),
    ("overflow wraps", "int main() { int x = 9223372036854775807; print(x + 1); }", [MIN], 0),
    ("precedence", "int main() { print(1 + 2 * 3); print((1 + 2) * 3); print(10 - 4 - 3); }", [7, 9, 3], 0),
    ("division truncates", "int main() { int a = -7; print(a / 2); print(a % 2); print(7 % -2); }", [-3, -1, 1], 0),
    ("comparisons", "int main() { int a = 2; print(a < 3); print(a > 3); print(a <= 2); print(a >= 3); print(a == 2); print(a != 2); }", [1, 0, 1, 0, 1, 0], 0),
    ("not and negate", "int main() { int a = 5; print(!a); print(!!a); print(-a); print(--a); }", [0, 1, -5, 5], 0),
    ("variables start at 0", "int main() { int x; print(x); }", [0], 0),
    ("shadowing", "int main() { int x = 1; { int x = 2; print(x); } print(x); }", [2, 1], 0),
    ("if else", "int main() { int x = 3; if (x > 2) { print(1); } else { print(2); } if (x > 5) print(3); else print(4); }", [1, 4], 0),
    ("while", "int main() { int i = 0; int t = 0; while (i < 5) { t = t + i; i = i + 1; } print(t); }", [10], 0),
    ("break", "int main() { int i = 0; while (1) { if (i == 3) { break; } i = i + 1; } print(i); }", [3], 0),
    ("continue", "int main() { int i = 0; int t = 0; while (i < 5) { i = i + 1; if (i % 2 == 0) { continue; } t = t + i; } print(t); }", [9], 0),
    ("nested loops break only the inner one",
     "int main() { int i = 0; int n = 0; while (i < 3) { int j = 0; while (1) { j = j + 1; if (j == 2) break; n = n + 1; } i = i + 1; } print(n); }",
     [3], 0),
    ("recursion", "int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); } int main() { print(fib(20)); }", [6765], 0),
    ("deep recursion", "int sum(int n) { if (n == 0) return 0; return n + sum(n - 1); } int main() { print(sum(50000)); }", [1250025000], 0),
    ("mutual recursion, defined later",
     "int main() { print(even(10)); print(even(7)); } int even(int n) { if (n == 0) return 1; return odd(n - 1); } int odd(int n) { if (n == 0) return 0; return even(n - 1); }",
     [1, 0], 0),
    ("eight arguments keep their order",
     "int f(int a, int b, int c, int d, int e, int g, int h, int i) { return a + 10*b + 100*c + 1000*d + 10000*e + 100000*g + 1000000*h + 10000000*i; } int main() { print(f(1,2,3,4,5,6,7,8)); }",
     [87654321], 0),
    ("calls inside arguments keep values",
     "int add(int a, int b) { return a + b; } int main() { print(add(add(1, 2), add(add(3, 4), 5))); }", [15], 0),
    ("call inside an expression keeps the left side",
     "int f(int x) { return x * 2; } int main() { print(100 - f(3) * f(4)); }", [52], 0),
    ("many locals, past small offsets",
     "int main() {" + "".join(f" int v{i} = {i};" for i in range(600)) + " print(v0 + v599); }", [599], 0),
    ("large constant", "int main() { print(1311768467463790320); print(-1311768467463790320); print(65536); }", [1311768467463790320, -1311768467463790320, 65536], 0),
    ("function named like libc", "int printf(int x) { return x + 1; } int main() { print(printf(1)); }", [2], 0),
    ("short circuit skips the right side",
     "int boom() { print(999); return 1; } int main() { print(0 && boom()); print(1 || boom()); int z = 0; print(z && boom()); print(1 && 5); print(0 || 7); }",
     [0, 1, 0, 1, 1], 0),
    ("right side runs when needed",
     "int yes() { print(5); return 1; } int main() { print(1 && yes()); print(0 || yes()); }", [5, 1, 5, 1], 0),
    ("exit code wraps at 256", "int main() { return 300; }", [], 44),
    ("unreached divide by zero is harmless", "int main() { if (0) { print(1 / 0); } print(2); }", [2], 0),
    ("primes example", (pathlib.Path(__file__).parent.parent / "examples/primes.mc").read_text(),
     [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47], 0),
]

# Each case: (source, expected "line:column: error: message" fragment).
ERRORS = [
    ("int main() { return 1 }", "1:23: error: expected ';', found '}'"),
    ("int main() {\n  print(x);\n}", "2:9: error: 'x' is not declared"),
    ("int main() { foo(); }", "1:14: error: function 'foo' is not defined"),
    ("int f(int a) { return a; }\nint main() { return f(1, 2); }", "2:21: error: function 'f' takes 1 argument(s), got 2"),
    ("int main() { break; }", "1:14: error: 'break' outside a loop"),
    ("int main() { return 99999999999999999999; }", "1:21: error: number is too large"),
    ("int helper() { return 1; }", "error: program has no 'main' function"),
    ("int main() { int x; int x; }", "1:21: error: 'x' is already declared"),
    ("int main() { return @; }", "1:21: error: unexpected character '@'"),
]


def to_signed(value):
    value &= 2**64 - 1
    return value - 2**64 if value >= 2**63 else value


def truncated_div(a, b):
    """Division that rounds toward zero, unlike Python's //."""
    quotient = abs(a) // abs(b)
    return to_signed(-quotient if (a < 0) != (b < 0) else quotient)


def random_expr(rng, depth, variables):
    """Returns (source text, value), computed here independently of the compiler."""
    if depth == 0 or rng.random() < 0.2:
        if variables and rng.random() < 0.5:
            name, value = rng.choice(variables)
            return name, value
        value = rng.choice([0, 1, 2, 3, 7, 255, 65536, 2**40, MAX, rng.randint(0, MAX)])
        return str(value), value
    kind = rng.randrange(14)
    if kind == 0:
        text, value = random_expr(rng, depth - 1, variables)
        return f"(-{text})", to_signed(-value)
    if kind == 1:
        text, value = random_expr(rng, depth - 1, variables)
        return f"(!{text})", int(value == 0)
    left, a = random_expr(rng, depth - 1, variables)
    right, b = random_expr(rng, depth - 1, variables)
    if kind in (5, 6) and b == 0:
        kind = 2  # Division by zero aborts by design, so don't generate it here.
    if kind == 2:
        return f"({left} + {right})", to_signed(a + b)
    if kind == 3:
        return f"({left} - {right})", to_signed(a - b)
    if kind == 4:
        return f"({left} * {right})", to_signed(a * b)
    if kind == 5:
        return f"({left} / {right})", truncated_div(a, b)
    if kind == 6:
        return f"({left} % {right})", to_signed(a - to_signed(truncated_div(a, b) * b))
    if kind == 7:
        return f"({left} == {right})", int(a == b)
    if kind == 8:
        return f"({left} != {right})", int(a != b)
    if kind == 9:
        return f"({left} < {right})", int(a < b)
    if kind == 10:
        return f"({left} <= {right})", int(a <= b)
    if kind == 11:
        return f"({left} > {right})", int(a > b)
    if kind == 12:
        return f"({left} && {right})", int(a != 0 and b != 0)
    return f"({left} || {right})", int(a != 0 or b != 0)


class Runner:
    def __init__(self, compiler, folder):
        self.compiler = compiler
        self.folder = pathlib.Path(folder)
        self.count = 0

    def compile(self, source, *flags):
        self.count += 1
        path = self.folder / f"p{self.count}.mc"
        path.write_text(source)
        exe = self.folder / f"p{self.count}"
        return exe, subprocess.run([self.compiler, str(path), "-o", str(exe), *flags], capture_output=True, text=True)

    def run(self, source, *flags):
        exe, result = self.compile(source, *flags)
        if result.returncode != 0:
            raise AssertionError(f"compile failed: {result.stderr}")
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)


def main():
    compiler = str(pathlib.Path(sys.argv[1]).resolve())
    failures = []
    passed = 0

    def check(name, condition, detail=""):
        nonlocal passed
        if condition:
            passed += 1
        else:
            failures.append(f"{name}: {detail}")

    with tempfile.TemporaryDirectory() as folder:
        runner = Runner(compiler, folder)

        # Every program must behave the same with and without the optimizer.
        for name, source, expected, code in PROGRAMS:
            for flags in ((), ("--no-opt",)):
                result = runner.run(source, *flags)
                printed = [int(line) for line in result.stdout.split()]
                check(f"{name} {flags}", printed == expected and result.returncode == code,
                      f"printed {printed}, exit {result.returncode}")

        for source, fragment in ERRORS:
            exe, result = runner.compile(source)
            check(f"error: {fragment}", result.returncode == 1 and fragment in result.stderr, result.stderr)
            check(f"no output on error: {fragment}", not exe.exists())

        for source in ("int main() { int z = 0; print(1 / z); }", "int main() { int z = 0; print(1 % z); }", "int main() { print(1 / 0); }"):
            for flags in ((), ("--no-opt",)):
                check(f"divide by zero aborts {flags}", runner.run(source, *flags).returncode == -6, source)

        # Random expressions, checked against values computed in Python.
        rng = random.Random(1234)
        for i in range(300):
            variables = [(f"v{n}", rng.choice([0, 1, -1, 5, MAX, MIN, rng.randint(MIN, MAX)])) for n in range(3)]
            declarations = "".join(
                f" int {name} = {'-9223372036854775807 - 1' if value == MIN else value};" for name, value in variables
            )
            pairs = [random_expr(rng, 5, variables) for _ in range(4)]
            source = "int main() {" + declarations + "".join(f" print({text});" for text, _ in pairs) + " }"
            expected = [value for _, value in pairs]
            for flags in ((), ("--no-opt",)):
                printed = [int(line) for line in runner.run(source, *flags).stdout.split()]
                check(f"random {i} {flags}", printed == expected, f"{source}\n got {printed}\n want {expected}")

    print(f"{passed} checks passed, {len(failures)} failed")
    for failure in failures[:15]:
        print("FAIL", failure[:900])
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
