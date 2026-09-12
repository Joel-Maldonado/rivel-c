#ifndef RV_PARSE_PARSER_H
#define RV_PARSE_PARSER_H

#include "base/arena.h"
#include "base/diag.h"

#include "ast/ast.h"
#include "lex/token.h"

/*
 * Parses a token stream into a Module. Syntax errors are reported to diags;
 * the parser recovers at statement and declaration boundaries and keeps
 * going, so the returned tree may be partial when diags has errors.
 */
Module *parse_module(const Source *src, const TokenVec *tokens, Arena *arena, Diags *diags);

#endif
