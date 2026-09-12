# QBE IL notes for the Rivel emitter

A compact reference for writing the QBE intermediate-language emitter. It is
distilled from `third_party/qbe/doc/il.txt` (the authoritative spec; read it
when something here is not enough) and from a spike that was compiled, run,
and cross-compiled with the vendored `bin/qbe`. Every claim marked *verified*
was checked by running QBE 1.2, not just by reading the manual.

## 1. Toolchain

- QBE 1.2 lives unmodified in `third_party/qbe/`; `make bin/qbe` (part of
  `make all`) builds it. Its Makefile generates `config.h` on first build to
  pick the default target from the build host's `uname`, so `bin/qbe` with no
  `-t` already targets the machine it was built on.
- Usage: `bin/qbe [-t target] [-o out.s] file.il` (`-o -` or no `-o` means
  stdout; `file` may be `-` for stdin; several input files may be given).
- Targets (`bin/qbe -h`): `amd64_sysv` (default here), `amd64_apple`,
  `arm64`, `arm64_apple`, `rv64`. `bin/qbe -t ?` prints the default.
- Pick the target from the host: Linux x86-64 -> `amd64_sysv`,
  Linux aarch64 -> `arm64`, macOS arm64 -> `arm64_apple`,
  macOS x86-64 -> `amd64_apple`. The Linux `arm64` output uses ELF
  directives (`.type`, `.size`, `.section .note.GNU-stack`) and will not
  assemble on macOS; the `_apple` variants use Mach-O sections.

From `.il` to an executable, with a C helper file and libm:

```sh
# Linux (either arch; -t is optional when it matches the host)
bin/qbe -t amd64_sysv -o prog.s prog.il && cc prog.s helper.c -o prog -lm && ./prog

# macOS (Apple Silicon / Intel); cc is clang, which assembles the .s. -lm is harmless.
bin/qbe -t arm64_apple -o prog.s prog.il && cc prog.s helper.c -o prog -lm && ./prog
bin/qbe -t amd64_apple -o prog.s prog.il && cc prog.s helper.c -o prog -lm && ./prog
```

The IL is identical for every target (*verified*: the same `prog.il` was
compiled for all four amd64/arm64 targets). Target differences are confined
to the emitted assembly, so the emitter never needs to know the target:

| | Linux (`amd64_sysv`, `arm64`) | Apple (`*_apple`) |
|---|---|---|
| global symbol `$main` | `main` | `_main` |
| block labels | `.Lbb4` / `.L4` | `Lbb4` / `L4` |
| float literals | `.section .rodata` | `.section __TEXT,__literal8,8byte_literals` |
| function metadata | `.type f, @function` / `.size` | none |

Exception: a quoted global `$"name"` is emitted verbatim (`"name"`) with no
`_` prefix on any target, so it silently breaks C interop on macOS. Always
use plain `$name` for anything that must link against C.

## 2. Lexical rules

- Comments: `#` to end of line.
- Sigils: `$global`, `%temp` (function-scoped), `@block`, `:aggregate-type`.
- Identifier after the sigil: first char is a letter, `.` or `_`; following
  chars may be letters, digits, `.`, `_`, `$`. So `%t.12`, `@loop.3`,
  `$Point.length` are all fine names for mangling (*verified*).
- Tokens are separated by spaces or tabs; newlines are significant in
  function bodies (one instruction per line) but are plain whitespace inside
  `data { ... }` and `type { ... }` bodies. Spacing may be omitted next to
  symbols (`,` `=` `{`), e.g. `%r=w add %a,%b` parses.
- Top-level items are `data`, `function`, `type`, `dbgfile`. Order is
  irrelevant: functions and data are mutually recursive across the whole file
  (and across files), and there are **no declarations** — external C
  functions and globals are simply referenced by `$name`.

## 3. Types

| letter | meaning | where it can appear |
|---|---|---|
| `w` | 32-bit int (C `int`, `int32_t`, `bool` at the ABI) | temps, params, returns, data, memory ops |
| `l` | 64-bit int; also every pointer (C `long`, `int64_t`, `T*`, `size_t`) | temps, params, returns, data, memory ops |
| `s` | 32-bit float (C `float`) | temps, params, returns, data, memory ops |
| `d` | 64-bit float (C `double`) | temps, params, returns, data, memory ops |
| `b` | 8-bit | `data` items and `type` fields only |
| `h` | 16-bit | `data` items and `type` fields only |
| `sb` `ub` `sh` `uh` | signed/unsigned 8/16-bit at the C ABI (`char`, `short`) | function params, call args, return types only; the temp has base type `w` |
| `:name` | user aggregate (struct passed/returned by value) | params, call args, returns; the value is a pointer |

