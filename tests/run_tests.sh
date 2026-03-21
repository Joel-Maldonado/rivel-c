#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RIVEL="$PROJECT_DIR/rivel"

pass=0
fail=0
errors=""
artifact_error=""

echo "Building..."
make -C "$PROJECT_DIR" clean > /dev/null 2>&1
make -C "$PROJECT_DIR" > /dev/null 2>&1

record_pass() {
    local name="$1"
    local detail="$2"
    echo "  PASS  $name ($detail)"
    pass=$((pass + 1))
}

record_fail() {
    local name="$1"
    local detail="$2"
    echo "  FAIL  $name ($detail)"
    errors="$errors\n  FAIL  $name ($detail)"
    fail=$((fail + 1))
}

compile_test() {
    local test_dir="$1"
    local file="$2"
    shift 2

    (
        cd "$test_dir" &&
        "$RIVEL" "$file" "$@" > /dev/null 2> "$test_dir/stderr.txt"
    )
}

check_artifacts() {
    local test_dir="$1"
    local expect_c="${2:-false}"

    artifact_error=""
    if [ "$expect_c" = "true" ]; then
        if [ ! -f "$test_dir/out.c" ]; then
            artifact_error="missing out.c"
            return 1
        fi
    elif [ -f "$test_dir/out.c" ]; then
        artifact_error="unexpected out.c"
        return 1
    fi
    if [ ! -x "$test_dir/out" ]; then
        artifact_error="missing out executable"
        return 1
    fi
}

compile_and_run() {
    local name="$1"
    local file="$2"

    test_dir="$(mktemp -d "${TMPDIR:-/tmp}/rivel-test.XXXXXX")"

    if ! compile_test "$test_dir" "$file"; then
        run_stderr="$(cat "$test_dir/stderr.txt")"
        rm -rf "$test_dir"
        record_fail "$name" "compilation failed: $run_stderr"
        return 1
    fi
    compile_stderr="$(cat "$test_dir/stderr.txt")"
    if ! check_artifacts "$test_dir"; then
        rm -rf "$test_dir"
        record_fail "$name" "$artifact_error"
        return 1
    fi

    set +e
    "$test_dir/out" > "$test_dir/runtime-stdout.txt" 2> "$test_dir/runtime-stderr.txt"
    run_exit_code=$?
    set -e
    run_stdout="$(cat "$test_dir/runtime-stdout.txt")"
    run_stderr="$(cat "$test_dir/runtime-stderr.txt")"
    rm -rf "$test_dir"
}

run_clean_success_test() {
    local name="$1"
    local file="$2"
    local expected="$3"

    if ! compile_and_run "$name" "$file"; then return; fi

    if [ -n "$compile_stderr" ]; then
        record_fail "$name" "unexpected compile stderr: $compile_stderr"
    elif [ "$run_exit_code" -eq "$expected" ] && [ -z "$run_stdout" ] && [ -z "$run_stderr" ]; then
        record_pass "$name" "exit=$run_exit_code"
    elif [ "$run_exit_code" -ne "$expected" ]; then
        record_fail "$name" "expected=$expected, got=$run_exit_code"
    elif [ -n "$run_stdout" ]; then
        record_fail "$name" "unexpected stdout: $run_stdout"
    else
        record_fail "$name" "unexpected stderr: $run_stderr"
    fi
}

run_success_test() {
    local name="$1"
    local file="$2"
    local expected="$3"

    if ! compile_and_run "$name" "$file"; then return; fi

    if [ "$run_exit_code" -eq "$expected" ] && [ -z "$run_stdout" ] && [ -z "$run_stderr" ]; then
        record_pass "$name" "exit=$run_exit_code"
    elif [ "$run_exit_code" -ne "$expected" ]; then
        record_fail "$name" "expected=$expected, got=$run_exit_code"
    elif [ -n "$run_stdout" ]; then
        record_fail "$name" "unexpected stdout: $run_stdout"
    else
        record_fail "$name" "unexpected stderr: $run_stderr"
    fi
}

