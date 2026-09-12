#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/.build/tests"

mkdir -p "$BUILD_DIR"

SRC_FILES=()
while IFS= read -r file; do
    SRC_FILES+=("$file")
done < <(find "$PROJECT_DIR/src" -name '*.c' ! -name 'main.c' | sort)

TEST_FILES=()
while IFS= read -r file; do
    TEST_FILES+=("$file")
done < <(find "$SCRIPT_DIR" -maxdepth 1 -name 'unit_*.c' | sort)

compile_test() {
    local test_file="$1"
    local test_name
    local output

    test_name="$(basename "${test_file%.c}")"
    output="$BUILD_DIR/$test_name"

    cc -std=c11 -Wall -Wextra -Werror -pedantic -g \
        -I"$PROJECT_DIR/src" \
        "${SRC_FILES[@]}" \
        "$test_file" \
        -o "$output"
}

run_test() {
    local test_file="$1"
    local test_name
    local output

    test_name="$(basename "${test_file%.c}")"
    output="$BUILD_DIR/$test_name"

    echo "  RUN   $test_name"
    "$output"
}

echo "=== Unit Tests ==="

for test_file in "${TEST_FILES[@]}"; do
    compile_test "$test_file"
done

for test_file in "${TEST_FILES[@]}"; do
    run_test "$test_file"
done

echo ""
echo "Unit tests passed."