Rules:

- Temporaries can only have the four base types `w l s d`. Bytes and
  halfwords are loaded into a `w` or `l` (`loadsb`/`loadub`...) and stored
  from a `w` (`storeb`/`storeh`).
- Subtyping is one-way: an `l` may be used where a `w` is expected (only the
  low 32 bits are used). A `w` may **not** be used where an `l` is expected —
  QBE errors with `invalid type for first operand %x in add` (*verified*).
  Widen explicitly with `extsw` (sign) or `extuw` (zero).
- Constants are untyped 64-bit two's-complement blobs, sized by context.
  `-1` is written literally: `%m =l copy -1`, `call $f(l -1)`, `l -1` in
  data, `w -1` and `b -1` in data all work (*verified*). Floats are written
  `d_1.5`, `d_-2.5`, `s_1.5` (a bare `1.5` is a syntax error). A bare `$sym`
  is a constant holding the symbol's address.

## 4. Functions

```
[export] function [RETTY] $name([env %e,] [TY %p1, TY %p2, ...] [, ...]) {
@start
        ...
}
```

- `RETTY` is `w l s d`, a sub-word type, or `:aggr`; omit it for a void
  function. A void function ends blocks with a bare `ret`; `ret 1` in a void
  function is a syntax error (*verified*). A typed function should end with
  `ret VAL` — but QBE **accepts a bare `ret` in a `w` function and returns
  garbage** (*verified*), so the emitter must never rely on QBE to catch a
  missing return value.
- C `int`/`int32_t`/`bool` results use `w`; C `long`/`int64_t`/pointers use
  `l`. `main` is `export function w $main(w %argc, l %argv)`.
- `export` marks the symbol global (`.globl`); without it the symbol is
  still a real (non-`.L`) symbol in the object but file-local, so two IL
  files can both define a non-exported `$helper` without a link clash.
  Anything called from C or from another compilation unit must be exported.
- `env %e`: an optional first parameter of type `l` that is invisible to C
  callers (`function w $f(env %e, w %a)` has C prototype `int f(int)`). Pass it
  from IL with `call $f(env %closure, w 1)`; if the callee does not declare
  `env`, the value is silently dropped. This is how closures should carry
  their environment pointer.
- `...` as the last parameter makes the function variadic; the body reads
  extra arguments with `vastart`/`vaarg` (see il.txt "Variadic"). Calling a
  variadic C function needs the `...` marker between fixed and extra args:
  `%r =w call $printf(l $fmt, ..., w %x)` (*verified*).
- Sub-word params arrive in a `w` with only the low N bits meaningful;
  sign/zero-extend them yourself (`extsb`/`extub`/`extsh`/`extuh`) before use.

## 5. Blocks and control flow

```
@label
        %x =w phi @pred1 VAL, @pred2 VAL      # optional, must come first
        ...instructions...
        jmp @l | jnz VAL, @then, @else | ret [VAL] | hlt
```

- Every block starts with a label and ends with exactly one jump. If a block
  has no terminator and the next block follows it in the file, the parser
  inserts a `jmp` to it (used by `$pick` in the spike).
- **The first block can never be a jump target** (`invalid jump to the start
  block`, *verified*). A loop at the top of a function therefore needs an
  entry block before the loop header: always emit `@start` as a prologue
  (allocs and parameter spills go there) and begin the body in a second
  block.
- `jnz` tests a `w`; an `l` is accepted but only its low 32 bits are tested.
  Comparisons produce 0/1 in a `w` or `l`, so `%c =w csltl %i, 10` followed
  by `jnz %c, ...` is the idiom.
- `hlt` marks unreachable code (after a call to `exit`, for instance).
- Every block referenced must be defined (`block @x is used undefined`).

## 6. Data

```
[export] data $name = [align N] { ITEM, ITEM, ... }
ITEM := TY VALUE+ | z N
TY   := b | h | w | l | s | d
VALUE:= NUMBER | s_F | d_F | $sym | $sym+OFF | "string"   (string only after b)
```

- Members are packed; you write the padding (`z N` = N zero bytes). `align`
  applies to the whole object and defaults to 8.