run_stdout_test() {
    local name="$1"
    local file="$2"
    local expected_exit="$3"
    local expected_stdout="$4"

    if ! compile_and_run "$name" "$file"; then return; fi

    if [ "$run_exit_code" -eq "$expected_exit" ] && [ "$run_stdout" = "$expected_stdout" ] && [ -z "$run_stderr" ]; then
        record_pass "$name" "exit=$run_exit_code"
    elif [ "$run_exit_code" -ne "$expected_exit" ]; then
        record_fail "$name" "expected exit=$expected_exit, got=$run_exit_code"
    elif [ "$run_stdout" != "$expected_stdout" ]; then
        record_fail "$name" "expected stdout='$expected_stdout', got='$run_stdout'"
    else
        record_fail "$name" "unexpected stderr: $run_stderr"
    fi
}

run_runtime_error_test() {
    local name="$1"
    local file="$2"
    local expected_exit="$3"
    local expected_stderr="$4"

    if ! compile_and_run "$name" "$file"; then return; fi

    if [ "$run_exit_code" -eq "$expected_exit" ] && [ "$run_stderr" = "$expected_stderr" ]; then
        record_pass "$name" "exit=$run_exit_code"
    elif [ "$run_exit_code" -ne "$expected_exit" ]; then
        record_fail "$name" "expected exit=$expected_exit, got=$run_exit_code"
    else
        record_fail "$name" "expected stderr='$expected_stderr', got='$run_stderr'"
    fi
}

run_emit_c_flag_test() {
    local name="$1"
    local file="$2"
    local out_name="$3"
    local expected="$4"

    test_dir="$(mktemp -d "${TMPDIR:-/tmp}/rivel-test.XXXXXX")"

    if ! (cd "$test_dir" && "$RIVEL" "$file" -o "$out_name" --emit-c > /dev/null 2> "$test_dir/stderr.txt"); then
        run_stderr="$(cat "$test_dir/stderr.txt")"
        rm -rf "$test_dir"
        record_fail "$name" "compilation failed: $run_stderr"
        return
    fi

    if [ ! -f "$test_dir/${out_name}.c" ]; then
        rm -rf "$test_dir"
        record_fail "$name" "missing ${out_name}.c"
        return
    fi
    if [ ! -x "$test_dir/${out_name}" ]; then
        rm -rf "$test_dir"
        record_fail "$name" "missing ${out_name} executable"
        return
    fi

    set +e
    "$test_dir/$out_name" > /dev/null 2>&1
    run_exit_code=$?
    set -e
    rm -rf "$test_dir"

    if [ "$run_exit_code" -eq "$expected" ]; then
        record_pass "$name" "exit=$run_exit_code"
    else
        record_fail "$name" "expected=$expected, got=$run_exit_code"
    fi
}

run_compile_fail_test() {
    local name="$1"
    local file="$2"
    local expected_substring="$3"

    test_dir="$(mktemp -d "${TMPDIR:-/tmp}/rivel-test.XXXXXX")"

    set +e
    (cd "$test_dir" && "$RIVEL" "$file" > /dev/null 2> "$test_dir/stderr.txt")
    compile_exit=$?
    set -e
    compile_stderr="$(cat "$test_dir/stderr.txt")"
    rm -rf "$test_dir"

    if [ "$compile_exit" -eq 0 ]; then
        record_fail "$name" "expected compile failure"
    elif [[ "$compile_stderr" == *"$expected_substring"* ]]; then
        record_pass "$name" "matched diagnostic"
    else
        record_fail "$name" "expected diagnostic containing '$expected_substring', got '$compile_stderr'"
    fi
}

echo "=== Rivel V1 Compiler Tests ==="
echo ""

