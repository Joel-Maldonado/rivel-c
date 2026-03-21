# Rivel

Rivel is a small statically typed, compiled, language with a tiny core.
This repo is mostly a toy compiler project I made for fun to learn more about
what it feels like to compile a language. 

It is very much an educational project that I quickly made to learn about comilers, not a serious production language. In fact, most of the features you expect to be in a programming language aren't implemented yet!

The syntax of Rivel right now is a bit like if Python was static and compiled, and with braces instead of whitespace.

Right now the compiler simply compiles Rivel source code to C which then produces a native executable via gcc:

`Rivel -> automatically generated C -> native executable`

If you want the exact syntax and behavior, see [docs/grammar.md](docs/grammar.md). But if you want to quickly see some of what's implemented so far, try running the `example.rivel` file.

## Quickstart

### Prerequisites

- `make`
- `gcc` in your `PATH` for compiling generated C into the final executable

### Build the compiler

```bash
make
```

That gives you `./rivel`, the rivel compiler.

### Try a small program

Create `hello.rivel`:

```rivel
const BASE: Int = 40

fn add_one(x: Int) -> Int {
    return x * 2 + 1
}

fn main() -> Int {
    const result = add_one(BASE)
    print(result)
    return 0
}
```

Compile and run it:

```bash
./rivel -o hello hello.rivel
./hello
```

Expected behavior:

- `./hello` prints `81`
- the process exits with status `0`

If you want to keep the automatically generated C code around:

```bash
./rivel -o hello hello.rivel --emit-c
./hello
ls hello hello.c
```

Flags:
- `-o <name>` changes the executable name
- `--emit-c` saves the automatically generated C code

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

The language is tiny and not developed yet. Some important limits:

- one input file per compiler invocation
- only `Int` and `Bool` exist today
- no imports or modules
- no strings, character literals, lists, or member access
- no `for` loops
- no top-level `mut`
- `print(...)` is a statement-only builtin, not an expression
- conditions for `if` and `while` must not be wrapped directly in outer parentheses

## Testing

To test if the compiled rivel compiler is working properly:

```bash
bash tests/run_tests.sh
```

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
