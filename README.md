# Rivel Lang (C++ Compiler)

Rivel is a small compiled language with a strict, statically typed v1 core.

This compiler is written in C++ and currently targets C as an intermediate
language:

`Rivel -> C -> clang -> native executable`

## Rivel v1

### Supported

- top-level `const`
- `fn` declarations
- `fn main() -> Int` entrypoint
- local `const` and `mut`
- explicit function parameter and return types
- optional local type annotations with inference
- `Int` and `Bool`
- `return`
- `if` / `elif` / `else`
- `while`
- function calls
- bare call statements
- builtin `print(expr)` for `Int` and `Bool`
- operators:
  - arithmetic: `+ - * / %`
  - comparison: `== != < <= > >=`
  - logical: `and or not`

## Example

```txt
const BASE: Int = 40

fn add(a: Int, b: Int) -> Int {
    return a + b
}

fn main() -> Int {
    mut total = add(BASE, 1)
    print(total)

    if total == 41 and not false {
        total = total + 1
    }

    return total
}
```

## Building

Requires CMake, a C++20 compiler, and `clang` in your `PATH`.

```bash
cmake -S . -B build
cmake --build build
```

The executable is written to `build/rivel`.

## Running

The compiler still takes a single `.rivel` input file and can optionally rename the
generated executable with `-o`.

```bash
./build/rivel program.rivel
./build/out
```

Custom output name:

```bash
./build/rivel program.rivel -o hello
./hello
```

If you also want to keep the generated C, pass `--emit-c`:

```bash
./build/rivel program.rivel --emit-c
```

By default the compiler writes only:

- `<output>` for the final executable

With `--emit-c`, it also writes:

- `<output>.c` for generated C

## Testing

```bash
bash tests/run_tests.sh
```