echo "--- Positive ---"
run_success_test "main_literal"        "$SCRIPT_DIR/pass_main_literal.rivel"         42
run_success_test "bindings"            "$SCRIPT_DIR/pass_bindings.rivel"             42
run_success_test "semicolons"          "$SCRIPT_DIR/pass_semicolons.rivel"           42
run_success_test "unary_and_mod"       "$SCRIPT_DIR/pass_unary_and_mod.rivel"        7
run_success_test "logic"               "$SCRIPT_DIR/pass_logic_and_comparisons.rivel" 42
run_success_test "if_chain"            "$SCRIPT_DIR/pass_if_chain.rivel"             42
run_success_test "while_loop"          "$SCRIPT_DIR/pass_while.rivel"                21
run_success_test "for_range_exclusive" "$SCRIPT_DIR/pass_for_range_exclusive.rivel"  21
run_success_test "for_range_inclusive" "$SCRIPT_DIR/pass_for_range_inclusive.rivel"  21
run_success_test "for_shadowing"       "$SCRIPT_DIR/pass_for_shadowing.rivel"        33
run_success_test "for_capture_once"    "$SCRIPT_DIR/pass_for_capture_once.rivel"     6
run_success_test "forward_call"        "$SCRIPT_DIR/pass_forward_call.rivel"         42
run_success_test "recursion"           "$SCRIPT_DIR/pass_recursion.rivel"            120
run_success_test "globals"             "$SCRIPT_DIR/pass_globals.rivel"              42
run_success_test "shadowing"           "$SCRIPT_DIR/pass_shadowing.rivel"            42
run_success_test "call_stmt"           "$SCRIPT_DIR/pass_call_statement.rivel"       42
run_success_test "c_style_comments"    "$SCRIPT_DIR/pass_c_style_comments.rivel"     42
run_clean_success_test "equality_condition" "$SCRIPT_DIR/pass_equality_condition.rivel" 42
run_stdout_test  "print_int"           "$SCRIPT_DIR/pass_print_int.rivel"            7 $'42'
run_stdout_test  "print_bool"          "$SCRIPT_DIR/pass_print_bool.rivel"           0 $'true\nfalse'
run_stdout_test  "doubles"             "$SCRIPT_DIR/pass_doubles.rivel"              0 $'3.5\n2.25\ntrue\ntrue'
run_stdout_test  "string_features"     "$SCRIPT_DIR/pass_string_features.rivel"      5 $'hello world\nworld\ntrue\ntrue\ntrue\nworld!'
run_stdout_test  "string_bindings"     "$SCRIPT_DIR/pass_string_bindings.rivel"      2 $'ab\nac'

echo ""
echo "--- Runtime Errors ---"
run_runtime_error_test "division_by_zero" "$SCRIPT_DIR/runtime_div_zero.rivel"       1 "division by zero"
run_runtime_error_test "substr_out_of_range" "$SCRIPT_DIR/runtime_substr_oob.rivel" 1 "substring out of range"

