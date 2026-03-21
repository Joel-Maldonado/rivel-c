#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "parser.h"
#include "semantic.h"
#include "tokenizer.h"

static SemanticResult *analyze_source(const char *source, Arena *arena, Program *program, CompileError *error) {
    TokenList tokens;
    SemanticResult *result;

    token_list_init(&tokens, arena);
    decl_list_init(&program->decls, arena);

    assert(tokenize_source(source, arena, &tokens, error));
    assert(parse_program(&tokens, arena, program, error));

    result = semantic_result_create(arena, error);
    assert(result != NULL);
    assert(semantic_analyze(program, result, error));
    return result;
}

static void test_semantic_result_tracks_expr_types(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;
    Program program;
    SemanticResult *result;
    Decl *main_decl;
    Stmt *return_stmt;
    Type expr_type;
    ConstValue global_value;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);
    decl_list_init(&program.decls, &arena);

    assert(tokenize_source(
        "const BASE: Int = 40 + 2\n"
        "fn main() -> Int { return BASE }\n",
        &arena,
        &tokens,
        &error));
    assert(parse_program(&tokens, &arena, &program, &error));

    result = semantic_result_create(&arena, &error);
    assert(result != NULL);
    assert(semantic_analyze(&program, result, &error));

    main_decl = decl_list_get(&program.decls, 1U);
    return_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    assert(semantic_expr_type(result, return_stmt->as.ret.value, &expr_type));
    assert(expr_type.kind == TYPE_INT);

    assert(semantic_global_const_value(result, slice_from_cstr("BASE"), &global_value));
    assert(global_value.type.kind == TYPE_INT);
    assert(global_value.int_value == 42);

    semantic_result_dispose(result);
    error_free(&error);
    arena_free(&arena);
}

static void test_semantic_result_tracks_const_values_and_shadowing(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;
    Program program;
    SemanticResult *result;
    Decl *global_decl;
    Decl *main_decl;
    Stmt *return_stmt;
    ConstValue expr_value;
    Type expr_type;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);
    decl_list_init(&program.decls, &arena);

    assert(tokenize_source(
        "const BASE: Int = 40 + 2\n"
        "fn main() -> Int {\n"
        "    const BASE: Int = 7\n"
        "    return BASE\n"
        "}\n",
        &arena,
        &tokens,
        &error));
    assert(parse_program(&tokens, &arena, &program, &error));

    result = semantic_result_create(&arena, &error);
    assert(result != NULL);
    assert(semantic_analyze(&program, result, &error));

    global_decl = decl_list_get(&program.decls, 0U);
    assert(semantic_expr_const_value(result, global_decl->as.global_const.initializer, &expr_value));
    assert(expr_value.type.kind == TYPE_INT);
    assert(expr_value.int_value == 42);

    main_decl = decl_list_get(&program.decls, 1U);
    return_stmt = stmt_list_get(&main_decl->as.function.body->statements, 1U);
    assert(semantic_expr_type(result, return_stmt->as.ret.value, &expr_type));
    assert(expr_type.kind == TYPE_INT);

    semantic_result_dispose(result);
    error_free(&error);
    arena_free(&arena);
}

static void test_semantic_result_tracks_string_values_and_builtin_types(void) {
    Arena arena;
    CompileError error;
    Program program;
    SemanticResult *result;
    Decl *main_decl;
    Stmt *binding_stmt;
    Stmt *return_stmt;
    Type expr_type;
    ConstValue greeting_value;
    ConstValue contains_value;

    arena_init(&arena, 4096U);
    error_init(&error);

    result = analyze_source(
        "const GREETING: String = \"hello\" + \" world\"\n"
        "const HAS_WORLD: Bool = contains(GREETING, \"world\")\n"
        "fn main() -> Int {\n"
        "    const tail = substr(GREETING, 6, 5)\n"
        "    return len(tail)\n"
        "}\n",
        &arena,
        &program,
        &error);

    assert(semantic_global_const_value(result, slice_from_cstr("GREETING"), &greeting_value));
    assert(greeting_value.type.kind == TYPE_STRING);
    assert(greeting_value.string_value.len == 11U);
    assert(memcmp(greeting_value.string_value.data, "hello world", 11U) == 0);

    assert(semantic_global_const_value(result, slice_from_cstr("HAS_WORLD"), &contains_value));
    assert(contains_value.type.kind == TYPE_BOOL);
    assert(contains_value.bool_value);

    main_decl = decl_list_get(&program.decls, 2U);
    binding_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    return_stmt = stmt_list_get(&main_decl->as.function.body->statements, 1U);

    assert(semantic_expr_type(result, binding_stmt->as.binding.initializer, &expr_type));
    assert(expr_type.kind == TYPE_STRING);

    assert(semantic_expr_type(result, return_stmt->as.ret.value, &expr_type));
    assert(expr_type.kind == TYPE_INT);

    semantic_result_dispose(result);
    error_free(&error);
    arena_free(&arena);
}

int main(void) {
    test_semantic_result_tracks_expr_types();
    test_semantic_result_tracks_const_values_and_shadowing();
    test_semantic_result_tracks_string_values_and_builtin_types();
    return 0;
}
