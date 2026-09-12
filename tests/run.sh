#!/usr/bin/env bash
# Rivel language test runner.
#
# Every tests/cases/**/*.rivel is a test. Expectations live in the file as
# comment directives:
#
#   // exit: N          the program must exit with status N (default 0)
#   // error: TEXT      compilation must fail and stderr must contain TEXT (repeatable)
#   // warn: TEXT       compilation must succeed and stderr must contain TEXT (repeatable)
#   // panic: TEXT      the program must exit 101 and stderr must contain TEXT
#   // args: A B C      command-line arguments passed to the program
#   // stdin: TEXT      text fed to the program's standard input (\n allowed)
#
# Standard output is compared with NAME.stdout next to the test when that
# file exists, and must be empty otherwise.
#
# Usage: tests/run.sh [filter]   (filter is a substring of the test path)
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RIVELC="${RIVEL_BIN:-bin}/rivelc"
case "$RIVELC" in /*) ;; *) RIVELC="$ROOT/$RIVELC" ;; esac
export RIVEL_HOME="${RIVEL_HOME:-$ROOT}"
FILTER="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/rivel-tests.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
failures=()

directive() { # file name -> values, one per line
    sed -n "s|^// $2: \{0,1\}||p" "$1"
}

check_contains() { # haystack-file needle label
    if ! grep -qF -- "$2" "$1"; then
        echo "    expected stderr to contain: $2"
        echo "    stderr was:"
        sed 's/^/      /' "$1"
        return 1
    fi
}

run_case() {
    local src="$1"
    local name="${src#$ROOT/tests/cases/}"
    local exe="$WORK/prog"
    local cerr="$WORK/compile.err"
    local out="$WORK/run.out"
    local rerr="$WORK/run.err"
    local ok=1
    local expected_exit errors warns panic args stdin

    expected_exit="$(directive "$src" exit)"
    errors="$(directive "$src" error)"
    warns="$(directive "$src" warn)"
    panic="$(directive "$src" panic)"
    args="$(directive "$src" args)"
    stdin="$(directive "$src" stdin)"
    : "${expected_exit:=0}"

    rm -f "$exe"
    "$RIVELC" -o "$exe" "$src" >"$WORK/compile.out" 2>"$cerr"
    local cstatus=$?

    if [ -n "$errors" ]; then
        if [ "$cstatus" -eq 0 ]; then
            echo "  FAIL $name: expected a compile error, but it compiled"
            ok=0
        else
            while IFS= read -r needle; do
                check_contains "$cerr" "$needle" || ok=0
            done <<<"$errors"
        fi
    else
        if [ "$cstatus" -ne 0 ]; then
            echo "  FAIL $name: did not compile"
            sed 's/^/      /' "$cerr"
            ok=0
        else
            if [ -n "$warns" ]; then
                while IFS= read -r needle; do
                    check_contains "$cerr" "$needle" || ok=0
                done <<<"$warns"
            elif [ -s "$cerr" ]; then
                echo "  FAIL $name: unexpected compiler output"
                sed 's/^/      /' "$cerr"
                ok=0
            fi
            # shellcheck disable=SC2086
            printf '%b' "$stdin" | "$exe" $args >"$out" 2>"$rerr"
            local rstatus=$?
            if [ -n "$panic" ]; then
                if [ "$rstatus" -ne 101 ]; then
                    echo "  FAIL $name: expected a panic (exit 101), got exit $rstatus"
                    ok=0
                fi
                check_contains "$rerr" "$panic" || ok=0
            else
                if [ "$rstatus" -ne "$expected_exit" ]; then
                    echo "  FAIL $name: expected exit $expected_exit, got $rstatus"
                    sed 's/^/      /' "$rerr"
                    ok=0
                fi
                if [ -s "$rerr" ]; then
                    echo "  FAIL $name: unexpected stderr"
                    sed 's/^/      /' "$rerr"
                    ok=0
                fi
            fi
            local expected_out="${src%.rivel}.stdout"
            if [ -f "$expected_out" ]; then
                if ! diff -u "$expected_out" "$out" >"$WORK/diff"; then
                    echo "  FAIL $name: stdout differs"
                    sed 's/^/      /' "$WORK/diff"
                    ok=0
                fi
            elif [ -s "$out" ]; then
                echo "  FAIL $name: unexpected stdout"
                sed 's/^/      /' "$out"
                ok=0
            fi
        fi
    fi

    if [ "$ok" -eq 1 ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failures+=("$name")
    fi
}

if [ ! -x "$RIVELC" ]; then
    echo "tests/run.sh: compiler not found at $RIVELC; run make first" >&2
    exit 2
fi

while IFS= read -r src; do
    case "$src" in *"$FILTER"*) run_case "$src" ;; esac
done < <(find "$ROOT/tests/cases" -name '*.rivel' | sort)

echo "$pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then
    printf '  %s\n' "${failures[@]}"
    exit 1
fi