echo ""
echo "--- Compile Failures ---"
run_compile_fail_test "legacy_let"           "$SCRIPT_DIR/fail/legacy_let.rivel"             "Rivel v1 does not support legacy keyword \`let\`"
run_compile_fail_test "legacy_exit"          "$SCRIPT_DIR/fail/legacy_exit.rivel"            "Rivel v1 does not support legacy keyword \`exit\`"
run_compile_fail_test "legacy_hash_comment"  "$SCRIPT_DIR/fail/legacy_hash_comment.rivel"    "Unexpected character \`#\`"
run_compile_fail_test "legacy_if_paren"      "$SCRIPT_DIR/fail/legacy_if_paren.rivel"        "Parenthesized conditions are not supported in Rivel v1"
run_compile_fail_test "top_level_separator"  "$SCRIPT_DIR/fail/top_level_separator.rivel"    "Expected a declaration separator after top-level declaration"
run_compile_fail_test "missing_main"         "$SCRIPT_DIR/fail/missing_main.rivel"           "missing entrypoint \`main\`"
run_compile_fail_test "top_level_mut"        "$SCRIPT_DIR/fail/top_level_mut.rivel"          "Top-level \`mut\` declarations are not part of Rivel v1"
run_compile_fail_test "assign_const"         "$SCRIPT_DIR/fail/assign_const.rivel"           "Cannot assign to immutable binding \`x\`"
run_compile_fail_test "non_bool_cond"        "$SCRIPT_DIR/fail/non_bool_cond.rivel"          "If condition must be Bool"
run_compile_fail_test "int_literal_overflow" "$SCRIPT_DIR/fail/int_literal_overflow.rivel"   "Integer literal out of range for Int"
run_compile_fail_test "type_mismatch_assign" "$SCRIPT_DIR/fail/type_mismatch_assign.rivel"   "Cannot assign value of type Bool to Int"
run_compile_fail_test "type_mismatch_double_assign" "$SCRIPT_DIR/fail/type_mismatch_double_assign.rivel" "Cannot assign value of type Double to Int"
run_compile_fail_test "type_mismatch_return" "$SCRIPT_DIR/fail/type_mismatch_return.rivel"   "Return type mismatch: expected Int but got Bool"
run_compile_fail_test "type_mismatch_op"     "$SCRIPT_DIR/fail/type_mismatch_operator.rivel" "Operator \`+\` expects numeric operands or String operands"
run_compile_fail_test "type_mismatch_string_op" "$SCRIPT_DIR/fail/type_mismatch_string_operator.rivel" "Operator \`+\` expects numeric operands or String operands"
run_compile_fail_test "type_mismatch_double_mod" "$SCRIPT_DIR/fail/type_mismatch_double_mod.rivel" "Operator \`%\` expects Int operands"
run_compile_fail_test "duplicate_scope"      "$SCRIPT_DIR/fail/duplicate_same_scope.rivel"   "Binding \`x\` is already declared in this scope"
run_compile_fail_test "reserved_print"       "$SCRIPT_DIR/fail/redefine_print.rivel"         "Top-level name \`print\` is reserved for a builtin"
run_compile_fail_test "unsupported_import"   "$SCRIPT_DIR/fail/unsupported_import.rivel"     "Rivel v1 does not support \`import\`"
run_compile_fail_test "unsupported_double_exponent" "$SCRIPT_DIR/fail/unsupported_double_exponent.rivel" "Exponent notation is not part of Rivel v1 doubles"
run_compile_fail_test "invalid_string_escape" "$SCRIPT_DIR/fail/invalid_string_escape.rivel" "Unsupported escape sequence"
run_compile_fail_test "builtin_arity_len"    "$SCRIPT_DIR/fail/builtin_arity_len.rivel"      "Builtin \`len\` expects 1 argument(s)"
run_compile_fail_test "builtin_type_contains" "$SCRIPT_DIR/fail/builtin_type_contains.rivel"  "Argument 2 to builtin \`contains\` has type Int, expected String"
run_compile_fail_test "unsupported_list"     "$SCRIPT_DIR/fail/unsupported_list.rivel"       "List syntax is not part of Rivel v1"
run_compile_fail_test "for_start_not_int"    "$SCRIPT_DIR/fail/for_start_not_int.rivel"      "For range start must be Int"
run_compile_fail_test "for_end_not_int"      "$SCRIPT_DIR/fail/for_end_not_int.rivel"        "For range end must be Int"
run_compile_fail_test "assign_loop_var"      "$SCRIPT_DIR/fail/assign_loop_var.rivel"        "Cannot assign to immutable binding \`i\`"
run_compile_fail_test "for_missing_range"    "$SCRIPT_DIR/fail/for_missing_range.rivel"      "Expected \`..\` or \`..=\` in \`for\` range"
run_compile_fail_test "unsupported_member"   "$SCRIPT_DIR/fail/unsupported_member_access.rivel" "Member access is not part of Rivel v1"
run_compile_fail_test "unterminated_block_comment" "$SCRIPT_DIR/fail/unterminated_block_comment.rivel" "Unterminated block comment"

echo ""
echo "--- Emit C Flag ---"
run_emit_c_flag_test "emit_c_flag" "$SCRIPT_DIR/pass_main_literal.rivel" "mytest" 42
run_emit_c_flag_test "emit_c_flag_spaces" "$SCRIPT_DIR/pass_main_literal.rivel" "my app" 42

echo ""
echo "=== Results: $pass passed, $fail failed ==="
if [ "$fail" -gt 0 ]; then
    echo -e "\nFailures:$errors"
    exit 1
fi
