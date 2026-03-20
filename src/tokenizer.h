#ifndef RIVEL_TOKENIZER_H
#define RIVEL_TOKENIZER_H

#include "arena.h"
#include "error.h"
#include "token.h"
#include "vec.h"

bool tokenize_source(const char *source, Arena *arena, Vec *out_tokens, CompileError *error);

#endif
