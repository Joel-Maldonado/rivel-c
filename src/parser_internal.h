#ifndef RIVEL_PARSER_INTERNAL_H
#define RIVEL_PARSER_INTERNAL_H

#include "parser.h"

typedef struct {
    const TokenList *tokens;
    size_t index;
    Arena *arena;
    CompileError *error;
} Parser;

const Token *parser_peek(const Parser *parser, size_t offset);
const Token *parser_previous(const Parser *parser, size_t back);
bool parser_is(const Parser *parser, TokenType type, size_t offset);
const Token *parser_advance(Parser *parser);
bool parser_expect(Parser *parser, TokenType type, const char *message, const Token **out_token);
bool parser_match(Parser *parser, TokenType type);
void parser_skip_separators(Parser *parser);

Expr *parser_new_expr(Parser *parser, ExprKind kind);
Stmt *parser_new_stmt(Parser *parser, StmtKind kind);
Decl *parser_new_decl(Parser *parser, DeclKind kind);
Block *parser_new_block(Parser *parser);

bool parser_require_stmt_end(Parser *parser, const char *after_what);
bool parser_consume_decl_end(Parser *parser);
bool parser_parse_type(Parser *parser, Type *out_type);

bool parser_parse_global_const(Parser *parser, Decl **out_decl);
bool parser_parse_function(Parser *parser, Decl **out_decl);
bool parser_parse_struct(Parser *parser, Decl **out_decl);
bool parser_parse_block(Parser *parser, Block **out_block);
bool parser_parse_statement(Parser *parser, Stmt **out_stmt);
bool parser_parse_expression(Parser *parser, Expr **out_expr);

#endif
