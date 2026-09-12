# Rivel

Rivel is a Swift/Rust-style compiled language with Python ergonomics and
Go-style inference. It is statically typed, garbage-collected, and compiles
to native code.

```rivel
struct Point {
    x: float;
    y: float;

    func length(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
}

func main() -> int {
    p := Point(x: 3.0, y: 4.0);
    names: list[str] = [];
    for i in 0..3 {
        names.append(f"point {i}");
    }
    println(f"{p.length()} {names[1]} {len(names)}");
    return 0;
}
```

The compiler is written in C11. It lowers programs to a small typed IR and
hands that to [QBE](https://c9x.me/compile/), a vendored backend that does
instruction selection and register allocation for x86-64, AArch64, and
RISC-V on Linux and macOS. The runtime (garbage collector, strings, lists,
panics) is a single C file linked into every program.

## Build

You need a C compiler, `make`, and the system assembler and linker. Nothing
else.

```sh
make            # bin/rivelc, bin/qbe, lib/rivel_rt.o
make unit       # C unit tests for the runtime and compiler internals
make test       # the language test suite (tests/cases)
make sanitize   # everything again under ASan and UBSan
```

## Use

```sh
bin/rivelc hello.rivel          # produces ./hello
./hello
bin/rivelc run hello.rivel      # compile to a temporary file and run it
bin/rivelc -o out prog.rivel    # choose the output path
bin/rivelc -t arm64_apple ...   # cross-emit for another target (see bin/qbe -h)
```

Useful while developing: `--dump-tokens`, `--dump-ast`, `--dump-ir`,
`--emit-il` (the QBE input), `--emit-asm`, and `--keep`.

The compiler finds `bin/qbe` and `lib/rivel_rt.o` relative to the checkout it
was built in. Set `RIVEL_HOME` to point it elsewhere, and `CC` to choose the
C compiler used for assembling and linking.

## The language

The full contract is in [docs/spec.md](docs/spec.md). In short:

- `int` (64-bit, overflow panics), `float`, `bool`, `str` (immutable UTF-8
  bytes), `list[T]`, structs with methods, and `T?` optionals with `null`
- structs are reference types; assignment shares, `==` compares structurally
- optionals must be checked before use; the checker narrows `x` to `T` inside
  `if x != null` and after early returns
- `x := e;` declares with inference, `x: T = e;` with a type; no shadowing
- `if`/`else if`/`else`, `while`, `for x in a..b`, `for x in list`, `break`,
  `continue`
- f-strings: `f"{name} has {n} items"`
- floor division and modulo, left-to-right evaluation, panics with locations
  for out-of-range indexes, overflow, and division by zero

Errors are reported all at once, with the source line and a caret:

```
prog.rivel:12:14: error: `total: int` expects `int`, found `str`
    total: int = "0";
                 ^~~
```

## Project layout

```
src/base/      arena, vectors, string map, diagnostics with spans
src/lex/       tokenizer, including f-string modes
src/parse/     recursive-descent parser with error recovery
src/ast/       syntax tree and --dump-ast
src/sema/      types, symbols, checker, narrowing, constant folding
src/ir/        the IR, --dump-ir
src/lower/     AST to IR
src/backend/   IR to QBE IL
src/driver/    command line, running qbe and cc
runtime/       rivel_rt.c: garbage collector, strings, lists, io, panics
third_party/   QBE, vendored unmodified
tests/cases/   language tests: run, error, panic, warn (see tests/run.sh)
tests/unit/    C unit tests
docs/          spec.md, architecture.md, qbe-notes.md
examples/      small programs
legacy/        the previous C-transpiling compiler, kept for reference
```

See [docs/architecture.md](docs/architecture.md) for how the pieces fit and
how to add a language feature.

## Status

Working today: everything in the spec. Not yet: modules, enums and `match`,
dictionaries, closures. The roadmap is to grow the language only as far as
the compiler itself needs, then rewrite the compiler in Rivel.

## License

MIT. QBE is MIT, copyright Quentin Carbonneaux; see `third_party/README.md`.
