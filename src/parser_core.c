#include "parser_internal.h"

const Token *parser_peek(const Parser *parser, size_t offset) {
    size_t count = token_list_len(parser->tokens);
    size_t index = parser->index + offset;

    if (index >= count) {
        return token_list_get_const(parser->tokens, count - 1U);
    }
    return token_list_get_const(parser->tokens, index);
}

const Token *parser_previous(const Parser *parser, size_t back) {
    return token_list_get_const(parser->tokens, parser->index - back - 1U);
}

bool parser_is(const Parser *parser, TokenType type, size_t offset) {
    return parser_peek(parser, offset)->type == type;
}

const Token *parser_advance(Parser *parser) {
    const Token *token = parser_peek(parser, 0U);

    parser->index += 1U;
    return token;
}

bool parser_expect(Parser *parser, TokenType type, const char *message, const Token **out_token) {
    const Token *token;

    if (!parser_is(parser, type, 0U)) {
        return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "%s", message);
    }

    token = parser_advance(parser);
    if (out_token != NULL) {
        *out_token = token;
    }
    return true;
}

bool parser_match(Parser *parser, TokenType type) {
    if (!parser_is(parser, type, 0U)) {
        return false;
    }
    parser_advance(parser);
    return true;
}

void parser_skip_separators(Parser *parser) {
    while (parser_match(parser, TOKEN_END_STMT)) {
    }
}

Expr *parser_new_expr(Parser *parser, ExprKind kind) {
    Expr *expr = (Expr *)arena_alloc_zero(parser->arena, sizeof(*expr), _Alignof(Expr), parser->error);

    if (expr == NULL) {
        return NULL;
    }
    expr->kind = kind;
    return expr;
}

Stmt *parser_new_stmt(Parser *parser, StmtKind kind) {
    Stmt *stmt = (Stmt *)arena_alloc_zero(parser->arena, sizeof(*stmt), _Alignof(Stmt), parser->error);

    if (stmt == NULL) {
        return NULL;
    }
    stmt->kind = kind;
    return stmt;
}

Decl *parser_new_decl(Parser *parser, DeclKind kind) {
    Decl *decl = (Decl *)arena_alloc_zero(parser->arena, sizeof(*decl), _Alignof(Decl), parser->error);

    if (decl == NULL) {
        return NULL;
    }
    decl->kind = kind;
    return decl;
}

Block *parser_new_block(Parser *parser) {
    Block *block = (Block *)arena_alloc_zero(parser->arena, sizeof(*block), _Alignof(Block), parser->error);

    if (block == NULL) {
        return NULL;
    }
    stmt_list_init(&block->statements, parser->arena);
    return block;
}

bool parser_require_stmt_end(Parser *parser, const char *after_what) {
    if (parser_match(parser, TOKEN_END_STMT)) {
        parser_skip_separators(parser);
        return true;
    }
    if (parser_is(parser, TOKEN_CLOSE_BRACE, 0U) || parser_is(parser, TOKEN_EOF, 0U)) {
        return true;
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected a statement separator after %s", after_what);
}

bool parser_consume_decl_end(Parser *parser) {
    if (parser_match(parser, TOKEN_END_STMT)) {
        parser_skip_separators(parser);
        return true;
    }
    if (parser_is(parser, TOKEN_EOF, 0U)) {
        return true;
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected a declaration separator after top-level declaration");
}

bool parser_parse_type(Parser *parser, Type *out_type) {
    if (parser_match(parser, TOKEN_KW_TYPE_INT)) {
        *out_type = type_make_int();
        return true;
    }
    if (parser_match(parser, TOKEN_KW_TYPE_DOUBLE)) {
        *out_type = type_make_double();
        return true;
    }
    if (parser_match(parser, TOKEN_KW_TYPE_BOOL)) {
        *out_type = type_make_bool();
        return true;
    }
    if (parser_match(parser, TOKEN_KW_TYPE_STRING)) {
        *out_type = type_make_string();
        return true;
    }
    if (parser_is(parser, TOKEN_IDENTIFIER, 0U)) {
        const Token *token = parser_advance(parser);
        const char *type_name = arena_copy_slice(parser->arena, token->lexeme, parser->error);

        if (type_name == NULL) {
            return false;
        }
        *out_type = type_make_struct(token->lexeme, type_name);
        return true;
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected a type name");
}
