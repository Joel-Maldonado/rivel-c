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

static bool backend_emit_runtime_string_helpers(Backend *backend) {
    if (!backend_emit_line(backend, "")
        || !backend_emit_line(backend, "typedef struct RivelStringStorage RivelStringStorage;")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "struct RivelStringStorage {")
        || !backend_emit_line(backend, "    size_t refcount;")
        || !backend_emit_line(backend, "    size_t len;")
        || !backend_emit_line(backend, "    char bytes[];")
        || !backend_emit_line(backend, "};")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "typedef struct {")
        || !backend_emit_line(backend, "    const char *data;")
        || !backend_emit_line(backend, "    size_t len;")
        || !backend_emit_line(backend, "    RivelStringStorage *storage;")
        || !backend_emit_line(backend, "} RivelString;")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelStringStorage *rivel_string_storage_new(size_t len) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "RivelStringStorage *storage = (RivelStringStorage *)malloc(sizeof(*storage) + len);")
        || !backend_emit_line(backend, "if (storage == NULL) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "fputs(\"out of memory\\n\", stderr);")
        || !backend_emit_line(backend, "exit(1);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "storage->refcount = 1U;")
        || !backend_emit_line(backend, "storage->len = len;")
        || !backend_emit_line(backend, "return storage;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelString rivel_string_retain(RivelString value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "if (value.storage != NULL) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "value.storage->refcount += 1U;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "return value;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_string_release(RivelString value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "if (value.storage == NULL) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "return;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "if (value.storage->refcount == 1U) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "free(value.storage);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "} else {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "value.storage->refcount -= 1U;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelString rivel_string_copy(const char *data, size_t len) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "RivelStringStorage *storage;")
        || !backend_emit_line(backend, "if (len == 0U) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "return (RivelString){\"\", 0U, NULL};")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "storage = rivel_string_storage_new(len);")
        || !backend_emit_line(backend, "memcpy(storage->bytes, data, len);")
        || !backend_emit_line(backend, "return (RivelString){storage->bytes, len, storage};")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_substring_out_of_range(void) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "fputs(\"substring out of range\\n\", stderr);")
        || !backend_emit_line(backend, "exit(1);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_equal(RivelString lhs, RivelString rhs) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "if (lhs.len != rhs.len) {")
        || !backend_emit_line(backend, "    return false;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "if (lhs.len == 0U) {")
        || !backend_emit_line(backend, "    return true;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "return memcmp(lhs.data, rhs.data, lhs.len) == 0;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_contains(RivelString haystack, RivelString needle) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "size_t index = 0U;")
        || !backend_emit_line(backend, "if (needle.len == 0U) {")
        || !backend_emit_line(backend, "    return true;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "if (needle.len > haystack.len) {")
        || !backend_emit_line(backend, "    return false;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "while (index + needle.len <= haystack.len) {")
        || !backend_emit_line(backend, "    if (memcmp(haystack.data + index, needle.data, needle.len) == 0) {")
        || !backend_emit_line(backend, "        return true;")
        || !backend_emit_line(backend, "    }")
        || !backend_emit_line(backend, "    index += 1U;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "return false;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_starts_with(RivelString value, RivelString prefix) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "if (prefix.len > value.len) {")
        || !backend_emit_line(backend, "    return false;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "if (prefix.len == 0U) {")
        || !backend_emit_line(backend, "    return true;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "return memcmp(value.data, prefix.data, prefix.len) == 0;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_ends_with(RivelString value, RivelString suffix) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "if (suffix.len > value.len) {")
        || !backend_emit_line(backend, "    return false;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "if (suffix.len == 0U) {")
        || !backend_emit_line(backend, "    return true;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "return memcmp(value.data + value.len - suffix.len, suffix.data, suffix.len) == 0;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelString rivel_string_concat_take(RivelString lhs, RivelString rhs) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "size_t len = lhs.len + rhs.len;")
        || !backend_emit_line(backend, "RivelString result;")
        || !backend_emit_line(backend, "if (len == 0U) {")
        || !backend_emit_line(backend, "    result = (RivelString){\"\", 0U, NULL};")
        || !backend_emit_line(backend, "} else {")
        || !backend_emit_line(backend, "    RivelStringStorage *storage = rivel_string_storage_new(len);")
        || !backend_emit_line(backend, "    if (lhs.len > 0U) {")
        || !backend_emit_line(backend, "        memcpy(storage->bytes, lhs.data, lhs.len);")
        || !backend_emit_line(backend, "    }")
        || !backend_emit_line(backend, "    if (rhs.len > 0U) {")
        || !backend_emit_line(backend, "        memcpy(storage->bytes + lhs.len, rhs.data, rhs.len);")
        || !backend_emit_line(backend, "    }")
        || !backend_emit_line(backend, "    result = (RivelString){storage->bytes, len, storage};")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "rivel_string_release(lhs);")
        || !backend_emit_line(backend, "rivel_string_release(rhs);")
        || !backend_emit_line(backend, "return result;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_eq_take(RivelString lhs, RivelString rhs) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "bool result = rivel_string_equal(lhs, rhs);")
        || !backend_emit_line(backend, "rivel_string_release(lhs);")
        || !backend_emit_line(backend, "rivel_string_release(rhs);")
        || !backend_emit_line(backend, "return result;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static int64_t rivel_string_len_take(RivelString value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "int64_t len = (int64_t)value.len;")
        || !backend_emit_line(backend, "rivel_string_release(value);")
        || !backend_emit_line(backend, "return len;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelString rivel_string_substr_take(RivelString value, int64_t start_value, int64_t len_value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "size_t start_index;")
        || !backend_emit_line(backend, "size_t len;")
        || !backend_emit_line(backend, "RivelString result;")
        || !backend_emit_line(backend, "if (start_value < 0 || len_value < 0) {")
        || !backend_emit_line(backend, "    rivel_string_release(value);")
        || !backend_emit_line(backend, "    rivel_substring_out_of_range();")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "start_index = (size_t)start_value;")
        || !backend_emit_line(backend, "len = (size_t)len_value;")
        || !backend_emit_line(backend, "if (start_index > value.len || len > value.len - start_index) {")
        || !backend_emit_line(backend, "    rivel_string_release(value);")
        || !backend_emit_line(backend, "    rivel_substring_out_of_range();")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "result.data = value.data + start_index;")
        || !backend_emit_line(backend, "result.len = len;")
        || !backend_emit_line(backend, "result.storage = value.storage;")
        || !backend_emit_line(backend, "if (result.storage != NULL) {")
        || !backend_emit_line(backend, "    result.storage->refcount += 1U;")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "rivel_string_release(value);")
        || !backend_emit_line(backend, "return result;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_contains_take(RivelString haystack, RivelString needle) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "bool result = rivel_string_contains(haystack, needle);")
        || !backend_emit_line(backend, "rivel_string_release(haystack);")
        || !backend_emit_line(backend, "rivel_string_release(needle);")
        || !backend_emit_line(backend, "return result;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_starts_with_take(RivelString value, RivelString prefix) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "bool result = rivel_string_starts_with(value, prefix);")
        || !backend_emit_line(backend, "rivel_string_release(value);")
        || !backend_emit_line(backend, "rivel_string_release(prefix);")
        || !backend_emit_line(backend, "return result;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static bool rivel_string_ends_with_take(RivelString value, RivelString suffix) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "bool result = rivel_string_ends_with(value, suffix);")
        || !backend_emit_line(backend, "rivel_string_release(value);")
        || !backend_emit_line(backend, "rivel_string_release(suffix);")
        || !backend_emit_line(backend, "return result;")) {
        return false;
    }
    backend_indent_pop(backend);
    return backend_emit_line(backend, "}");
}

static bool backend_emit_runtime_print_helpers(Backend *backend) {
    if (!backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelString rivel_string_from_int(int64_t value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "char buffer[32];")
        || !backend_emit_line(backend, "int len = snprintf(buffer, sizeof(buffer), \"%lld\", (long long)value);")
        || !backend_emit_line(backend, "if (len < 0) {")
        || !backend_emit_line(backend, "    fputs(\"failed to format int\\n\", stderr);")
        || !backend_emit_line(backend, "    exit(1);")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "return rivel_string_copy(buffer, (size_t)len);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelString rivel_string_from_bool(bool value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "return rivel_string_copy(value ? \"true\" : \"false\", value ? 4U : 5U);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static RivelString rivel_string_from_double(double value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "char buffer[64];")
        || !backend_emit_line(backend, "int len = snprintf(buffer, sizeof(buffer), \"%.17g\", value);")
        || !backend_emit_line(backend, "if (len < 0) {")
        || !backend_emit_line(backend, "    fputs(\"failed to format double\\n\", stderr);")
        || !backend_emit_line(backend, "    exit(1);")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "return rivel_string_copy(buffer, (size_t)len);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_write_string(RivelString value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "if (value.len > 0U) {")
        || !backend_emit_line(backend, "    fwrite(value.data, 1U, value.len, stdout);")
        || !backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "rivel_string_release(value);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_print_int(int64_t value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "RivelString text = rivel_string_from_int(value);")
        || !backend_emit_line(backend, "rivel_write_string(text);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_println_int(int64_t value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "rivel_print_int(value);")
        || !backend_emit_line(backend, "fputc('\\n', stdout);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_print_bool(bool value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "RivelString text = rivel_string_from_bool(value);")
        || !backend_emit_line(backend, "rivel_write_string(text);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_println_bool(bool value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "rivel_print_bool(value);")
        || !backend_emit_line(backend, "fputc('\\n', stdout);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_print_double(double value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "RivelString text = rivel_string_from_double(value);")
        || !backend_emit_line(backend, "rivel_write_string(text);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_println_double(double value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "rivel_print_double(value);")
        || !backend_emit_line(backend, "fputc('\\n', stdout);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_print_string_take(RivelString value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "rivel_write_string(value);")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend, "static void rivel_println_string_take(RivelString value) {")) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend, "rivel_print_string_take(value);")
        || !backend_emit_line(backend, "fputc('\\n', stdout);")) {
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
        || !backend_emit_line(backend, "#include <string.h>")
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
    if (!backend_emit_runtime_string_helpers(backend)) {
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
