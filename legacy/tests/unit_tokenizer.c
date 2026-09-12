#include <assert.h>
#include <string.h>

#include "arena.h"
#include "error.h"
#include "token.h"
#include "tokenizer.h"

static void test_tokenize_for_range_header(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(tokenize_source("for i in 1..=3 { }", &arena, &tokens, &error));
    assert(token_list_len(&tokens) == 9U);
    assert(token_list_get_const(&tokens, 0U)->type == TOKEN_KW_FOR);
    assert(token_list_get_const(&tokens, 1U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 2U)->type == TOKEN_KW_IN);
    assert(token_list_get_const(&tokens, 3U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 4U)->type == TOKEN_DOT_DOT_EQ);
    assert(token_list_get_const(&tokens, 5U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 6U)->type == TOKEN_OPEN_BRACE);
    assert(token_list_get_const(&tokens, 7U)->type == TOKEN_CLOSE_BRACE);
    assert(token_list_get_const(&tokens, 8U)->type == TOKEN_EOF);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_line_comment_preserves_newline_separator(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(tokenize_source("const x = 1 // comment\nconst y = 2", &arena, &tokens, &error));
    assert(token_list_len(&tokens) == 10U);
    assert(token_list_get_const(&tokens, 0U)->type == TOKEN_KW_CONST);
    assert(token_list_get_const(&tokens, 1U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 2U)->type == TOKEN_ASSIGN);
    assert(token_list_get_const(&tokens, 3U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 4U)->type == TOKEN_END_STMT);
    assert(token_list_get_const(&tokens, 5U)->type == TOKEN_KW_CONST);
    assert(token_list_get_const(&tokens, 6U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 7U)->type == TOKEN_ASSIGN);
    assert(token_list_get_const(&tokens, 8U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 9U)->type == TOKEN_EOF);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_block_comment_skips_embedded_newlines(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(tokenize_source("const x = 1 /* comment\nstill comment */ + 2", &arena, &tokens, &error));
    assert(token_list_len(&tokens) == 7U);
    assert(token_list_get_const(&tokens, 0U)->type == TOKEN_KW_CONST);
    assert(token_list_get_const(&tokens, 1U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 2U)->type == TOKEN_ASSIGN);
    assert(token_list_get_const(&tokens, 3U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 4U)->type == TOKEN_PLUS);
    assert(token_list_get_const(&tokens, 5U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 6U)->type == TOKEN_EOF);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_slash_when_not_a_comment(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(tokenize_source("const x = 8 / 2", &arena, &tokens, &error));
    assert(token_list_len(&tokens) == 7U);
    assert(token_list_get_const(&tokens, 0U)->type == TOKEN_KW_CONST);
    assert(token_list_get_const(&tokens, 1U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 2U)->type == TOKEN_ASSIGN);
    assert(token_list_get_const(&tokens, 3U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 4U)->type == TOKEN_SLASH);
    assert(token_list_get_const(&tokens, 5U)->type == TOKEN_INT_LITERAL);
    assert(token_list_get_const(&tokens, 6U)->type == TOKEN_EOF);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_unterminated_block_comment_reports_opening_location(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(!tokenize_source("const x = 1 /* unterminated", &arena, &tokens, &error));
    assert(error.phase != NULL);
    assert(strcmp(error.phase, "Lexer") == 0);
    assert(error.has_location);
    assert(error.line == 1);
    assert(error.column == 13);
    assert(error.message != NULL);
    assert(strcmp(error.message, "Unterminated block comment") == 0);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_struct_literal_and_field_access(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(tokenize_source("struct Person { name: String }\nperson.name", &arena, &tokens, &error));
    assert(token_list_get_const(&tokens, 0U)->type == TOKEN_KW_STRUCT);
    assert(token_list_get_const(&tokens, 1U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 2U)->type == TOKEN_OPEN_BRACE);
    assert(token_list_get_const(&tokens, 3U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 4U)->type == TOKEN_COLON);
    assert(token_list_get_const(&tokens, 5U)->type == TOKEN_KW_TYPE_STRING);
    assert(token_list_get_const(&tokens, 6U)->type == TOKEN_CLOSE_BRACE);
    assert(token_list_get_const(&tokens, 7U)->type == TOKEN_END_STMT);
    assert(token_list_get_const(&tokens, 8U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 9U)->type == TOKEN_DOT);
    assert(token_list_get_const(&tokens, 10U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 11U)->type == TOKEN_EOF);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_string_literal_with_interpolation(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(tokenize_source("const x = \"Hello, ${name}!\"", &arena, &tokens, &error));
    assert(token_list_len(&tokens) == 5U);
    assert(token_list_get_const(&tokens, 0U)->type == TOKEN_KW_CONST);
    assert(token_list_get_const(&tokens, 1U)->type == TOKEN_IDENTIFIER);
    assert(token_list_get_const(&tokens, 2U)->type == TOKEN_ASSIGN);
    assert(token_list_get_const(&tokens, 3U)->type == TOKEN_STRING_LITERAL);
    assert(token_list_get_const(&tokens, 4U)->type == TOKEN_EOF);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_rejects_unterminated_interpolation_in_string(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(!tokenize_source("const x = \"Hello, ${name\"", &arena, &tokens, &error));
    assert(error.message != NULL);
    assert(strcmp(error.message, "Unterminated string literal") == 0);

    error_free(&error);
    arena_free(&arena);
}

static void test_tokenize_rejects_stray_closing_brace_in_string(void) {
    Arena arena;
    CompileError error;
    TokenList tokens;

    arena_init(&arena, 4096U);
    error_init(&error);
    token_list_init(&tokens, &arena);

    assert(!tokenize_source("const x = \"Hello }\"", &arena, &tokens, &error));
    assert(error.message != NULL);
    assert(strcmp(error.message, "Unexpected `}` in string literal") == 0);

    error_free(&error);
    arena_free(&arena);
}

int main(void) {
    test_tokenize_for_range_header();
    test_tokenize_line_comment_preserves_newline_separator();
    test_tokenize_block_comment_skips_embedded_newlines();
    test_tokenize_slash_when_not_a_comment();
    test_tokenize_unterminated_block_comment_reports_opening_location();
    test_tokenize_struct_literal_and_field_access();
    test_tokenize_string_literal_with_interpolation();
    test_tokenize_rejects_unterminated_interpolation_in_string();
    test_tokenize_rejects_stray_closing_brace_in_string();
    return 0;
}
