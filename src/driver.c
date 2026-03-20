#include "driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "backend_c.h"
#include "parser.h"
#include "semantic.h"
#include "strbuf.h"
#include "tokenizer.h"

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

static char *dup_cstr(const char *text, CompileError *error) {
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1U);
    if (copy == NULL) {
        error_set_oom(error, NULL);
        return NULL;
    }
    memcpy(copy, text, len + 1U);
    return copy;
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

static char *shell_quote(const char *text, CompileError *error) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_char(&buf, '\'', error)) {
        strbuf_free(&buf);
        return NULL;
    }
    while (text[index] != '\0') {
        if (text[index] == '\'') {
            if (!strbuf_append_cstr(&buf, "'\\''", error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (!strbuf_append_char(&buf, text[index], error)) {
            strbuf_free(&buf);
            return NULL;
        }
        index += 1U;
    }
    if (!strbuf_append_char(&buf, '\'', error)) {
        strbuf_free(&buf);
        return NULL;
    }

    copy = dup_cstr(strbuf_cstr(&buf), error);
    strbuf_free(&buf);
    return copy;
}

bool driver_compile_file(const char *input_file, const char *output_name, bool emit_c, CompileError *error) {
    char *source = NULL;
    char *quoted_c = NULL;
    char *quoted_out = NULL;
    char *build_c_filename = NULL;
    char *command = NULL;
    Vec tokens;
    Program program;
    SemanticContext semantics;
    Arena arena;
    StrBuf generated_c;
    int command_result;
    bool ok = false;

    arena_init(&arena, 4096U);
    strbuf_init(&generated_c);
    semantic_context_init(&semantics, &arena);
    vec_init(&tokens, sizeof(Token), &arena);
    vec_init(&program.decls, sizeof(Decl *), &arena);

    if (!read_entire_file(input_file, &source, error)) {
        goto cleanup;
    }
    if (!tokenize_source(source, &arena, &tokens, error)) {
        goto cleanup;
    }
    if (!parse_program(&tokens, &arena, &program, error)) {
        goto cleanup;
    }
    if (!semantic_analyze(&program, &semantics, error)) {
        goto cleanup;
    }
    if (!c_backend_generate(&program, &semantics, &arena, &generated_c, error)) {
        goto cleanup;
    }

    build_c_filename = join_suffix(output_name, emit_c ? ".c" : ".tmp.c", error);
    if (build_c_filename == NULL) {
        goto cleanup;
    }

    if (!write_entire_file(build_c_filename, strbuf_cstr(&generated_c), error)) {
        goto cleanup;
    }

    quoted_c = shell_quote(build_c_filename, error);
    quoted_out = shell_quote(output_name, error);
    if (quoted_c == NULL || quoted_out == NULL) {
        goto cleanup;
    }

    command = (char *)malloc(strlen(quoted_c) + strlen(quoted_out) + 32U);
    if (command == NULL) {
        error_set_oom(error, NULL);
        goto cleanup;
    }
    (void)snprintf(command, strlen(quoted_c) + strlen(quoted_out) + 32U, "clang -std=c11 %s -o %s", quoted_c, quoted_out);
    command_result = system(command);
    if (!emit_c) {
        (void)remove(build_c_filename);
    }
    if (command_result != 0) {
        error_set(error, NULL, "Command failed: %s", command);
        goto cleanup;
    }

    ok = true;

cleanup:
    free(source);
    free(quoted_c);
    free(quoted_out);
    free(build_c_filename);
    free(command);
    semantic_context_free(&semantics);
    strbuf_free(&generated_c);
    arena_free(&arena);
    return ok;
}
