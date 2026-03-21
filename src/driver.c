#include "driver_internal.h"

#include <stdlib.h>
void driver_pipeline_init(DriverPipeline *pipeline) {
    arena_init(&pipeline->arena, 4096U);
    token_list_init(&pipeline->tokens, &pipeline->arena);
    decl_list_init(&pipeline->program.decls, &pipeline->arena);
    pipeline->semantics = NULL;
    strbuf_init(&pipeline->generated_c);
}

void driver_pipeline_dispose(DriverPipeline *pipeline) {
    semantic_result_dispose(pipeline->semantics);
    strbuf_free(&pipeline->generated_c);
    arena_free(&pipeline->arena);
}

bool driver_run_pipeline(const char *source, DriverPipeline *pipeline, CompileError *error) {
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
    return driver_join_suffix(output_name, emit_c ? ".c" : ".tmp.c", error);
}

static void driver_cleanup_generated_c(const char *generated_c_path, bool emit_c, bool wrote_generated_c) {
    if (!emit_c && wrote_generated_c && generated_c_path != NULL) {
        (void)remove(generated_c_path);
    }
}

bool driver_compile_file(const char *input_file, const char *output_name, bool emit_c, CompileError *error) {
    DriverPipeline pipeline;
    char *source = NULL;
    char *build_c_filename = NULL;
    bool wrote_generated_c = false;
    bool ok = false;

    driver_pipeline_init(&pipeline);

    if (!driver_read_entire_file(input_file, &source, error)) {
        goto cleanup;
    }
    if (!driver_run_pipeline(source, &pipeline, error)) {
        goto cleanup;
    }

    build_c_filename = driver_generated_c_path(output_name, emit_c, error);
    if (build_c_filename == NULL) {
        goto cleanup;
    }
    if (!driver_write_entire_file(build_c_filename, strbuf_cstr(&pipeline.generated_c), error)) {
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