- Several values may follow one letter: `w 1 2 3` is three words.
- Symbol references: `l $hello_ti` stores the address of `$hello_ti`;
  `l $tbl+16` stores address+16 and `l $tbl+-8` stores address-8
  (emitted as `.quad tbl+16` / `.quad tbl-8`, *verified*). Self-reference is
  fine (`data $c = { l $c }`).
- Strings only with `b` (`strings only supported for 'b' currently`,
  *verified*). A string does **not** get an implicit terminator: write
  `b "hello", b 0` for a C string.
- QBE copies the bytes between the quotes verbatim into a `.ascii "..."`
  directive, so escape sequences are interpreted by the **assembler**, not
  by QBE (*verified* by dumping the bytes). What works on GNU as and on
  clang's integrated assembler: `\n \t \r \" \\`, three-digit octal `\NNN`
  (`\000` for a NUL, `\303\251` for UTF-8 bytes), and raw non-ASCII bytes
  pass through unchanged. The only thing QBE itself cares about is that an
  unescaped `"` ends the string. A raw newline inside the quotes is copied
  into the `.s` and corrupts the assembly with only a warning (*verified*),
  so the emitter must escape it. Safest rule: emit printable ASCII except
  `"` and `\` verbatim, everything else as `\NNN` octal.
- A data object with only `z` items goes to `.bss` automatically.
- Layout of the string object used by the spike, for reference:
  `data $hello = align 8 { l $hello_ti, w 1, w 0, l 5, b "hello", b 0 }`
  is `[0]=type-info ptr, [8]=w, [12]=w, [16]=l length, [24]=bytes, [29]=0`.

## 7. Instruction reference (the subset the emitter needs)

Type strings follow il.txt: the letters before the parentheses are the
allowed result types, those inside are the argument types for each result
type. `T`=`wlsd`, `I`=`wl`, `F`=`sd`, `m`=pointer (`l` on all our targets).

Stack and memory:

| instruction | type | notes |
|---|---|---|
| `alloc4 N`, `alloc8 N`, `alloc16 N` | `m(l)` | N bytes with 4/8/16 alignment. **Only in `@start` with a constant size** — an alloc in any later block is a dynamic `sub rsp` executed every time the block runs; in a loop the stack grows each iteration (*verified* in the emitted asm). |
| `loadl`, `loadw`, `loadd`, `loads` | `l(m)`, `w(m)`, `d(m)`, `s(m)` | `loadw` = `loadsw`; `loadsw`/`loaduw` also load into an `l` |
| `loadsb`, `loadub`, `loadsh`, `loaduh` | `I(m)` | sign/zero extend a byte/halfword into a `w` or `l` |
| `storel V, P`, `storew`, `stored`, `stores` | `(l,m)`... | value first, address second |
| `storeb V, P`, `storeh V, P` | `(w,m)` | store the low 8/16 bits of a `w` |
| `blit SRC, DST, N` | `(m,m,w)` | memcpy of constant N bytes; keep N small, call `memcpy` for big copies |

Address arithmetic is ordinary integer arithmetic on `l`: `%p =l add $hello, 24`
or `%p =l add %base, %off`. A load or store may take a `$sym` directly
(`%x =l loadl $g`, `storel 9, $g`, *verified*), but `$sym+off` is **data-only
syntax** — inside a function `loadl $g+8` is a parse error (`, or end of line
expected`, *verified*).

Arithmetic and bits (`div`/`rem` truncate toward zero; `rem` has the sign of the dividend):

| | type |
|---|---|
| `add`, `sub`, `mul`, `div` | `T(T,T)` (signed for integers) |
| `neg` | `T(T)` |
| `udiv`, `rem`, `urem` | `I(I,I)` |
| `and`, `or`, `xor` | `I(I,I)` |
| `shl`, `shr`, `sar` | `I(I,ww)` — the shift amount is always a `w`; taken modulo the result width |

Comparisons return 1 or 0 as a `w` or `l`; the suffix names the **operand**
type and must match it exactly (`csltl` on a `w` operand is an error, *verified*):

| operands | instructions |
|---|---|
| integers (`w`/`l`) | `ceqw ceql cnew cnel csltw csltl cslew cslel csgtw csgtl csgew csgel` and unsigned `cultw cultl culew culel cugtw cugtl cugew cugel` |
| doubles | `ceqd cned cltd cled cgtd cged`, plus `cod` (neither NaN) and `cuod` (either NaN) |
| singles | same with `s`: `ceqs cnes clts cles cgts cges cos cuos` |

Conversions:

| instruction | type | meaning |
|---|---|---|
| `extsw`, `extuw` | `l(w)` | sign/zero-extend word to long |
| `extsb`, `extub`, `extsh`, `extuh` | `I(w)` | extend the low 8/16 bits of a word |
| `exts` | `d(s)` | float to double |
| `truncd` | `s(d)` | double to float |
| `swtof`, `uwtof` | `F(w)` | signed/unsigned word to float or double |
| `sltof`, `ultof` | `F(l)` | signed/unsigned long to float or double |
| `stosi`, `stoui`, `dtosi`, `dtoui` | `I(s)` / `I(d)` | float/double to int (truncating) |
| `cast` | `wlsd(sdwl)` | bit-for-bit reinterpret between same-width int and float (`w<->s`, `l<->d`) |
| `copy` | `T(T)` | move; also how to give a constant a type (`%z =l copy 0`) |

Narrowing an `l` to a `w` needs no instruction (subtyping).

Calls:

```
[%r =RETTY] call VAL([env VAL,] TY VAL, TY VAL, ... [, ..., TY VAL, ...])
```

- `VAL` is usually `$name` but may be an `l` temp holding a function pointer.
- Every argument carries its ABI type and every call names its result type;
  there are no prototypes, so **the call site alone determines the ABI**. A
  wrong type at a call (`l` for a `double`, `w` for a pointer) compiles
  cleanly and miscompiles. Get these from Rivel's own signatures.
- Void calls omit the result: `call $print_i64(l %v)`. A non-void function
  called without a result also parses (*verified*); that is harmless for
  scalar results but wrong for `:aggr` results (the hidden return pointer is
  not passed), so always bind the result of an aggregate-returning call.
- Sub-word results (`=sb`, `=ub`...) define a `w` with unspecified high bits.

Jumps: `jmp @l`, `jnz VAL, @nonzero, @zero`, `ret [VAL]`, `hlt` (section 5).

Phi: `%x =w phi @from1 VAL, @from2 VAL` — must list every predecessor exactly
once, must sit at the top of the block. Only needed if you build SSA
yourself; see section 8.

Debug info (optional): `dbgfile "path"` at top level and `dbgloc LINE[, COL]`
as an instruction emit `.file`/`.loc` directives.

## 8. Non-SSA temporaries

QBE does its own SSA construction, so the emitter can assign a temp as many
times as it likes, in as many blocks as it likes, and skip `phi` entirely.
The spike relies on this (*verified*): `%i` and `%sum` are reassigned in the
loop body, and `$pick` sets `%r` in both arms of an `if` and reads it after
the join. Rules:

1. **One type per temp name, for the whole function.** Definitions of a temp
   are merged: mixing `w` and `l` silently demotes the temp to `w`
   (so a later use as `l` fails with `invalid type for ... operand`, and if
   every use happens to be `w` the 64-bit definition is truncated with no
   diagnostic); mixing int with float or `s` with `d` is the hard error
   `temporary %x is assigned with multiple types`. Give each source
   variable a fixed IL type and never reuse a temp name for another type.
2. A temp used on a path where it was never assigned is not diagnosed
   (QBE inserts an undefined value). A temp with no definition anywhere is
   reported, confusingly, as `invalid type for first operand %x in add`.
3. Temps are function-scoped; reusing `%t0` in the next function is fine.
4. Do not mix hand-written `phi` with non-SSA assignment of the same temp;
   if a temp is defined by a phi, QBE assumes it is fully SSA.

Alternative (what LLVM frontends do): give every mutable variable an
`alloc8` slot in `@start` and `storel`/`loadl` through it. QBE's `promote`
pass turns slots whose address never escapes back into registers, so this
costs nothing at `-O`-less QBE and is the right choice for variables whose
address is taken or that are captured by closures.

## 9. Gotchas, ranked

1. **Call-site types are the ABI.** No prototypes exist; a wrong `w`/`l`/`d`
   at a call or a wrong return type compiles and silently corrupts. Treat the
   arg-type list at every call as generated from the callee's real signature,
   and use `w` (not `l`) for C `int`/`bool` results.
2. **`w` never widens implicitly and comparisons name the operand type.**
   `%r =l add %w, 1` is an error; write `extsw`/`extuw` first. `csltl` needs
   `l` operands, `csltw` needs `w`. `l` narrows to `w` silently (low 32 bits).
3. **One type per temp name** (section 8). `w`/`l` mixing is not even an
   error at the definition, only at a later `l` use.
4. **`$sym+off` only in data.** In code use `add $sym, off`. Loads/stores can
   take a bare `$sym` as the address.
5. **All `alloc`s in `@start`, with constant sizes**, or the stack grows on
   every execution of the block.
6. **The first block cannot be a jump target**; emit a prologue block.
7. **Strings are raw assembler text.** Escape `"`, `\`, and every
   non-printable byte (newline especially) as `\NNN`; append `b 0` yourself.
