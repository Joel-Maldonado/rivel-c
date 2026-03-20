#ifndef RIVEL_PARSER_H
#define RIVEL_PARSER_H

#include "arena.h"
#include "ast.h"
#include "error.h"
#include "vec.h"

bool parse_program(const Vec *tokens, Arena *arena, Program *out_program, CompileError *error);

#endif
