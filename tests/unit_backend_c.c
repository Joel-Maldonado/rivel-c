#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "backend_c.h"
#include "error.h"
#include "parser.h"
#include "semantic.h"
#include "strbuf.h"
#include "tokenizer.h"

static void test_c_backend_uses_semantic_queries(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;
    Program program;
    SemanticResult *result;
    StrBuf output;
    const char *generated_c;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);
    decl_list_init(&program.decls, &arena);
    strbuf_init(&output);

    assert(tokenize_source(
        "const FLAG: Bool = true\n"
        "fn main() -> Int { print(FLAG); return 0 }\n",
        &arena,
        &tokens,
        &error));
    assert(parse_program(&tokens, &arena, &program, &error));

    result = semantic_result_create(&arena, &error);
    assert(result != NULL);
    assert(semantic_analyze(&program, result, &error));
    assert(c_backend_generate(&program, result, &arena, &output, &error));

    generated_c = strbuf_cstr(&output);
    assert(strstr(generated_c, "rivel_print_bool") != NULL);
    assert(strstr(generated_c, "static const bool rivel_global_FLAG = true;") != NULL);

    semantic_result_dispose(result);
    strbuf_free(&output);
    error_free(&error);
    arena_free(&arena);
}

static void test_c_backend_emits_runtime_helpers_and_unique_shadowed_locals(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;
    Program program;
    SemanticResult *result;
    StrBuf output;
    const char *generated_c;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);
    decl_list_init(&program.decls, &arena);
    strbuf_init(&output);

    assert(tokenize_source(
        "fn main() -> Int {\n"
        "    const value = 8\n"
        "    if true {\n"
        "        const value = 3\n"
        "        print(value % 2)\n"
        "    }\n"
        "    return value / 2\n"
        "}\n",
        &arena,
        &tokens,
        &error));
    assert(parse_program(&tokens, &arena, &program, &error));

    result = semantic_result_create(&arena, &error);
    assert(result != NULL);
    assert(semantic_analyze(&program, result, &error));
    assert(c_backend_generate(&program, result, &arena, &output, &error));

    generated_c = strbuf_cstr(&output);
    assert(strstr(generated_c, "static int64_t rivel_div") != NULL);
    assert(strstr(generated_c, "static int64_t rivel_mod") != NULL);
    assert(strstr(generated_c, "rivel_local_value_0") != NULL);
    assert(strstr(generated_c, "rivel_local_value_1") != NULL);

    semantic_result_dispose(result);
    strbuf_free(&output);
    error_free(&error);
    arena_free(&arena);
}

static void test_c_backend_captures_for_range_bounds_once(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;
    Program program;
    SemanticResult *result;
    StrBuf output;
    const char *generated_c;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);
    decl_list_init(&program.decls, &arena);
    strbuf_init(&output);

    assert(tokenize_source(
        "fn start() -> Int { return 2 }\n"
        "fn stop() -> Int { return 4 }\n"
        "fn main() -> Int {\n"
        "    for i in start()..=stop() {\n"
        "        print(i)\n"
        "    }\n"
        "    return 0\n"
        "}\n",
        &arena,
        &tokens,
        &error));
    assert(parse_program(&tokens, &arena, &program, &error));

    result = semantic_result_create(&arena, &error);
    assert(result != NULL);
    assert(semantic_analyze(&program, result, &error));
    assert(c_backend_generate(&program, result, &arena, &output, &error));

    generated_c = strbuf_cstr(&output);
    assert(strstr(generated_c, "int64_t rivel_range_start_0 = rivel_fn_start();") != NULL);
    assert(strstr(generated_c, "int64_t rivel_range_end_1 = rivel_fn_stop();") != NULL);
    assert(strstr(generated_c, "while (true) {") != NULL);
    assert(strstr(generated_c, "if (rivel_local_i_2 == rivel_range_end_1)") != NULL);

    semantic_result_dispose(result);
    strbuf_free(&output);
    error_free(&error);
    arena_free(&arena);
}

int main(void) {
    test_c_backend_uses_semantic_queries();
    test_c_backend_emits_runtime_helpers_and_unique_shadowed_locals();
    test_c_backend_captures_for_range_bounds_once();
    return 0;
}
