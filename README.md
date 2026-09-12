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
make               # bin/rivel, bin/rivelc, bin/qbe, lib/rivel_rt.o
make unit          # C unit tests for the runtime and compiler internals
make test          # the language test suite (tests/cases)
make test-examples  # runnable examples and tic-tac-toe checks
make sanitize      # everything again under ASan and UBSan
```

## Use

```sh
export PATH="$PWD/bin:$PATH"    # run once from this checkout
rivel hello.rivel              # compile to a temporary file and run it
rivel main.rv                  # .rv is the shorter source extension
rivel main.rv Ada --verbose    # pass arguments to the program
rivelc hello.rivel             # produces ./hello
./hello
rivelc -o out prog.rivel       # choose the output path
rivelc -t arm64_apple ...      # cross-emit for another target (see bin/qbe -h)
```

For future terminals, add `export PATH="/absolute/path/to/rivel-c/bin:$PATH"`
to your shell startup file (`~/.zshrc` for Zsh or `~/.bashrc` for Bash).
Use the checkout's absolute path there. This also makes `rivel-lsp` available
after `make lsp`.

Both `.rivel` and `.rv` work with the compiler and editor integrations.
`rivel` runs the program and returns its exit status; `rivelc` creates a reusable
executable. `rivelc run` remains available. Put compiler options before the
source filename and program arguments after it when running a program.

Useful while developing: `--dump-tokens`, `--dump-ast`, `--dump-ir`,
`--emit-il` (the QBE input), `--emit-asm`, and `--keep`.

The compiler finds `bin/qbe` and `lib/rivel_rt.o` relative to the checkout it
was built in. Set `RIVEL_HOME` to point it elsewhere, and `CC` to choose the
C compiler used for assembling and linking.

## Examples

Start with [hello world](examples/hello_world.rivel),
[for loops](examples/for_loops.rivel), or
[structs with methods](examples/structs.rivel). The
[examples guide](examples/README.md) covers inference, functions, lists,
strings, optionals, and command-line arguments.

For a complete project, try [tic-tac-toe](examples/tic_tac_toe/README.md):
a terminal game with an AI opponent, two-player mode, hints, undo, and a
session scoreboard.

```sh
bin/rivelc run examples/tic_tac_toe/main.rivel
bin/rivelc run examples/tic_tac_toe/main.rivel --demo
```

## Editor support

[Rivel editor support](editors/README.md) includes a language server, a Zed
extension with Tree-sitter highlighting, and a VS Code extension. Run `make lsp`
with Node.js 22+ to build the server. It provides live compiler diagnostics,
typed completion, hover, definition, references, checked rename, signature help,
symbols, and inferred type hints. The same server works with other LSP clients.
Portable TextMate highlighting is also included.

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
src/ide/       compiler analysis for editor features
tools/lsp/     portable language server
runtime/       rivel_rt.c: garbage collector, strings, lists, io, panics
third_party/   QBE with a documented build compatibility patch
tests/cases/   language tests: run, error, panic, warn (see tests/run.sh)
tests/unit/    C unit tests
docs/          spec.md, architecture.md, qbe-notes.md
examples/      language examples and a complete tic-tac-toe game
editors/       syntax grammar, VS Code extension, and TextMate export
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
