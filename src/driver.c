#include "driver.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "arena.h"
#include "backend_c.h"
#include "parser.h"
#include "semantic.h"
#include "strbuf.h"
#include "tokenizer.h"

extern char **environ;

static const char HOST_C_COMPILER[] = "gcc";

typedef struct {
    Arena arena;
    TokenList tokens;
    Program program;
    SemanticResult *semantics;
    StrBuf generated_c;
} DriverPipeline;

static bool read_entire_file(const char *path, char **out_contents, CompileError *error) {
    FILE *file = fopen(path, "rb");
    long size;
    size_t read_size;
    char *buffer;

    if (file == NULL) {
        return error_set(error, NULL, "Failed to open input file: %s", path);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }

    buffer = (char *)malloc((size_t)size + 1U);
    if (buffer == NULL) {
        fclose(file);
        return error_set_oom(error, NULL);
    }

    read_size = fread(buffer, 1U, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        free(buffer);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }

    buffer[size] = '\0';
    *out_contents = buffer;
    return true;
}

static bool write_entire_file(const char *path, const char *contents, CompileError *error) {
    FILE *file = fopen(path, "wb");
    size_t len = strlen(contents);

    if (file == NULL) {
        return error_set(error, NULL, "Failed to open output file: %s", path);
    }
    if (fwrite(contents, 1U, len, file) != len) {
        fclose(file);
        return error_set(error, NULL, "Failed to write output file: %s", path);
    }
    fclose(file);
    return true;
}

static char *join_suffix(const char *base, const char *suffix, CompileError *error) {
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    char *joined = (char *)malloc(base_len + suffix_len + 1U);
    if (joined == NULL) {
        error_set_oom(error, NULL);
        return NULL;
    }
    (void)snprintf(joined, base_len + suffix_len + 1U, "%s%s", base, suffix);
    return joined;
}

static void driver_pipeline_init(DriverPipeline *pipeline) {
    arena_init(&pipeline->arena, 4096U);
    token_list_init(&pipeline->tokens, &pipeline->arena);
    decl_list_init(&pipeline->program.decls, &pipeline->arena);
    pipeline->semantics = NULL;
    strbuf_init(&pipeline->generated_c);
}

static void driver_pipeline_dispose(DriverPipeline *pipeline) {
    semantic_result_dispose(pipeline->semantics);
    strbuf_free(&pipeline->generated_c);
    arena_free(&pipeline->arena);
}

static bool driver_run_pipeline(const char *source, DriverPipeline *pipeline, CompileError *error) {
    pipeline->semantics = semantic_result_create(&pipeline->arena, error);
    if (pipeline->semantics == NULL) {
        return false;
    }
    if (!tokenize_source(source, &pipeline->arena, &pipeline->tokens, error)) {
        return false;
    }
    if (!parse_program(&pipeline->tokens, &pipeline->arena, &pipeline->program, error)) {
        return false;
    }
    if (!semantic_analyze(&pipeline->program, pipeline->semantics, error)) {
        return false;
    }
    return c_backend_generate(&pipeline->program, pipeline->semantics, &pipeline->arena, &pipeline->generated_c, error);
}

static char *driver_generated_c_path(const char *output_name, bool emit_c, CompileError *error) {
    return join_suffix(output_name, emit_c ? ".c" : ".tmp.c", error);
}

static void driver_cleanup_generated_c(const char *generated_c_path, bool emit_c, bool wrote_generated_c) {
    if (!emit_c && wrote_generated_c && generated_c_path != NULL) {
        (void)remove(generated_c_path);
    }
}

static bool driver_wait_for_process(pid_t pid, CompileError *error) {
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return error_set(error, "Driver", "Failed while waiting for host C compiler: %s", strerror(errno));
        }
    }

    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0) {
            return true;
        }
        return error_set(error, "Driver", "Host C compiler `%s` exited with status %d", HOST_C_COMPILER, WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return error_set(error, "Driver", "Host C compiler `%s` terminated by signal %d", HOST_C_COMPILER, WTERMSIG(status));
    }
    return error_set(error, "Driver", "Host C compiler `%s` ended unexpectedly", HOST_C_COMPILER);
}

static bool driver_run_host_compiler(const char *generated_c_path, const char *output_name, CompileError *error) {
    char *argv[] = {
        (char *)HOST_C_COMPILER,
        "-std=c11",
        (char *)generated_c_path,
        "-o",
        (char *)output_name,
        NULL
    };
    pid_t pid;
    int spawn_error = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);

    if (spawn_error != 0) {
        return error_set(error, "Driver", "Failed to launch host C compiler `%s`: %s", HOST_C_COMPILER, strerror(spawn_error));
    }
    return driver_wait_for_process(pid, error);
}

bool driver_compile_file(const char *input_file, const char *output_name, bool emit_c, CompileError *error) {
    DriverPipeline pipeline;
    char *source = NULL;
    char *build_c_filename = NULL;
    bool wrote_generated_c = false;
    bool ok = false;

    driver_pipeline_init(&pipeline);

    if (!read_entire_file(input_file, &source, error)) {
        goto cleanup;
    }
    if (!driver_run_pipeline(source, &pipeline, error)) {
        goto cleanup;
    }

    build_c_filename = driver_generated_c_path(output_name, emit_c, error);
    if (build_c_filename == NULL) {
        goto cleanup;
    }
    if (!write_entire_file(build_c_filename, strbuf_cstr(&pipeline.generated_c), error)) {
        goto cleanup;
    }
    wrote_generated_c = true;
    if (!driver_run_host_compiler(build_c_filename, output_name, error)) {
        goto cleanup;
    }

    ok = true;

cleanup:
    driver_cleanup_generated_c(build_c_filename, emit_c, wrote_generated_c);
    free(source);
    free(build_c_filename);
    driver_pipeline_dispose(&pipeline);
    return ok;
}