8. **A missing `ret VAL` in a typed function is not diagnosed** — the
   emitter must guarantee every path returns a value (or ends in `hlt`).
9. **Float literals need the `d_`/`s_` prefix**; a bare `1.5` is an error.
10. **Never use `$"quoted"` names for C symbols** — they skip the `_` prefix
    on Apple targets.
11. **Link with `-lm`** on Linux when the IL calls `$sqrt` and friends; QBE
    emits plain calls, nothing is inlined.

## 10. The spike (verified output)

Files in the session scratchpad `qbe-spike/`: `helper.c`, `prog.il`.
Built and run on Linux x86-64 with
`bin/qbe -o prog.s prog.il && cc prog.s helper.c -o prog -lm && ./prog`, which
prints exactly:

```
42
45
hello
4
10
20
-1
```

`bin/qbe -t arm64`, `-t arm64_apple` and `-t amd64_apple` all accept the
same file without error.

### helper.c

```c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int64_t add_checked(int64_t a, int64_t b)
{
	int64_t r;
	if (__builtin_add_overflow(a, b, &r)) {
		fputs("integer overflow\n", stderr);
		exit(1);
	}
	return r;
}

void print_i64(int64_t v)
{
	printf("%lld\n", (long long)v);
}

void print_str(const char *p, int64_t len)
{
	fwrite(p, 1, (size_t)len, stdout);
	fputc('\n', stdout);
}
```

