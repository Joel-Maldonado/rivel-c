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

static void test_parse_for_range_statement(void) {
    Arena arena;
    Program program;
    Decl *main_decl;
    Stmt *for_stmt;

    arena_init(&arena, 4096U);
    program = parse_source(
        "fn main() -> Int {\n"
        "    for i in 1..=3 {\n"
        "        print(i)\n"
        "    }\n"
        "    return 0\n"
        "}\n",
        &arena);

    assert(decl_list_len(&program.decls) == 1U);
    main_decl = decl_list_get(&program.decls, 0U);
    assert(main_decl->kind == DECL_FUNCTION);
    assert(stmt_list_len(&main_decl->as.function.body->statements) == 2U);

    for_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    assert(for_stmt->kind == STMT_FOR_RANGE);
    assert(slice_equal_cstr(for_stmt->as.for_range.name, "i"));
    assert(for_stmt->as.for_range.is_inclusive);
    assert(for_stmt->as.for_range.start->kind == EXPR_INT);
    assert(for_stmt->as.for_range.start->as.int_value == 1);
    assert(for_stmt->as.for_range.end->kind == EXPR_INT);
    assert(for_stmt->as.for_range.end->as.int_value == 3);
    assert(stmt_list_len(&for_stmt->as.for_range.body->statements) == 1U);
    assert(stmt_list_get(&for_stmt->as.for_range.body->statements, 0U)->kind == STMT_CALL);

    arena_free(&arena);
}

static void test_parse_double_types_and_literals(void) {
    Arena arena;
    Program program;
    Decl *global_decl;
    Decl *square_decl;
    Decl *main_decl;
    Stmt *binding_stmt;

    arena_init(&arena, 4096U);
    program = parse_source(
        "const PI: Double = 3.5\n"
        "fn square(x: Double) -> Double { return x * x }\n"
        "fn main() -> Int {\n"
        "    const value: Double = square(2.0)\n"
        "    print(value)\n"
        "    return 0\n"
        "}\n",
        &arena);

    assert(decl_list_len(&program.decls) == 3U);

    global_decl = decl_list_get(&program.decls, 0U);
    assert(global_decl->kind == DECL_GLOBAL_CONST);
    assert(global_decl->as.global_const.has_annotation);
    assert(strcmp(type_display_name(global_decl->as.global_const.annotation), "Double") == 0);

    square_decl = decl_list_get(&program.decls, 1U);
    assert(square_decl->kind == DECL_FUNCTION);
    assert(param_list_len(&square_decl->as.function.params) == 1U);
    assert(strcmp(type_display_name(param_list_get_const(&square_decl->as.function.params, 0U)->type), "Double") == 0);
    assert(strcmp(type_display_name(square_decl->as.function.return_type), "Double") == 0);

    main_decl = decl_list_get(&program.decls, 2U);
    assert(main_decl->kind == DECL_FUNCTION);
    assert(stmt_list_len(&main_decl->as.function.body->statements) == 3U);
    binding_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    assert(binding_stmt->kind == STMT_BINDING);
    assert(binding_stmt->as.binding.has_annotation);
    assert(strcmp(type_display_name(binding_stmt->as.binding.annotation), "Double") == 0);

    arena_free(&arena);
}

int main(void) {
    test_parse_nested_call_arguments();
    test_parse_statement_separators_across_blank_lines();
    test_parse_for_range_statement();
    test_parse_double_types_and_literals();
    return 0;
}
