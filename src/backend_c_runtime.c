#include "backend_c_internal.h"

static bool backend_emit_runtime_division_helpers(Backend *backend) {
    if (!backend_emit_line(backend, "static void rivel_check_divisor(int64_t rhs) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "if (rhs == INT64_C(0)) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "fputs(\"division by zero\\n\", stderr);")
        || !backend_emit_line(backend, "exit(1);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static int64_t rivel_div(int64_t lhs, int64_t rhs) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "rivel_check_divisor(rhs);")
        || !backend_emit_line(backend, "return lhs / rhs;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static int64_t rivel_mod(int64_t lhs, int64_t rhs) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "rivel_check_divisor(rhs);")
        || !backend_emit_line(backend, "return lhs % rhs;")) {
        return false;
    }
    backend_indent_pop(backend);
    return backend_emit_line(backend, "}");
}

static bool backend_emit_runtime_print_helpers(Backend *backend) {
    if (!backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_print_int(int64_t value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "printf(\"%lld\\n\", (long long)value);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_print_bool(bool value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "puts(value ? \"true\" : \"false\");")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_print_double(double value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "printf(\"%.17g\\n\", value);")) {
        return false;
    }
    backend_indent_pop(backend);
    return backend_emit_line(backend, "}");
}

bool backend_emit_prelude(Backend *backend) {
    if (!backend_emit_line(backend, "#include <stdbool.h>")
        || !backend_emit_line(backend, "#include <stdint.h>")
        || !backend_emit_line(backend, "#include <math.h>")
        || !backend_emit_line(backend, "#include <stdio.h>")
        || !backend_emit_line(backend, "#include <stdlib.h>")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static int rivel_exit_code(int64_t value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "return (int)value;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")) {
        return false;
    }
    if (!backend_emit_runtime_division_helpers(backend)) {
        return false;
    }
    if (!backend_emit_runtime_print_helpers(backend)) {
        return false;
    }
    return backend_emit_line(backend, "");
}

bool backend_emit_program_entry(Backend *backend) {
    if (!backend_emit_line(backend, "int main(void) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "return rivel_exit_code(rivel_fn_main());")) {
        return false;
    }
    backend_indent_pop(backend);
    return backend_emit_line(backend, "}");
}
