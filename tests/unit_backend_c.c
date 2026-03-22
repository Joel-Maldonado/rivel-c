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

static void test_c_backend_emits_double_types_and_print_helper(void) {
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
        "const PI: Double = 3.5\n"
        "fn add_one(x: Double) -> Double { return x + 1 }\n"
        "fn main() -> Int {\n"
        "    print(add_one(PI))\n"
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
    assert(strstr(generated_c, "static void rivel_print_double(double value)") != NULL);
    assert(strstr(generated_c, "static const double rivel_global_PI") != NULL);
    assert(strstr(generated_c, "double rivel_fn_add_one(double rivel_param_x)") != NULL);

    semantic_result_dispose(result);
    strbuf_free(&output);
    error_free(&error);
    arena_free(&arena);
}

static void test_c_backend_emits_string_runtime_and_release_aware_lowering(void) {
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
        "const GREETING: String = \"hello\"\n"
        "fn decorate(value: String) -> String {\n"
        "    return value + \"!\"\n"
        "}\n"
        "fn main() -> Int {\n"
        "    mut message = GREETING\n"
        "    if true {\n"
        "        const message = decorate(message)\n"
        "        print(message)\n"
        "    }\n"
        "    message = decorate(message)\n"
        "    return len(message)\n"
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
    assert(strstr(generated_c, "typedef struct RivelStringStorage RivelStringStorage;") != NULL);
    assert(strstr(generated_c, "rivel_string_concat_take") != NULL);
    assert(strstr(generated_c, "rivel_string_release") != NULL);
    assert(strstr(generated_c, "rivel_string_retain") != NULL);
    assert(strstr(generated_c, "rivel_print_string_take") != NULL);
    assert(strstr(generated_c, "rivel_string_release(rivel_local_message_0);") != NULL);

    semantic_result_dispose(result);
    strbuf_free(&output);
    error_free(&error);
    arena_free(&arena);
}

static void test_c_backend_emits_struct_lowering_and_string_field_cleanup(void) {
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
        "struct Person {\n"
        "    name: String\n"
        "    age: Int\n"
        "}\n"
        "fn main() -> Int {\n"
        "    mut person = Person { name: \"John\", age: 23 }\n"
        "    person.name = \"Jane\"\n"
        "    return person.age\n"
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
    assert(strstr(generated_c, "typedef struct RivelStruct_Person") != NULL);
    assert(strstr(generated_c, "RivelStruct_Person rivel_local_person_0") != NULL);
    assert(strstr(generated_c, "rivel_local_person_0.name") != NULL);
    assert(strstr(generated_c, "rivel_string_release(rivel_local_person_0.name);") != NULL);

    semantic_result_dispose(result);
    strbuf_free(&output);
    error_free(&error);
    arena_free(&arena);
}

static void test_c_backend_emits_inline_print_and_stringify_helpers(void) {
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
        "    const name = \"Nova\"\n"
        "    print(\"Hello, ${name}: \")\n"
        "    println(3 + 4)\n"
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
    assert(strstr(generated_c, "static void rivel_print_int_inline(int64_t value)") != NULL);
    assert(strstr(generated_c, "static void rivel_println_int(int64_t value)") != NULL);
    assert(strstr(generated_c, "rivel_string_from_int") != NULL);
    assert(strstr(generated_c, "rivel_string_concat_take") != NULL);

    semantic_result_dispose(result);
    strbuf_free(&output);
    error_free(&error);
    arena_free(&arena);
}

int main(void) {
    test_c_backend_uses_semantic_queries();
    test_c_backend_emits_runtime_helpers_and_unique_shadowed_locals();
    test_c_backend_captures_for_range_bounds_once();
    test_c_backend_emits_double_types_and_print_helper();
    test_c_backend_emits_string_runtime_and_release_aware_lowering();
    test_c_backend_emits_struct_lowering_and_string_field_cleanup();
    test_c_backend_emits_inline_print_and_stringify_helpers();
    return 0;
}
