#ifndef RV_LEX_LEXER_H
#define RV_LEX_LEXER_H

#include <stdio.h>

#include "base/arena.h"
#include "base/diag.h"
#include "base/source.h"

#include "lex/token.h"

/*
 * Tokenizes the whole source. Errors are reported to diags and the lexer
 * keeps going, so the parser always receives a token stream ending in
 * TOK_EOF. Decoded string bytes are allocated from arena.
 */
void lex_source(const Source *src, Arena *arena, Diags *diags, TokenVec *out);

void tokens_dump(const TokenVec *tokens, const Source *src, FILE *out);

#endif
