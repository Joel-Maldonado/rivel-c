# third_party

Vendored dependencies, with upstream versions, licenses, and local patches
recorded below.

## qbe/ — QBE 1.2

- Upstream: https://c9x.me/compile/ (git: `git://c9x.me/qbe.git`, tag `v1.2`;
  tarball: https://c9x.me/compile/release/qbe-1.2.tar.xz)
- License: MIT, copyright Quentin Carbonneaux — see `qbe/LICENSE`. That file
  applies to everything under `qbe/`.
- Based on the upstream release, with the build compatibility patch below.

### Local patch

[`qbe-build-warnings.patch`](qbe-build-warnings.patch) is already applied to
the checked-in sources. It gives parameterless functions explicit `(void)`
prototypes, replaces `sprintf` with bounded `snprintf`, and passes parser
diagnostics directly to QBE's existing variadic error function. This removes
modern Clang warnings without disabling compiler diagnostics.

To reapply it to a fresh upstream copy, run from the repository root:

```sh
git apply --unidiff-zero third_party/qbe-build-warnings.patch
```

The patch uses zero-context hunks. Review and refresh it when updating QBE;
an upstream version may already include equivalent changes.

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

The top-level `Makefile` has a `bin/qbe` target that runs QBE's own Makefile
and copies `third_party/qbe/qbe` to `bin/qbe`. It passes through `CC` and
`CFLAGS`, including sanitizer flags, and adds `-std=c99 -fwrapv -Wall -Wextra
-Wpedantic`. C99 keeps QBE's `asm` field name valid; `-fwrapv` defines the
wrapping signed arithmetic used by its integer constants and alias offsets.
Both ASan and UBSan remain enabled in sanitizer builds.

QBE's own Makefile is POSIX make; it generates `config.h` (selecting the
default `-t` target from `uname`) on first build. The build products
(`config.h`, `*.o`, `qbe`) are gitignored. `make clean` at the
top level also runs QBE's `clean` (which leaves `config.h`; use
`make -C third_party/qbe clean-gen` to remove that too).

### How to update

1. Fetch the new release: `git clone git://c9x.me/qbe.git && git checkout vX.Y`
   or download and extract the tarball from https://c9x.me/compile/release/.
2. `rm -rf third_party/qbe` and copy the fresh tree in.
3. `rm -rf third_party/qbe/minic third_party/qbe/tools third_party/qbe/test`.
4. Review and reapply the local patch above if it is still needed. Record any
   further source changes in a `.patch` file here so they survive updates.
5. Update the version number and tag in this file.
6. `make clean && make bin/qbe`, then re-run the spike in `docs/qbe-notes.md`
   and the language test suite to confirm the IL we emit still compiles.
