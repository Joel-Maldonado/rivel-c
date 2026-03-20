#ifndef RIVEL_TOKENIZER_H
#define RIVEL_TOKENIZER_H

#include "arena.h"
#include "error.h"
#include "token.h"

bool tokenize_source(const char *source, Arena *arena, TokenList *out_tokens, CompileError *error);

#endif
