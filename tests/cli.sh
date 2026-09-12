#!/usr/bin/env bash
# Exercise the installed command names from outside the checkout.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_DIR="${RIVEL_BIN:-bin}"
case "$BIN_DIR" in /*) ;; *) BIN_DIR="$ROOT/$BIN_DIR" ;; esac
export PATH="$BIN_DIR:$PATH"
export RIVEL_HOME="${RIVEL_HOME:-$ROOT}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/rivel-cli.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir "$WORK/source files"
cd "$WORK/source files"

cat > main.rivel <<'RIVEL'
func main() -> int {
    for argument in args() { println(argument); }
    return 7;
}
RIVEL
cp main.rivel main.rv
printf 'hello world\n--help\n\n' > "$WORK/expected"

check_program() {
    local status=0
    "$@" 'hello world' '--help' '' > "$WORK/actual" || status=$?
    if [ "$status" -ne 7 ]; then
        echo "CLI: expected program exit status 7, got $status" >&2
        exit 1
    fi
    diff -u "$WORK/expected" "$WORK/actual"
}

for extension in rivel rv; do
    check_program rivel "main.$extension"
    test ! -e main
    test ! -e main.out
    rivelc "main.$extension" > "$WORK/compile.out"
    test ! -s "$WORK/compile.out"
    test -x main
    check_program ./main
    rm main
done
check_program rivelc run main.rv

printf 'func main() { missing(); }\n' > invalid.rv
if rivel invalid.rv > "$WORK/actual" 2> "$WORK/error"; then
    echo 'CLI: invalid source unexpectedly succeeded' >&2
    exit 1
fi
test ! -s "$WORK/actual"
test -s "$WORK/error"
rivel --help > "$WORK/help"
echo 'CLI tests passed: .rivel/.rv, PATH, arguments, exit status, compilation and errors'
