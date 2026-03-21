#include <assert.h>
#include <stddef.h>
#include <string.h>

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

static void expect_parse_failure(const char *source, const char *expected_message) {
    Arena arena;
    CompileError error;
    TokenList tokens;
    Program program;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);
    decl_list_init(&program.decls, &arena);

    assert(tokenize_source(source, &arena, &tokens, &error));
    assert(!parse_program(&tokens, &arena, &program, &error));
    assert(error.message != NULL);
    assert(strstr(error.message, expected_message) != NULL);

    error_free(&error);
    arena_free(&arena);
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

static void test_parse_string_type_and_literal(void) {
    Arena arena;
    Program program;
    Decl *echo_decl;
    Decl *main_decl;
    Stmt *binding_stmt;
    Expr *initializer;

    arena_init(&arena, 4096U);
    program = parse_source(
        "fn echo(value: String) -> String {\n"
        "    return value\n"
        "}\n"
        "fn main() -> Int {\n"
        "    const message: String = \"hello\"\n"
        "    return 0\n"
        "}\n",
        &arena);

    assert(decl_list_len(&program.decls) == 2U);
    echo_decl = decl_list_get(&program.decls, 0U);
    main_decl = decl_list_get(&program.decls, 1U);

    assert(param_list_get_const(&echo_decl->as.function.params, 0U)->type.kind == TYPE_STRING);
    assert(echo_decl->as.function.return_type.kind == TYPE_STRING);

    binding_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    assert(binding_stmt->kind == STMT_BINDING);
    assert(binding_stmt->as.binding.annotation.kind == TYPE_STRING);

    initializer = binding_stmt->as.binding.initializer;
    assert(initializer->kind == EXPR_STRING);
    assert(initializer->as.string_value.len == 5U);
    assert(memcmp(initializer->as.string_value.data, "hello", 5U) == 0);

    arena_free(&arena);
}

static void test_parse_string_literal_escapes(void) {
    Arena arena;
    Program program;
    Decl *main_decl;
    Stmt *binding_stmt;
    Expr *initializer;
    const char expected[] = {'a', '\n', 'b', '\t', '"', '\\'};

    arena_init(&arena, 4096U);
    program = parse_source(
        "fn main() -> Int {\n"
        "    const value = \"a\\nb\\t\\\"\\\\\"\n"
        "    return 0\n"
        "}\n",
        &arena);

    main_decl = decl_list_get(&program.decls, 0U);
    binding_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    initializer = binding_stmt->as.binding.initializer;

    assert(initializer->kind == EXPR_STRING);
    assert(initializer->as.string_value.len == sizeof(expected));
    assert(memcmp(initializer->as.string_value.data, expected, sizeof(expected)) == 0);

    arena_free(&arena);
}

static void test_parse_rejects_invalid_string_escape(void) {
    expect_parse_failure(
        "fn main() -> Int {\n"
        "    const value = \"bad\\x\"\n"
        "    return 0\n"
        "}\n",
        "Unsupported escape sequence");
}

int main(void) {
    test_parse_nested_call_arguments();
    test_parse_statement_separators_across_blank_lines();
    test_parse_string_type_and_literal();
    test_parse_string_literal_escapes();
    test_parse_rejects_invalid_string_escape();
    return 0;
}
