#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RIVELC="${RIVEL_BIN:-bin}/rivelc"
case "$RIVELC" in /*) ;; *) RIVELC="$ROOT/$RIVELC" ;; esac
export RIVEL_HOME="${RIVEL_HOME:-$ROOT}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/rivel-examples.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$RIVELC" ]; then
    echo "Compiler not found at $RIVELC; run make first." >&2
    exit 1
fi

count=0
for expected in "$ROOT"/examples/*.stdout; do
    source="${expected%.stdout}.rivel"
    name="$(basename "${source%.rivel}")"
    if ! "$RIVELC" -o "$WORK/example" "$source" >"$WORK/compile.out" 2>"$WORK/compile.err"; then
        echo "FAIL $name: compilation failed" >&2
        cat "$WORK/compile.err" >&2
        exit 1
    fi
    if [ -s "$WORK/compile.err" ]; then
        cat "$WORK/compile.err" >&2
        exit 1
    fi
    if ! "$WORK/example" >"$WORK/output" 2>"$WORK/error"; then
        echo "FAIL $name: program failed" >&2
        cat "$WORK/error" >&2
        exit 1
    fi
    if [ -s "$WORK/error" ]; then
        cat "$WORK/error" >&2
        exit 1
    fi
    diff -u "$expected" "$WORK/output"
    echo "  PASS $name"
    count=$((count + 1))
done
echo "$count standalone examples passed"

bash "$ROOT/examples/tic_tac_toe/test.sh"
