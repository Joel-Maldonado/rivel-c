#include <assert.h>
#include <stddef.h>

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "parser.h"
#include "semantic.h"
#include "tokenizer.h"

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

int main(void) {
    test_semantic_result_tracks_expr_types();
    test_semantic_result_tracks_const_values_and_shadowing();
    return 0;
}
