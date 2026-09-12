#ifndef RIVEL_BACKEND_C_H
#define RIVEL_BACKEND_C_H

#include "arena.h"
#include "error.h"
#include "semantic.h"
#include "strbuf.h"

bool c_backend_generate(const Program *program, const SemanticResult *semantics, Arena *arena, StrBuf *output, CompileError *error);

#endif
