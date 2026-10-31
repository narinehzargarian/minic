# minic

A small compiler written in C++20. It compiles a C-like language to ARM64 assembly and produces native macOS executables.

```
 source ──> lexer ──> parser ──> checker ──> optimizer ──> code generator ──> assembly ──> clang ──> executable
           tokens    syntax     names and    constant       ARM64            assembles
                     tree       scopes       folding                         and links
```

The compiler does everything except the final step, where `clang` assembles and links the generated text.

## The language

```c
// Prints the prime numbers below 50.
int is_prime(int n) {
    if (n < 2) {
        return 0;
    }
    int d = 2;
    while (d * d <= n) {
        if (n % d == 0) {
            return 0;
        }
        d = d + 1;
    }
    return 1;
}

int main() {
    int n = 0;
    while (n < 50) {
        if (is_prime(n)) {
            print(n);
        }
        n = n + 1;
    }
    return 0;
}
```

It has 64-bit integers, functions with up to 8 parameters, recursion, block scopes with shadowing, `if`/`else`, `while`, `break`, `continue`, arithmetic, comparisons, short-circuit `&&` and `||`, and a built-in `print`.

## Running it

Requires an Apple Silicon Mac with Xcode command-line tools, CMake, and GoogleTest.

```sh
cmake -S . -B build
cmake --build build -j
./build/minic examples/primes.mc -o primes
./primes
```

Add `-S` to see the assembly, or `--no-opt` to turn off the optimizer.

Errors give the exact location:

```
hello.mc:2:9: error: 'x' is not declared
```

## How it works

| Stage | What it does |
|---|---|
| Lexer | Splits text into tokens and tracks line and column. Rejects numbers that don't fit in 64 bits |
| Parser | Recursive descent with precedence levels. Limits nesting, so hostile input gets an error instead of overflowing the compiler's stack |
| Checker | Resolves names through nested scopes, checks calls against definitions, and assigns each variable a stack slot. Collects function signatures first, so functions can call ones defined later |
| Optimizer | Folds constant expressions and removes branches that can never run |
| Code generator | Writes ARM64 assembly following the platform's calling convention |

## Design decisions

**Expressions use the stack, not a register allocator.** Each expression leaves its result in `x0`, and intermediate values wait on the stack. The generated code is slow, but it's simple and clearly correct. A register allocator would be the next step.

**The optimizer must match the hardware exactly.** Constant folding happens in C++, where signed overflow is undefined behavior, while the generated code wraps. So folding computes on unsigned values, and handles the one division that overflows, the minimum value divided by -1, the same way ARM64's `sdiv` does.

**Division by zero stops the program.** ARM64 silently returns 0, which would hide bugs, so the generated code checks first and aborts. The optimizer leaves `1 / 0` for that runtime check rather than failing to compile, because the line may never run.

**Short-circuit folding keeps side effects.** `0 && f()` folds to 0 because `f` never runs. `f() && 0` doesn't fold, because `f` still has to run.

**macOS calling details.** The stack stays 16-byte aligned at all times. `printf`'s value goes on the stack, because Apple's ARM64 convention passes variadic arguments there rather than in registers.

## Testing

- **Unit tests (GoogleTest):** 19 tests for the lexer, parser, checker, and optimizer.
- **End-to-end tests (`tests/run_tests.py`):** 684 checks that compile programs, run the executables, and compare output and exit codes. Every program runs with and without the optimizer.
- **Random programs:** 300 generated programs, compared against values computed independently in Python.
- **Sanitizers:** all of the above also pass with the compiler built under AddressSanitizer and UBSan.
- **CI:** GitHub Actions runs everything on an Apple Silicon runner.

To check that the tests can fail at all, I planted six bugs one at a time: swapped subtraction operands, a wrong comparison, reversed arguments, a missing short circuit, truncated constants, and a wrong folded modulo. The end-to-end tests caught all six.

## Known limits

- It targets macOS on ARM64 only.
- The only type is the 64-bit integer. There are no arrays, pointers, strings, or `for` loops.
- It reports the first error and stops, rather than recovering to find more.
- It has no linker of its own, and uses `clang` for assembling and linking.
- The generated code isn't fast, and the only optimization is constant folding.
