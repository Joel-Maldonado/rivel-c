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

static void test_parse_interpolated_string_lowers_to_string_addition(void) {
    Arena arena;
    Program program;
    Decl *greet_decl;
    Stmt *return_stmt;
    Expr *expr;

    arena_init(&arena, 4096U);
    program = parse_source(
        "fn greet(name: String) -> String {\n"
        "    return \"Hello, ${name}!\"\n"
        "}\n"
        "fn main() -> Int {\n"
        "    return 0\n"
        "}\n",
        &arena);

    greet_decl = decl_list_get(&program.decls, 0U);
    return_stmt = stmt_list_get(&greet_decl->as.function.body->statements, 0U);
    expr = return_stmt->as.ret.value;

    assert(expr->kind == EXPR_BINARY);
    assert(expr->as.binary.op == TOKEN_PLUS);
    assert(expr->as.binary.lhs->kind == EXPR_BINARY);
    assert(expr->as.binary.lhs->as.binary.lhs->kind == EXPR_STRING);
    assert(expr->as.binary.lhs->as.binary.rhs->kind == EXPR_CALL);
    assert(expr->as.binary.rhs->kind == EXPR_STRING);

    arena_free(&arena);
}

static void test_parse_interpolated_string_allows_complex_expressions(void) {
    Arena arena;
    Program program;
    Decl *main_decl;
    Stmt *binding_stmt;
    Expr *initializer;

    arena_init(&arena, 4096U);
    program = parse_source(
        "struct Person {\n"
        "    name: String\n"
        "}\n"
        "fn decorate(value: String) -> String {\n"
        "    return value + \"!\"\n"
        "}\n"
        "fn main() -> Int {\n"
        "    const person = Person { name: \"Nova\" }\n"
        "    const count = 1\n"
        "    const value = \"${decorate(person.name)} ${count + 1}\"\n"
        "    return 0\n"
        "}\n",
        &arena);

    main_decl = decl_list_get(&program.decls, 2U);
    binding_stmt = stmt_list_get(&main_decl->as.function.body->statements, 2U);
    initializer = binding_stmt->as.binding.initializer;

    assert(initializer->kind == EXPR_BINARY);
    assert(initializer->as.binary.op == TOKEN_PLUS);

    arena_free(&arena);
}

static void test_parse_rejects_empty_string_interpolation(void) {
    expect_parse_failure(
        "fn main() -> Int {\n"
        "    const value = \"${}\"\n"
        "    return 0\n"
        "}\n",
        "Expected expression");
}

static void test_parse_struct_decl_literal_access_and_assignment(void) {
    Arena arena;
    Program program;
    Decl *struct_decl;
    const StructFieldDecl *name_field;
    Decl *main_decl;
    Stmt *binding_stmt;
    Expr *literal_expr;
    const StructLiteralField *literal_name_field;
    Stmt *assign_stmt;
    Expr *assign_target;
    Stmt *return_stmt;
    Expr *return_expr;

    arena_init(&arena, 4096U);
    program = parse_source(
        "struct Person {\n"
        "    name: String\n"
        "    age: Int\n"
        "}\n"
        "fn main() -> Int {\n"
        "    mut person: Person = Person { name: \"John\", age: 23 }\n"
        "    person.age = 24\n"
        "    return person.age\n"
        "}\n",
        &arena);

    assert(decl_list_len(&program.decls) == 2U);

    struct_decl = decl_list_get(&program.decls, 0U);
    assert(struct_decl->kind == DECL_STRUCT);
    assert(slice_equal_cstr(struct_decl->name, "Person"));
    assert(struct_field_decl_list_len(&struct_decl->as.struct_decl.fields) == 2U);

    name_field = struct_field_decl_list_get_const(&struct_decl->as.struct_decl.fields, 0U);
    assert(slice_equal_cstr(name_field->name, "name"));
    assert(name_field->type.kind == TYPE_STRING);

    main_decl = decl_list_get(&program.decls, 1U);
    binding_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);
    assert(binding_stmt->kind == STMT_BINDING);
    assert(binding_stmt->as.binding.has_annotation);
    assert(binding_stmt->as.binding.annotation.kind == TYPE_STRUCT);
    assert(slice_equal_cstr(binding_stmt->as.binding.annotation.struct_name, "Person"));

    literal_expr = binding_stmt->as.binding.initializer;
    assert(literal_expr->kind == EXPR_STRUCT_LITERAL);
    assert(slice_equal_cstr(literal_expr->as.struct_literal.struct_name, "Person"));
    assert(struct_literal_field_list_len(&literal_expr->as.struct_literal.fields) == 2U);

    literal_name_field = struct_literal_field_list_get_const(&literal_expr->as.struct_literal.fields, 0U);
    assert(slice_equal_cstr(literal_name_field->name, "name"));
    assert(literal_name_field->value->kind == EXPR_STRING);

    assign_stmt = stmt_list_get(&main_decl->as.function.body->statements, 1U);
    assert(assign_stmt->kind == STMT_ASSIGN);
    assign_target = assign_stmt->as.assign.target;
    assert(assign_target->kind == EXPR_FIELD);
    assert(assign_target->as.field.base->kind == EXPR_NAME);
    assert(slice_equal_cstr(assign_target->as.field.base->as.name, "person"));
    assert(slice_equal_cstr(assign_target->as.field.name, "age"));

    return_stmt = stmt_list_get(&main_decl->as.function.body->statements, 2U);
    return_expr = return_stmt->as.ret.value;
    assert(return_expr->kind == EXPR_FIELD);
    assert(slice_equal_cstr(return_expr->as.field.name, "age"));

    arena_free(&arena);
}

int main(void) {
    test_parse_nested_call_arguments();
    test_parse_statement_separators_across_blank_lines();
    test_parse_for_range_statement();
    test_parse_double_types_and_literals();
    test_parse_string_type_and_literal();
    test_parse_string_literal_escapes();
    test_parse_rejects_invalid_string_escape();
    test_parse_interpolated_string_lowers_to_string_addition();
    test_parse_interpolated_string_allows_complex_expressions();
    test_parse_rejects_empty_string_interpolation();
    test_parse_struct_decl_literal_access_and_assignment();
    return 0;
}
