# Rivel

Rivel is a small statically typed language with an intentionally tiny core.
This repo is mostly a toy compiler project I made for fun to learn more about
what it feels like to compile a language end to end.

It is very much an educational project, not a serious production language. The
language is limited on purpose, the compiler is small enough to read through,
and the whole point was to try things out and see the pipeline work.

Right now the compiler lowers Rivel source to C and then invokes a host C
compiler to produce a native executable:

`Rivel -> generated C -> native executable`

If you want the exact syntax and behavior, see [docs/grammar.md](docs/grammar.md).

## Quickstart

### Prerequisites

- `make`
- a C11 compiler to build `rivel`
- `clang` in your `PATH` for compiling generated C into the final executable

`make` builds the compiler itself. When you later run `./rivel program.rivel`,
the compiler currently invokes `clang -std=c11` internally for the generated C.

### Build the compiler

```bash
make
```

That gives you `./rivel`.

### Try a small program

Create `hello.rivel`:

```rivel
const BASE: Int = 40

fn add_one(x: Int) -> Int {
    return x + 1
}

fn main() -> Int {
    const result = add_one(BASE)
    print(result)
    return result
}
```

Compile and run it:

```bash
./rivel hello.rivel
./out
```

Expected behavior:

- `./out` prints `41`
- the process exits with status `41`

If you want to keep the generated C around and give the output a better name:

```bash
./rivel hello.rivel -o hello --emit-c
./hello
ls hello hello.c
```

What those flags do:

- default output name: `out`
- `-o <name>` changes the executable name
- without `--emit-c`, only the executable is kept
- with `--emit-c`, `<output>.c` is kept next to the executable

## What It Can Do Right Now

### Declarations and Types

- top-level `const` declarations
- function declarations with explicit parameter and return types
- required entrypoint `fn main() -> Int`
- local `const` and `mut` bindings
- optional type annotations on local bindings with inference from the initializer
- built-in types: `Int` and `Bool`

### Statements and Control Flow

- `return <expr>`
- `if` / `elif` / `else`
- `while`
- assignments to `mut` bindings
- function-call statements such as `print(x)` or `helper()`

### Expressions

- integer and boolean literals
- identifier references
- named function calls in expression position
- grouped expressions with `(...)`
- unary operators: `-`, `not`
- arithmetic operators: `+`, `-`, `*`, `/`, `%`
- comparison operators: `==`, `!=`, `<`, `<=`, `>`, `>=`
- logical operators: `and`, `or`

### Functions, Scope, and Runtime Behavior

- forward calls and recursion
- lexical block scope with shadowing in nested scopes
- top-level constant evaluation
- builtin `print(expr)` for `Int` and `Bool`
- division and modulo by zero are checked at runtime and terminate with an error
- `main`'s returned `Int` becomes the program's process exit code

## What It Does Not Do

The language is intentionally tiny. Some important limits:

- one input file per compiler invocation
- only `Int` and `Bool` exist today
- no imports or modules
- no strings, character literals, lists, or member access
- no `for` loops
- no top-level `mut`
- `print(...)` is a statement-only builtin, not an expression
- conditions for `if` and `while` must not be wrapped directly in outer parentheses
- the generated C is currently compiled with `clang`, even if `make` used a different C compiler to build `rivel`

## Testing

The main test path I actually trust right now is the end-to-end suite:

```bash
bash tests/run_tests.sh
```

That script rebuilds the compiler and checks:

- successful compilation and execution
- runtime error behavior
- compile-time diagnostics
- `--emit-c` artifact handling

A separate unit-test script exists at `tests/run_unit_tests.sh`, but I would not
treat it as the main documented workflow for this repo right now. The
end-to-end suite above is the one to lean on.

## If You Want To Poke Around The Compiler

The compiler is laid out as a pretty direct staged pipeline:

- `src/tokenizer.c`: lexical analysis and separator/comment handling
- `src/parser*.c`: declarations, statements, and expression parsing
- `src/semantic*.c`: name resolution, type checking, constant evaluation, and entrypoint validation
- `src/backend_c*.c`: C code generation and runtime helpers
- `src/driver.c` and `src/main.c`: file I/O, CLI handling, generated-C emission, and host compiler invocation

## Command Line Summary

The current CLI is:

```txt
rivel <input.rivel> [-o <output>] [--emit-c]
```

Notes:

- exactly one input file is accepted
- `-o` renames the produced executable
- `--emit-c` preserves the generated C file as `<output>.c`
