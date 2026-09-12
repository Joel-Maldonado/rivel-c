# third_party

Vendored dependencies. Nothing in here is written by us; each subdirectory is
an unmodified copy of an upstream release plus this note.

## qbe/ — QBE 1.2

- Upstream: https://c9x.me/compile/ (git: `git://c9x.me/qbe.git`, tag `v1.2`;
  tarball: https://c9x.me/compile/release/qbe-1.2.tar.xz)
- License: MIT, copyright Quentin Carbonneaux — see `qbe/LICENSE`. That file
  applies to everything under `qbe/`.
- Vendored unmodified. No source file has been edited; `diff -r` against the
  upstream release tree shows no differences in the files that are kept.

### What was removed

To keep the tree small, three upstream directories that the compiler does not
need at build time were deleted:

- `minic/` — the example C frontend (a yacc grammar plus driver)
- `tools/` — upstream's test runner scripts (`test.sh`, `abi_fuzz.c`, ...)
- `test/` — upstream's IL test suite (~1.4 MB)

Everything else is kept: `LICENSE`, `README`, `Makefile`, `.gitignore`, every
`.c`/`.h` at the top level, the `amd64/`, `arm64/`, `rv64/` backends, and the
whole `doc/` directory (`il.txt` is the IL reference; `abi.txt` describes the
aggregate-type ABI lowering).

### How it is built

The top-level `Makefile` has a `bin/qbe` target that runs
`make -C third_party/qbe CC="$(CC)" qbe` and copies `third_party/qbe/qbe` to
`bin/qbe`. QBE's own Makefile is POSIX make; it generates `config.h`
(selecting the default `-t` target from `uname`) on first build, and compiles
with `-std=c99 -g -Wall -Wextra -Wpedantic` and no optimisation level. The
build products (`config.h`, `*.o`, `qbe`) are gitignored. `make clean` at the
top level also runs QBE's `clean` (which leaves `config.h`; use
`make -C third_party/qbe clean-gen` to remove that too).

### How to update

1. Fetch the new release: `git clone git://c9x.me/qbe.git && git checkout vX.Y`
   or download and extract the tarball from https://c9x.me/compile/release/.
2. `rm -rf third_party/qbe` and copy the fresh tree in.
3. `rm -rf third_party/qbe/minic third_party/qbe/tools third_party/qbe/test`.
4. Do not edit any file under `third_party/qbe/`. If a local patch is ever
   unavoidable, keep it as a `.patch` file next to this README and record it
   here so it can be re-applied on the next update.
5. Update the version number and tag in this file.
6. `make clean && make bin/qbe`, then re-run the spike in `docs/qbe-notes.md`
   and the language test suite to confirm the IL we emit still compiles.
