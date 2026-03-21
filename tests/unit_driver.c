#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver.h"
#include "error.h"

static void write_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(contents, 1U, strlen(contents), file) == strlen(contents));
    assert(fclose(file) == 0);
}

static char *copy_env_value(const char *name) {
    const char *value = getenv(name);
    size_t len;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    len = strlen(value);
    copy = (char *)malloc(len + 1U);
    assert(copy != NULL);
    memcpy(copy, value, len + 1U);
    return copy;
}

static void restore_path(char *saved_path) {
    if (saved_path == NULL) {
        assert(unsetenv("PATH") == 0);
        return;
    }
    assert(setenv("PATH", saved_path, 1) == 0);
}

static void test_driver_emit_c_flag_controls_generated_c_artifact(void) {
    char input_template[] = "/tmp/rivel-driver-input-XXXXXX";
    char out_template[] = "/tmp/rivel-driver-out-XXXXXX";
    int input_fd;
    int out_fd;
    char *input_path;
    char *output_path;
    char emit_c_path[256];
    CompileError error;

    input_fd = mkstemp(input_template);
    assert(input_fd >= 0);
    assert(close(input_fd) == 0);

    out_fd = mkstemp(out_template);
    assert(out_fd >= 0);
    assert(close(out_fd) == 0);
    assert(remove(out_template) == 0);

    input_path = input_template;
    output_path = out_template;

    write_file(
        input_path,
        "fn main() -> Int {\n"
        "    return 42\n"
        "}\n");

    error_init(&error);
    assert(driver_compile_file(input_path, output_path, true, &error));
    (void)snprintf(emit_c_path, sizeof(emit_c_path), "%s.c", output_path);
    assert(access(emit_c_path, F_OK) == 0);
    assert(remove(emit_c_path) == 0);
    assert(remove(output_path) == 0);

    assert(driver_compile_file(input_path, output_path, false, &error));
    (void)snprintf(emit_c_path, sizeof(emit_c_path), "%s.c", output_path);
    assert(access(emit_c_path, F_OK) != 0);
    assert(remove(output_path) == 0);

    error_free(&error);
    assert(remove(input_path) == 0);
}

static void test_driver_compile_failure_cleans_temp_c_when_emit_c_is_disabled(void) {
    char input_template[] = "/tmp/rivel-driver-input-XXXXXX";
    char out_template[] = "/tmp/rivel-driver-out-XXXXXX";
    char temp_c_path[256];
    int input_fd;
    int out_fd;
    char *saved_path;
    CompileError error;

    input_fd = mkstemp(input_template);
    assert(input_fd >= 0);
    assert(close(input_fd) == 0);

    out_fd = mkstemp(out_template);
    assert(out_fd >= 0);
    assert(close(out_fd) == 0);
    assert(remove(out_template) == 0);

    write_file(
        input_template,
        "fn main() -> Int {\n"
        "    return 1\n"
        "}\n");

    saved_path = copy_env_value("PATH");
    assert(setenv("PATH", "", 1) == 0);

    error_init(&error);
    assert(!driver_compile_file(input_template, out_template, false, &error));
    assert(error.message != NULL);
    assert(strstr(error.message, "gcc") != NULL);

    (void)snprintf(temp_c_path, sizeof(temp_c_path), "%s.tmp.c", out_template);
    assert(access(temp_c_path, F_OK) != 0);
    assert(access(out_template, F_OK) != 0);

    restore_path(saved_path);
    free(saved_path);
    error_free(&error);
    assert(remove(input_template) == 0);
}

static void test_driver_compile_failure_keeps_c_when_emit_c_is_enabled(void) {
    char input_template[] = "/tmp/rivel-driver-input-XXXXXX";
    char out_template[] = "/tmp/rivel-driver-out-XXXXXX";
    char emit_c_path[256];
    int input_fd;
    int out_fd;
    char *saved_path;
    CompileError error;

    input_fd = mkstemp(input_template);
    assert(input_fd >= 0);
    assert(close(input_fd) == 0);

    out_fd = mkstemp(out_template);
    assert(out_fd >= 0);
    assert(close(out_fd) == 0);
    assert(remove(out_template) == 0);

    write_file(
        input_template,
        "fn main() -> Int {\n"
        "    return 2\n"
        "}\n");

    saved_path = copy_env_value("PATH");
    assert(setenv("PATH", "", 1) == 0);

    error_init(&error);
    assert(!driver_compile_file(input_template, out_template, true, &error));
    assert(error.message != NULL);
    assert(strstr(error.message, "gcc") != NULL);

    (void)snprintf(emit_c_path, sizeof(emit_c_path), "%s.c", out_template);
    assert(access(emit_c_path, F_OK) == 0);
    assert(remove(emit_c_path) == 0);
    assert(access(out_template, F_OK) != 0);

    restore_path(saved_path);
    free(saved_path);
    error_free(&error);
    assert(remove(input_template) == 0);
}

int main(void) {
    test_driver_emit_c_flag_controls_generated_c_artifact();
    test_driver_compile_failure_cleans_temp_c_when_emit_c_is_disabled();
    test_driver_compile_failure_keeps_c_when_emit_c_is_enabled();
    return 0;
}
