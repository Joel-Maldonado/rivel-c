# Rivel

Rivel is a small statically typed, compiled, language. This repo is mostly a toy compiler project I made to learn more about compilers. As such, it is extremely experimental and not meant at all to be production level.

That said, it already has the fundamentals: variables, constants, conditionals, while/for loops, functions, and structs.

Right now the compiler automatically compiles Rivel source code to C which then produces a native executable via gcc:

`Rivel -> automatically generated C -> native executable`

If you want the exact syntax and behavior, see [docs/grammar.md](docs/grammar.md). If you want a fuller showcase of what Rivel looks like today, run the `example.rivel`.

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
// Constant
const BASE: Int = 40

// Function
fn add_one(x: Int) -> Int {
    return x * 2 + 1
}

// Main entry point
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

### Run the full example

The repo also includes a larger `example.rivel` program that shows strings, doubles, loops, helper functions, and a readable multi-section report.

```bash
./rivel -o example example.rivel --emit-c
./example
```

## What It Can Do Right Now

### Declarations and Types

- built-in types: `Int`, `Double`, `Bool`, and `String`
- local `const` and `mut` bindings
- function declarations with explicit parameter and return types
- required entrypoint `fn main() -> Int`
- top-level `struct` declarations
- nominal struct types with named fields

### Statements and Control Flow

- C-style comments with `//` and `/* ... */`
- `return <expr>`
- `if` / `elif` / `else`
- `while`
- `for i in start..end` and `for i in start..=end` over `Int` ranges
- assignments to `mut` bindings
- assignments to fields through mutable local struct bindings
- function-call statements such as `print(x)` or `helper()`

### Expressions

- struct literals such as `Person { name: "John", age: 23 }`
- field access with `.`, such as `person.age`
- named function calls in expression position
- grouped expressions with `(...)`
- unary operators: `-`, `not`
- arithmetic operators: `+`, `-`, `*`, `/`, `%`
- comparison operators: `==`, `!=`, `<`, `<=`, `>`, `>=`
- logical operators: `and`, `or`

### Functions, Scope, and Runtime Behavior

- forward calls and recursion
- string builtins `len`, `substr`, `contains`, `starts_with`, `ends_with`
- struct values are copied by value; string-containing structs retain and release nested strings automatically
- strings are immutable UTF-8 byte sequences, and `len` and `substr` use byte counts

## What It Does Not Do

The language is very tiny, basic, and underdeveloed. Some current limits:

- no arrays yet
- one input file per compiler invocation
- no imports or modules
- `print(...)` is a statement-only builtin, not an expression

## Testing

To run the language and integration suite against the compiled compiler:

```bash
bash tests/run_tests.sh
```

To run the C unit tests for the compiler implementation itself:

```bash
bash tests/run_unit_tests.sh
```

## If You Want To Poke Around The Compiler

The compiler is laid out as a pretty direct staged pipeline:

- `src/tokenizer.c`: lexical analysis and separator/comment handling
- `src/parser*.c`: declarations, statements, and expression parsing
- `src/ast.c` / `src/ast.h`: syntax tree types plus semantic annotations attached to expressions
- `src/semantic*.c`: declaration collection, scope handling, name resolution, type checking, constant evaluation, and entrypoint validation
- `src/backend_c*.c`: C code generation, naming/lifetime helpers, and runtime emission
- `src/driver*.c` and `src/main.c`: CLI handling, pipeline orchestration, file I/O, generated-C emission, and host compiler invocation
