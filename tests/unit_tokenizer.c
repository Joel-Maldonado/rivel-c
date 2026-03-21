#include <assert.h>

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

int main(void) {
    test_tokenize_for_range_header();
    return 0;
}