### prog.il

```
# prog.il -- hand-written QBE IL exercising everything the Rivel emitter needs.
# Build (Linux): bin/qbe -o prog.s prog.il && cc prog.s helper.c -o prog -lm && ./prog

# A string object: { type_info*, w refcount, w pad, l len, bytes..., 0 }
# Offsets: 0 = $hello_ti, 8 = w 1, 12 = w 0, 16 = l 5, 24 = "hello", 29 = 0.
data $hello_ti = align 8 { l 0, l 1, l 0, l 0 }
data $hello = align 8 { l $hello_ti, w 1, w 0, l 5, b "hello", b 0 }

# Takes and returns a double, calls libm sqrt.
function d $root(d %x) {
@start
	%r =d call $sqrt(d %x)
	ret %r
}

# Takes a w boolean, branches with jnz. %r is assigned in two blocks: non-SSA.
function w $pick(w %b) {
@start
	jnz %b, @yes, @no
@yes
	%r =w copy 10
	jmp @join
@no
	%r =w copy 20
@join
	ret %r
}

export function w $main(w %argc, l %argv) {
@start
	# Stack slot: store and load an l through it.
	%slot =l alloc8 8
	storel 42, %slot
	%v =l loadl %slot
	call $print_i64(l %v)

	# Counted loop 0..10 (exclusive). %i and %sum are reassigned in @body: non-SSA.
	%i =l copy 0
	%sum =l copy 0
@loop
	%c =w csltl %i, 10
	jnz %c, @body, @done
@body
	%sum =l call $add_checked(l %sum, l %i)
	%i =l add %i, 1
	jmp @loop
@done
	call $print_i64(l %sum)

	# Read the length field at $hello+16, print the bytes at $hello+24.
	%lenp =l add $hello, 16
	%len =l loadl %lenp
	%str =l add $hello, 24
	call $print_str(l %str, l %len)

	# Double round trip: sqrt(16.0) -> 4.
	%d =d call $root(d d_16)
	%di =l dtosi %d
	call $print_i64(l %di)

	# Boolean branch, both ways.
	%p1 =w call $pick(w 1)
	%p1l =l extsw %p1
	call $print_i64(l %p1l)
	%p0 =w call $pick(w 0)
	%p0l =l extsw %p0
	call $print_i64(l %p0l)

	# An l constant -1.
	%m =l copy -1
	call $print_i64(l %m)

	ret 0
}
```
