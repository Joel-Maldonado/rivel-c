#include <assert.h>
#include <stddef.h>

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "parser.h"
#include "tokenizer.h"

static Program parse_source(const char *source, Arena *arena) {
    CompileError error;
    TokenList tokens;
    Program program;

    error_init(&error);
    token_list_init(&tokens, arena);
    decl_list_init(&program.decls, arena);

    assert(tokenize_source(source, arena, &tokens, &error));
    assert(parse_program(&tokens, arena, &program, &error));

    error_free(&error);
    return program;
}

static void test_parse_nested_call_arguments(void) {
    Arena arena;
    Program program;
    Decl *main_decl;
    Stmt *return_stmt;
    Expr *call_expr;

    arena_init(&arena, 4096U);
    program = parse_source(
        "fn twice(x: Int) -> Int { return x + x }\n"
        "fn main() -> Int { return twice((1 + 2) * 3) }\n",
        &arena);

    assert(decl_list_len(&program.decls) == 2U);
    main_decl = decl_list_get(&program.decls, 1U);
    assert(main_decl->kind == DECL_FUNCTION);
    assert(stmt_list_len(&main_decl->as.function.body->statements) == 1U);

    return_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    assert(return_stmt->kind == STMT_RETURN);

    call_expr = return_stmt->as.ret.value;
    assert(call_expr->kind == EXPR_CALL);
    assert(expr_list_len(&call_expr->as.call.args) == 1U);

    arena_free(&arena);
}

static void test_parse_statement_separators_across_blank_lines(void) {
    Arena arena;
    Program program;
    Decl *main_decl;

    arena_init(&arena, 4096U);
    program = parse_source(
        "fn main() -> Int {\n"
        "    print(1)\n"
        "\n"
        "    return 0\n"
        "}\n",
        &arena);

    assert(decl_list_len(&program.decls) == 1U);
    main_decl = decl_list_get(&program.decls, 0U);
    assert(main_decl->kind == DECL_FUNCTION);
    assert(stmt_list_len(&main_decl->as.function.body->statements) == 2U);
    assert(stmt_list_get(&main_decl->as.function.body->statements, 0U)->kind == STMT_CALL);
    assert(stmt_list_get(&main_decl->as.function.body->statements, 1U)->kind == STMT_RETURN);

    arena_free(&arena);
}

int main(void) {
    test_parse_nested_call_arguments();
    test_parse_statement_separators_across_blank_lines();
    return 0;
}
