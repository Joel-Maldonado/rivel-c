#include <assert.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "parser.h"
#include "semantic.h"
#include "tokenizer.h"

static Program parse_source(const char *source, Arena *arena, CompileError *error) {
    TokenList tokens;
    Program program;

    token_list_init(&tokens, arena);
    decl_list_init(&program.decls, arena);

    assert(tokenize_source(source, arena, &tokens, error));
    assert(parse_program(&tokens, arena, &program, error));
    return program;
}

static void test_type_constructors_build_expected_values(void) {
    Type int_type = type_make_int();
    Type double_type = type_make_double();
    Type bool_type = type_make_bool();
    Type string_type = type_make_string();
    Type struct_type = type_make_struct(slice_from_cstr("Person"), "Person");

    assert(int_type.kind == TYPE_INT);
    assert(double_type.kind == TYPE_DOUBLE);
    assert(bool_type.kind == TYPE_BOOL);
    assert(string_type.kind == TYPE_STRING);
    assert(struct_type.kind == TYPE_STRUCT);
    assert(slice_equal_cstr(struct_type.struct_name, "Person"));
    assert(strcmp(type_display_name(struct_type), "Person") == 0);
}

static void test_expr_semantic_annotations_round_trip(void) {
    Expr expr = {0};
    Type type = type_make_bool();
    ConstValue value = const_value_make_bool(true);
    Type out_type;
    ConstValue out_value;

    assert(!expr_resolved_type(&expr, &out_type));
    assert(!expr_const_value(&expr, &out_value));

    expr_set_resolved_type(&expr, type);
    expr_set_const_value(&expr, value);

    assert(expr_resolved_type(&expr, &out_type));
    assert(out_type.kind == TYPE_BOOL);
    assert(expr_const_value(&expr, &out_value));
    assert(out_value.type.kind == TYPE_BOOL);
    assert(out_value.bool_value);
}

static void test_semantic_analysis_populates_expr_annotations(void) {
    Arena arena;
    CompileError error;
    Program program;
    SemanticResult *result;
    Decl *global_decl;
    Decl *main_decl;
    Stmt *return_stmt;
    ConstValue const_value;
    Type expr_type;

    arena_init(&arena, 4096U);
    error_init(&error);

    program = parse_source(
        "const BASE: Int = 40 + 2\n"
        "fn main() -> Int {\n"
        "    return BASE\n"
        "}\n",
        &arena,
        &error);

    result = semantic_result_create(&arena, &error);
    assert(result != NULL);
    assert(semantic_analyze(&program, result, &error));

    global_decl = decl_list_get(&program.decls, 0U);
    main_decl = decl_list_get(&program.decls, 1U);
    return_stmt = stmt_list_get(&main_decl->as.function.body->statements, 0U);

    assert(expr_const_value(global_decl->as.global_const.initializer, &const_value));
    assert(const_value.type.kind == TYPE_INT);
    assert(const_value.int_value == 42);

    assert(expr_resolved_type(return_stmt->as.ret.value, &expr_type));
    assert(expr_type.kind == TYPE_INT);
    assert(semantic_expr_type(result, return_stmt->as.ret.value, &expr_type));
    assert(expr_type.kind == TYPE_INT);

    semantic_result_dispose(result);
    error_free(&error);
    arena_free(&arena);
}

int main(void) {
    test_type_constructors_build_expected_values();
    test_expr_semantic_annotations_round_trip();
    test_semantic_analysis_populates_expr_annotations();
    return 0;
}
