# Rivel compiler architecture

This is a guide for changing the compiler. The language itself is specified
in `spec.md`; the QBE intermediate language is summarized in `qbe-notes.md`.

## Pipeline

```
source text
  │  lex_source            src/lex        tokens with byte spans
  ▼
tokens
  │  parse_module          src/parse      syntax tree, recovers at statement and declaration boundaries
  ▼
Module (AST)
  │  sema_check            src/sema       symbols and types written onto the tree
  ▼
Program (AST + symbols + types)
  │  lower_program         src/lower      three-address IR over basic blocks
  ▼
IrModule
  │  qbe_emit              src/backend    QBE IL text
  ▼
prog.il ──bin/qbe──▶ prog.s ──cc + lib/rivel_rt.o──▶ executable
```

Every stage reports into one `Diags` and keeps going where it can, so a
file with several mistakes shows all of them. The driver stops between
stages when errors exist. Warnings never stop compilation.

Each stage has a dump flag (`--dump-tokens`, `--dump-ast`, `--dump-ir`,
`--emit-il`, `--emit-asm`) so a bug can be bisected to a stage in seconds.

## Memory ownership inside the compiler

One `Arena` per compilation owns the AST, types, symbols, and IR. Nothing in
those structures is freed individually. Vectors embedded in arena-owned
structs grow with `avec_push` (arena storage); short-lived vectors use
`vec_push` and `vec_free`. Out of memory calls `die`. The sanitizer job in CI
runs the compiler with leak detection on, so a stray `vec_push` into an
arena-owned struct shows up as a leak.

## Front end

The lexer is a mode stack. Ordinary tokens are lexed in `MODE_NORMAL`;
`f"` pushes `MODE_FSTR_TEXT`, which scans literal text and emits
`FSTR_TEXT`, and each `{` inside pushes `MODE_FSTR_EXPR`, where normal
tokens are lexed until the matching `}`. The parser therefore sees f-strings
as `FSTR_START (TEXT | EXPR_START expr EXPR_END)* FSTR_END` with real spans on
every token inside, and nested strings and nested f-strings need no special
handling.

The parser is recursive descent with one token of lookahead plus a peek at
the next for the `IDENT :=` and `IDENT :` declaration forms. `error_span`
enters panic mode and suppresses further errors until `sync_stmt` or
`sync_decl` reaches a boundary; `soft_error` reports without recovery for
mistakes the parser can carry on past (chained comparisons, a non-call used
as a statement). Struct construction and function calls parse to the same
`EXPR_CALL` node with optional argument labels; the checker decides which it
is.

## Semantic analysis

`sema_check` runs four passes:

1. collect every top-level name into the global scope (builtins and type
   names are already there);
2. resolve struct fields, method and function signatures, and global
   annotations; reject non-optional struct cycles;
3. type and fold globals in declaration order;
4. check each function body.

Types are interned in `TypeTable`, so `Type *` equality is type equality.
Every `Expr` gets a `type` (never NULL; `TY_ERROR` after a reported error,
which converts to and from everything so one mistake produces one message).
Every name gets a `Symbol *`, every call a `CallKind`, every field access an
index. The lowering pass reads these and never resolves anything itself.

Narrowing is an overlay, not a type change: `Checker.narrows` is a stack of
(symbol, type) facts. `effective_type` consults it before the declared type.
`collect_facts` pushes what a condition implies; `check_if` and `check_while`
push and pop around bodies; `check_block` pushes the negated condition after
an `if` whose body cannot fall through; assignment pushes the declared type
back. A narrowed name's `Expr.type` is the narrowed type while its symbol
keeps the declared type, and the lowering pass unboxes when they differ.

Reachability (`stmt_falls_through`) drives both the missing-return error and
the unreachable-code warning. `panic` and `exit` count as not falling
through; `while true` without a `break` does too.

## IR

`ir.h` defines a three-address IR with three machine classes (`W` 32-bit,
`L` 64-bit and pointers, `D` double). Locals are numbered stack slots rather
than SSA values so lowering needs no phi nodes; QBE promotes non-escaping
slots to registers. Blocks end in exactly one terminator. The module also
records string literals, struct type descriptors, panic locations, and
globals, which the backend emits as data.

Lowering conventions, shared with `runtime/rivel_rt.h`:

- every struct field, list element, and box payload is one 8-byte cell;
  bools are `W` in registers and widen to `L` in cells;
- optionals are pointers: null is 0 and a `T?` of a value type points to a
  box, so narrowing `int?` to `int` is a load from offset 16;
- string literals are static `RvStr` objects with the `RV_F_STATIC` flag;
- checked integer arithmetic calls the runtime (`rv_add` and friends) with a
  pointer to an `RvLoc` describing the source position;
- `&&`, `||`, `??`, `min`, and `max` are branches that meet at a slot.

The backend is a printer: one IR instruction becomes one or two IL
instructions. The generated C-ABI `main` calls `rv_init` with the address of a
stack slot (the collector's scan bound), registers pointer-typed globals as
roots, calls the user's main, and flushes stdout.

## Runtime

`rivel_rt.c` is compiled once into `lib/rivel_rt.o` and linked into every
program. The collector is a non-moving mark-sweep over size-classed pages,
precise on the heap through the type descriptors the compiler emits and
conservative on the stack and registers. Nothing in generated code registers
roots or maintains a shadow stack; any word on the stack that points into an
allocated object keeps that object alive.

## Adding a language feature

The usual path, in order, each step with its tests:

1. `docs/spec.md`: write the rule first. If you cannot state it precisely,
   the feature is not ready.
2. `src/lex`: new tokens or keywords, plus a `--dump-tokens` check.
3. `src/ast` and `src/parse`: a node, its parse, and its `ast_dump` form.
4. `src/sema`: typing rules and error messages. Add `tests/cases/error/*`
   for every new message; the messages are part of the language's interface.
5. `src/lower` and, if new runtime support is needed, `runtime/rivel_rt.h`
   and `rivel_rt.c` with a unit test in `rt_test.c`.
6. `tests/cases/run/*` for the behavior, `tests/cases/panic/*` for the
   failure modes.

Run `make unit test sanitize` before pushing. CI runs the same on Linux
x86-64 and macOS AArch64, so the AArch64 backend is exercised on every push.

## Tests

`tests/run.sh` treats every `tests/cases/**/*.rivel` as a test. Expectations
are directives in the file (`// exit:`, `// error:`, `// warn:`, `// panic:`,
`// args:`, `// stdin:`) and an optional `NAME.stdout` sidecar. Adding a test
is adding a file. `tests/run.sh substring` runs a subset.

## Debugging a miscompile

1. Reduce the program to a few lines.
2. `--dump-ir`: is the IR what you expect? If not, the bug is in lowering
   or earlier; `--dump-ast` narrows it further.
3. `--emit-il` and read the IL against `docs/qbe-notes.md`; a wrong call-site
   type (`w` for a pointer, a missing argument) compiles silently and
   misbehaves at run time.
4. `--keep` and inspect the assembly.
5. If the program panics with an impossible location, suspect argument count
   or order at a runtime call: the location is read from a register that a
   previous call left behind.
