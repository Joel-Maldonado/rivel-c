#!/usr/bin/env bash
# Compile and exercise the game without leaving generated files in examples/.
set -u
set -o pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RIVELC="${RIVEL_BIN:-bin}/rivelc"
case "$RIVELC" in /*) ;; *) RIVELC="$ROOT/$RIVELC" ;; esac
export RIVEL_HOME="${RIVEL_HOME:-$ROOT}"

if [ ! -x "$RIVELC" ]; then
    echo "Compiler not found at $RIVELC; run make first." >&2
    exit 2
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rivel-tic-tac-toe.XXXXXX")" || exit 1
trap 'rm -rf "$WORK"' EXIT
EXE="$WORK/tic-tac-toe"

if ! "$RIVELC" -o "$EXE" "$ROOT/examples/tic_tac_toe/main.rivel" >"$WORK/compile.out" 2>"$WORK/compile.err"; then
    echo "FAIL: game did not compile" >&2
    cat "$WORK/compile.out" "$WORK/compile.err" >&2
    exit 1
fi
if [ ! -x "$EXE" ] || [ -s "$WORK/compile.err" ]; then
    echo "FAIL: compilation did not produce a clean executable" >&2
    cat "$WORK/compile.out" "$WORK/compile.err" >&2
    exit 1
fi

passed=0
failed=0
case_name=""
case_failed=0

fail_case() {
    echo "    $*" >&2
    case_failed=1
}

run_case() {
    case_name="$1"
    local input="$2"
    local expected_status="$3"
    shift 3
    case_failed=0
    printf '%b' "$input" >"$WORK/input"
    "$EXE" "$@" <"$WORK/input" >"$WORK/output" 2>"$WORK/error"
    local status=$?
    if [ "$status" -ne "$expected_status" ]; then
        fail_case "Expected exit $expected_status, got $status."
    fi
    if [ "$expected_status" -eq 0 ] && [ -s "$WORK/error" ]; then
        fail_case "Unexpected standard error."
    fi
}

expect_text() {
    if ! grep -qF -- "$1" "$WORK/output"; then
        fail_case "Expected output to contain: $1"
    fi
}

expect_error() {
    if ! grep -qF -- "$1" "$WORK/error"; then
        fail_case "Expected stderr to contain: $1"
    fi
}

expect_pattern() {
    if ! grep -qE -- "$1" "$WORK/output"; then
        fail_case "Expected output to match: $1"
    fi
}

reject_text() {
    if grep -qF -- "$1" "$WORK/output"; then
        fail_case "Unexpected output: $1"
    fi
}

expect_count() {
    local count
    count="$(grep -cF -- "$2" "$WORK/output" || true)"
    if [ "$count" -ne "$1" ]; then
        fail_case "Expected $1 occurrences of '$2', got $count."
    fi
}

finish_case() {
    if [ "$case_failed" -eq 0 ]; then
        echo "PASS $case_name"
        passed=$((passed + 1))
    else
        echo "FAIL $case_name" >&2
        sed 's/^/    /' "$WORK/output" >&2
        sed 's/^/    /' "$WORK/error" >&2
        failed=$((failed + 1))
    fi
}

run_case "command-line help" "" 0 --help
expect_text "--two-player"
expect_text "--ai-first"
expect_text "--demo"
expect_text "--self-test"
finish_case

run_case "unknown option" "" 1 --unknown
expect_error "Unknown option: --unknown. Use --help."
finish_case

run_case "incompatible player options" "" 1 --two-player --ai-first
expect_error "Choose only one mode. Use --help."
finish_case

run_case "X horizontal win" '1\n4\n2\n5\n3\nn\n' 0 --two-player
expect_text "X wins!"
expect_text "Score: X 1 | O 0 | Draws 0"
finish_case

run_case "X vertical win" '1\n2\n4\n5\n7\nn\n' 0 --two-player
expect_text "X wins!"
finish_case

run_case "X diagonal win" '1\n2\n5\n3\n9\nn\n' 0 --two-player
expect_text "X wins!"
finish_case

run_case "O horizontal win" '1\n4\n2\n5\n9\n6\nn\n' 0 --two-player
expect_text "O wins!"
expect_text "Score: X 0 | O 1 | Draws 0"
finish_case

run_case "O vertical win" '1\n2\n3\n5\n9\n8\nn\n' 0 --two-player
expect_text "O wins!"
finish_case

run_case "O diagonal win" '1\n3\n2\n5\n4\n7\nn\n' 0 --two-player
expect_text "O wins!"
finish_case

run_case "full-board draw" '1\n2\n3\n5\n4\n6\n8\n7\n9\nn\n' 0 --two-player
expect_text "It's a draw."
expect_text "Score: X 0 | O 0 | Draws 1"
reject_text "wins!"
finish_case

run_case "replay resets the board and preserves scores" '1\n4\n2\n5\n3\ny\n1\n4\n2\n5\n9\n6\nn\n' 0 --two-player
expect_count 1 "X wins!"
expect_count 1 "O wins!"
expect_text "Score: X 1 | O 1 | Draws 0"
reject_text "That square is occupied."
finish_case

run_case "invalid and occupied squares recover" '\ngarbage\n0\n10\n1\n1\nhelp\nq\n' 0 --two-player
expect_count 4 "Enter a number from 1 to 9."
expect_count 1 "That square is occupied."
expect_text "hint"
expect_text "undo"
expect_text "quit"
finish_case

run_case "two-player hints and undo preserve the turn" 'undo\nhint\n1\nundo\n1\n2\n4\n5\n7\nn\n' 0 --two-player
expect_text "Nothing to undo yet."
expect_text "Undid your last turn."
expect_pattern 'Hint: play [1-9]\.'
expect_text "X wins!"
reject_text "That square is occupied."
finish_case

run_case "computer undo restores the human turn" 'undo\n1\nundo\n1\nq\n' 0
expect_text "Nothing to undo yet."
expect_text "Undid your last turn."
expect_pattern 'Computer O plays [1-9]\.'
reject_text "That square is occupied."
finish_case

run_case "computer makes the opening move" 'hint\nq\n' 0 --ai-first
expect_count 1 "Computer X plays "
expect_pattern 'Hint: play [1-9]\.'
finish_case

run_case "undo preserves the computer's opening move" 'undo\n1\nundo\n1\nq\n' 0 --ai-first
expect_count 1 "Nothing to undo yet."
expect_count 1 "Undid your last turn."
expect_count 3 "Computer X plays "
reject_text "That square is occupied."
finish_case

run_case "computer completes a game" '1\n2\n3\n4\n5\n6\n7\n8\n9\nn\n' 0
expect_pattern 'Computer O plays [1-9]\.'
expect_pattern "O wins!|It's a draw\."
reject_text "X wins!"
finish_case

run_case "quit command" 'quit\n' 0 --two-player
reject_text "wins!"
finish_case

run_case "short quit command and numbered board" 'q\n' 0
for square in 1 2 3 4 5 6 7 8 9; do
    expect_pattern "(^|[^[:digit:]])$square([^[:digit:]]|$)"
done
finish_case

run_case "EOF before a move" "" 0
finish_case

run_case "EOF during a round" '1\n' 0 --two-player
reject_text "wins!"
finish_case

run_case "EOF at the rematch prompt" '1\n4\n2\n5\n3\n' 0 --two-player
expect_text "X wins!"
expect_text "Thanks for playing."
expect_count 1 "Score: X 1 | O 0 | Draws 0"
finish_case

run_case "AI demonstration ends in a draw" "" 0 --demo
expect_count 1 "It's a draw."
reject_text "wins!"
finish_case

run_case "exhaustive strategy self-test" "" 0 --self-test
expect_text "Self-test passed:"
finish_case

echo "$passed passed, $failed failed"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
