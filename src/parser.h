#ifndef RIVEL_PARSER_H
#define RIVEL_PARSER_H

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "token.h"

bool parse_program(const TokenList *tokens, Arena *arena, Program *out_program, CompileError *error);

#endif
