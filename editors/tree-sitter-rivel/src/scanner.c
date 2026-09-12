#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stddef.h>

/* A nested comment has no state between tokens, so edits need no serialized state. */
void *tree_sitter_rivel_external_scanner_create(void) { return NULL; }
void tree_sitter_rivel_external_scanner_destroy(void *payload) { (void)payload; }
unsigned tree_sitter_rivel_external_scanner_serialize(void *payload, char *buffer) {
    (void)payload;
    (void)buffer;
    return 0;
}
void tree_sitter_rivel_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    (void)payload;
    (void)buffer;
    (void)length;
}
bool tree_sitter_rivel_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    (void)payload;
    if (!valid_symbols[0]) return false;
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r' || lexer->lookahead == '\n')
        lexer->advance(lexer, true);
    if (lexer->lookahead != '/') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead != '*') return false;
    lexer->advance(lexer, false);
    unsigned depth = 1;
    while (depth > 0) {
        if (lexer->eof(lexer)) return false;
        int32_t previous = lexer->lookahead;
        lexer->advance(lexer, false);
        if (previous == '/' && lexer->lookahead == '*') {
            depth++;
            lexer->advance(lexer, false);
        } else if (previous == '*' && lexer->lookahead == '/') {
            depth--;
            lexer->advance(lexer, false);
        }
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = 0;
    return true;
}
