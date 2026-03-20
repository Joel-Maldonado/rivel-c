#include "parser.h"

#include <errno.h>
#include <stdlib.h>

typedef struct {
    const Token *tokens;
    size_t count;
    size_t index;
    Arena *arena;
    CompileError *error;
} Parser;

static const Token *parser_peek(const Parser *parser, size_t offset) {
    size_t index = parser->index + offset;
    if (index >= parser->count) {
        return &parser->tokens[parser->count - 1U];
    }
    return &parser->tokens[index];
}

static bool parser_is(const Parser *parser, TokenType type, size_t offset) {
    return parser_peek(parser, offset)->type == type;
}

static const Token *parser_advance(Parser *parser) {
    const Token *token = parser_peek(parser, 0U);
    parser->index += 1U;
    return token;
}

static bool parser_expect(Parser *parser, TokenType type, const char *message, const Token **out_token) {
    if (!parser_is(parser, type, 0U)) {
        return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "%s", message);
    }
    *out_token = parser_advance(parser);
    return true;
}

static bool parser_match(Parser *parser, TokenType type) {
    if (!parser_is(parser, type, 0U)) {
        return false;
    }
    parser_advance(parser);
    return true;
}

static void parser_skip_separators(Parser *parser) {
    while (parser_match(parser, TOKEN_END_STMT)) {
    }
}

static Expr *parser_new_expr(Parser *parser, ExprKind kind) {
    Expr *expr = (Expr *)arena_alloc_zero(parser->arena, sizeof(*expr), _Alignof(Expr), parser->error);
    if (expr == NULL) {
        return NULL;
    }
    expr->kind = kind;
    return expr;
}

static Stmt *parser_new_stmt(Parser *parser, StmtKind kind) {
    Stmt *stmt = (Stmt *)arena_alloc_zero(parser->arena, sizeof(*stmt), _Alignof(Stmt), parser->error);
    if (stmt == NULL) {
        return NULL;
    }
    stmt->kind = kind;
    return stmt;
}

static Decl *parser_new_decl(Parser *parser, DeclKind kind) {
    Decl *decl = (Decl *)arena_alloc_zero(parser->arena, sizeof(*decl), _Alignof(Decl), parser->error);
    if (decl == NULL) {
        return NULL;
    }
    decl->kind = kind;
    return decl;
}

static Block *parser_new_block(Parser *parser) {
    Block *block = (Block *)arena_alloc_zero(parser->arena, sizeof(*block), _Alignof(Block), parser->error);
    if (block == NULL) {
        return NULL;
    }
    vec_init(&block->statements, sizeof(Stmt *), parser->arena);
    return block;
}

static bool parser_require_stmt_end(Parser *parser, const char *after_what) {
    if (parser_match(parser, TOKEN_END_STMT)) {
        parser_skip_separators(parser);
        return true;
    }
    if (parser_is(parser, TOKEN_CLOSE_BRACE, 0U) || parser_is(parser, TOKEN_EOF, 0U)) {
        return true;
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected a statement separator after %s", after_what);
}

static bool parser_consume_decl_end(Parser *parser) {
    if (parser_match(parser, TOKEN_END_STMT)) {
        parser_skip_separators(parser);
        return true;
    }
    if (parser_is(parser, TOKEN_EOF, 0U)) {
        return true;
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected a declaration separator after top-level declaration");
}

static bool parser_parse_type(Parser *parser, Type *out_type) {
    if (parser_match(parser, TOKEN_KW_TYPE_INT)) {
        out_type->kind = TYPE_INT;
        return true;
    }
    if (parser_match(parser, TOKEN_KW_TYPE_BOOL)) {
        out_type->kind = TYPE_BOOL;
        return true;
    }
    if (parser_is(parser, TOKEN_IDENTIFIER, 0U)) {
        const Token *token = parser_peek(parser, 0U);
        return error_set_at(parser->error, "Parse", token->line, token->column, "Unknown type `%.*s`", (int)token->lexeme.len, token->lexeme.data);
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected a type name");
}

static bool parser_parse_expression(Parser *parser, Expr **out_expr);

static bool parser_parse_param(Parser *parser, Param *out_param) {
    const Token *name_token;

    if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a parameter name", &name_token)) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_COLON, "Expected `:` after parameter name", &name_token)) {
        return false;
    }

    out_param->token = parser->tokens[parser->index - 2U];
    out_param->name = out_param->token.lexeme;
    return parser_parse_type(parser, &out_param->type);
}

static bool parser_parse_block(Parser *parser, Block **out_block);
static bool parser_parse_statement(Parser *parser, Stmt **out_stmt);

static bool parser_parse_global_const(Parser *parser, Decl **out_decl) {
    const Token *token;
    const Token *name_token;
    Decl *decl;

    if (!parser_expect(parser, TOKEN_KW_CONST, "Expected `const`", &token)) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a constant name", &name_token)) {
        return false;
    }

    decl = parser_new_decl(parser, DECL_GLOBAL_CONST);
    if (decl == NULL) {
        return false;
    }

    decl->token = *token;
    decl->name = name_token->lexeme;

    if (parser_match(parser, TOKEN_COLON)) {
        decl->as.global_const.has_annotation = true;
        if (!parser_parse_type(parser, &decl->as.global_const.annotation)) {
            return false;
        }
    }

    if (!parser_expect(parser, TOKEN_ASSIGN, "Expected `=` in constant declaration", &token)) {
        return false;
    }
    if (!parser_parse_expression(parser, &decl->as.global_const.initializer)) {
        return false;
    }

    *out_decl = decl;
    return true;
}

static bool parser_parse_function(Parser *parser, Decl **out_decl) {
    const Token *token;
    const Token *name_token;
    Decl *decl;

    if (!parser_expect(parser, TOKEN_KW_FN, "Expected `fn`", &token)) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a function name", &name_token)) {
        return false;
    }

    decl = parser_new_decl(parser, DECL_FUNCTION);
    if (decl == NULL) {
        return false;
    }

    decl->token = *token;
    decl->name = name_token->lexeme;
    vec_init(&decl->as.function.params, sizeof(Param), parser->arena);

    if (!parser_expect(parser, TOKEN_OPEN_PAREN, "Expected `(` after function name", &token)) {
        return false;
    }
    if (!parser_is(parser, TOKEN_CLOSE_PAREN, 0U)) {
        do {
            Param *param = (Param *)vec_push(&decl->as.function.params, parser->error);
            if (param == NULL) {
                return false;
            }
            if (!parser_parse_param(parser, param)) {
                return false;
            }
        } while (parser_match(parser, TOKEN_COMMA));
    }
    if (!parser_expect(parser, TOKEN_CLOSE_PAREN, "Expected `)` after parameter list", &token)) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_ARROW, "Expected `->` before function return type", &token)) {
        return false;
    }
    if (!parser_parse_type(parser, &decl->as.function.return_type)) {
        return false;
    }
    if (!parser_parse_block(parser, &decl->as.function.body)) {
        return false;
    }

    *out_decl = decl;
    return true;
}

static bool parser_parse_binding_stmt(Parser *parser, Stmt **out_stmt) {
    const Token *token = parser_advance(parser);
    const Token *name_token;
    Stmt *stmt = parser_new_stmt(parser, STMT_BINDING);

    if (stmt == NULL) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a binding name", &name_token)) {
        return false;
    }

    stmt->token = *token;
    stmt->as.binding.is_mutable = token->type == TOKEN_KW_MUT;
    stmt->as.binding.name = name_token->lexeme;

    if (parser_match(parser, TOKEN_COLON)) {
        stmt->as.binding.has_annotation = true;
        if (!parser_parse_type(parser, &stmt->as.binding.annotation)) {
            return false;
        }
    }

    if (!parser_expect(parser, TOKEN_ASSIGN, "Expected `=` in binding declaration", &token)) {
        return false;
    }
    if (!parser_parse_expression(parser, &stmt->as.binding.initializer)) {
        return false;
    }
    if (!parser_require_stmt_end(parser, "binding declaration")) {
        return false;
    }

    *out_stmt = stmt;
    return true;
}

static bool parser_parse_assign_stmt(Parser *parser, Stmt **out_stmt) {
    const Token *name_token;
    const Token *token;
    Stmt *stmt = parser_new_stmt(parser, STMT_ASSIGN);

    if (stmt == NULL) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a binding name", &name_token)) {
        return false;
    }
    stmt->token = *name_token;
    stmt->as.assign.name = name_token->lexeme;

    if (!parser_expect(parser, TOKEN_ASSIGN, "Expected `=` in assignment", &token)) {
        return false;
    }
    if (!parser_parse_expression(parser, &stmt->as.assign.value)) {
        return false;
    }
    if (!parser_require_stmt_end(parser, "assignment")) {
        return false;
    }

    *out_stmt = stmt;
    return true;
}

static bool parser_parse_return_stmt(Parser *parser, Stmt **out_stmt) {
    const Token *token;
    Stmt *stmt = parser_new_stmt(parser, STMT_RETURN);

    if (stmt == NULL) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_KW_RETURN, "Expected `return`", &token)) {
        return false;
    }
    stmt->token = *token;

    if (!parser_parse_expression(parser, &stmt->as.ret.value)) {
        return false;
    }
    if (!parser_require_stmt_end(parser, "return statement")) {
        return false;
    }

    *out_stmt = stmt;
    return true;
}

static bool parser_parse_call_stmt(Parser *parser, Stmt **out_stmt) {
    Expr *expr;
    Stmt *stmt;

    if (!parser_parse_expression(parser, &expr)) {
        return false;
    }
    if (expr->kind != EXPR_CALL) {
        return error_set_at(parser->error, "Parse", expr->token.line, expr->token.column, "Only function calls can be used as statements");
    }

    stmt = parser_new_stmt(parser, STMT_CALL);
    if (stmt == NULL) {
        return false;
    }
    stmt->token = expr->token;
    stmt->as.call.call = expr;

    if (!parser_require_stmt_end(parser, "call statement")) {
        return false;
    }

    *out_stmt = stmt;
    return true;
}

static bool parser_parse_if_stmt(Parser *parser, Stmt **out_stmt) {
    const Token *token;
    Stmt *stmt = parser_new_stmt(parser, STMT_IF);

    if (stmt == NULL) {
        return false;
    }
    vec_init(&stmt->as.if_stmt.elif_branches, sizeof(IfBranch), parser->arena);

    if (!parser_expect(parser, TOKEN_KW_IF, "Expected `if`", &token)) {
        return false;
    }
    if (parser_is(parser, TOKEN_OPEN_PAREN, 0U)) {
        return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Parenthesized conditions are not supported in Rivel v1");
    }

    stmt->token = *token;
    if (!parser_parse_expression(parser, &stmt->as.if_stmt.condition)) {
        return false;
    }
    if (!parser_parse_block(parser, &stmt->as.if_stmt.then_block)) {
        return false;
    }

    while (parser_match(parser, TOKEN_KW_ELIF)) {
        IfBranch *branch = (IfBranch *)vec_push(&stmt->as.if_stmt.elif_branches, parser->error);
        if (branch == NULL) {
            return false;
        }
        branch->token = parser->tokens[parser->index - 1U];
        if (parser_is(parser, TOKEN_OPEN_PAREN, 0U)) {
            return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Parenthesized conditions are not supported in Rivel v1");
        }
        if (!parser_parse_expression(parser, &branch->condition)) {
            return false;
        }
        if (!parser_parse_block(parser, &branch->body)) {
            return false;
        }
    }

    if (parser_match(parser, TOKEN_KW_ELSE) && !parser_parse_block(parser, &stmt->as.if_stmt.else_block)) {
        return false;
    }

    *out_stmt = stmt;
    return true;
}

static bool parser_parse_while_stmt(Parser *parser, Stmt **out_stmt) {
    const Token *token;
    Stmt *stmt = parser_new_stmt(parser, STMT_WHILE);

    if (stmt == NULL) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_KW_WHILE, "Expected `while`", &token)) {
        return false;
    }
    if (parser_is(parser, TOKEN_OPEN_PAREN, 0U)) {
        return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Parenthesized conditions are not supported in Rivel v1");
    }
    stmt->token = *token;
    if (!parser_parse_expression(parser, &stmt->as.while_stmt.condition)) {
        return false;
    }
    if (!parser_parse_block(parser, &stmt->as.while_stmt.body)) {
        return false;
    }

    *out_stmt = stmt;
    return true;
}

static bool parser_parse_block(Parser *parser, Block **out_block) {
    const Token *token;
    Block *block = parser_new_block(parser);

    if (block == NULL) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_OPEN_BRACE, "Expected `{` to start a block", &token)) {
        return false;
    }

    block->token = *token;
    parser_skip_separators(parser);

    while (!parser_is(parser, TOKEN_CLOSE_BRACE, 0U)) {
        Stmt **slot;
        if (parser_is(parser, TOKEN_EOF, 0U)) {
            return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected `}` to close the block");
        }
        slot = (Stmt **)vec_push(&block->statements, parser->error);
        if (slot == NULL) {
            return false;
        }
        if (!parser_parse_statement(parser, slot)) {
            return false;
        }
        parser_skip_separators(parser);
    }

    if (!parser_expect(parser, TOKEN_CLOSE_BRACE, "Expected `}` to close the block", &token)) {
        return false;
    }

    *out_block = block;
    return true;
}

static bool parser_parse_statement(Parser *parser, Stmt **out_stmt) {
    if (parser_is(parser, TOKEN_KW_CONST, 0U) || parser_is(parser, TOKEN_KW_MUT, 0U)) {
        return parser_parse_binding_stmt(parser, out_stmt);
    }
    if (parser_is(parser, TOKEN_KW_RETURN, 0U)) {
        return parser_parse_return_stmt(parser, out_stmt);
    }
    if (parser_is(parser, TOKEN_KW_IF, 0U)) {
        return parser_parse_if_stmt(parser, out_stmt);
    }
    if (parser_is(parser, TOKEN_KW_WHILE, 0U)) {
        return parser_parse_while_stmt(parser, out_stmt);
    }
    if (parser_is(parser, TOKEN_IDENTIFIER, 0U) && parser_is(parser, TOKEN_ASSIGN, 1U)) {
        return parser_parse_assign_stmt(parser, out_stmt);
    }
    if (parser_is(parser, TOKEN_IDENTIFIER, 0U) && parser_is(parser, TOKEN_OPEN_PAREN, 1U)) {
        return parser_parse_call_stmt(parser, out_stmt);
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected a statement");
}

static bool parser_parse_primary(Parser *parser, Expr **out_expr);

static bool parser_make_binary(Parser *parser, Token token, Expr *lhs, Expr *rhs, Expr **out_expr) {
    Expr *expr = parser_new_expr(parser, EXPR_BINARY);
    if (expr == NULL) {
        return false;
    }
    expr->token = token;
    expr->as.binary.op = token.type;
    expr->as.binary.lhs = lhs;
    expr->as.binary.rhs = rhs;
    *out_expr = expr;
    return true;
}

static bool parser_parse_call(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_primary(parser, &expr)) {
        return false;
    }

    while (parser_match(parser, TOKEN_OPEN_PAREN)) {
        Token open_paren = parser->tokens[parser->index - 1U];
        Expr *call_expr;

        if (expr->kind != EXPR_NAME) {
            return error_set_at(parser->error, "Parse", open_paren.line, open_paren.column, "Only named functions can be called in Rivel v1");
        }

        call_expr = parser_new_expr(parser, EXPR_CALL);
        if (call_expr == NULL) {
            return false;
        }
        call_expr->token = expr->token;
        call_expr->as.call.callee = expr->as.name;
        vec_init(&call_expr->as.call.args, sizeof(Expr *), parser->arena);

        if (!parser_is(parser, TOKEN_CLOSE_PAREN, 0U)) {
            do {
                Expr **arg = (Expr **)vec_push(&call_expr->as.call.args, parser->error);
                if (arg == NULL) {
                    return false;
                }
                if (!parser_parse_expression(parser, arg)) {
                    return false;
                }
            } while (parser_match(parser, TOKEN_COMMA));
        }
        if (!parser_expect(parser, TOKEN_CLOSE_PAREN, "Expected `)` after call arguments", (const Token **)&expr)) {
            return false;
        }
        expr = call_expr;
    }

    *out_expr = expr;
    return true;
}

static bool parser_parse_unary(Parser *parser, Expr **out_expr) {
    if (parser_match(parser, TOKEN_KW_NOT) || parser_match(parser, TOKEN_MINUS)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *expr = parser_new_expr(parser, EXPR_UNARY);
        if (expr == NULL) {
            return false;
        }
        expr->token = token;
        expr->as.unary.op = token.type;
        if (!parser_parse_unary(parser, &expr->as.unary.operand)) {
            return false;
        }
        *out_expr = expr;
        return true;
    }
    return parser_parse_call(parser, out_expr);
}

static bool parser_parse_multiplicative(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_unary(parser, &expr)) {
        return false;
    }
    while (parser_match(parser, TOKEN_STAR) || parser_match(parser, TOKEN_SLASH) || parser_match(parser, TOKEN_PERCENT)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *rhs;
        if (!parser_parse_unary(parser, &rhs)) {
            return false;
        }
        if (!parser_make_binary(parser, token, expr, rhs, &expr)) {
            return false;
        }
    }
    *out_expr = expr;
    return true;
}

static bool parser_parse_additive(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_multiplicative(parser, &expr)) {
        return false;
    }
    while (parser_match(parser, TOKEN_PLUS) || parser_match(parser, TOKEN_MINUS)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *rhs;
        if (!parser_parse_multiplicative(parser, &rhs)) {
            return false;
        }
        if (!parser_make_binary(parser, token, expr, rhs, &expr)) {
            return false;
        }
    }
    *out_expr = expr;
    return true;
}

static bool parser_parse_comparison(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_additive(parser, &expr)) {
        return false;
    }
    while (parser_match(parser, TOKEN_LESS) || parser_match(parser, TOKEN_LESS_EQ) || parser_match(parser, TOKEN_GREATER) || parser_match(parser, TOKEN_GREATER_EQ)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *rhs;
        if (!parser_parse_additive(parser, &rhs)) {
            return false;
        }
        if (!parser_make_binary(parser, token, expr, rhs, &expr)) {
            return false;
        }
    }
    *out_expr = expr;
    return true;
}

static bool parser_parse_equality(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_comparison(parser, &expr)) {
        return false;
    }
    while (parser_match(parser, TOKEN_EQ_EQ) || parser_match(parser, TOKEN_BANG_EQ)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *rhs;
        if (!parser_parse_comparison(parser, &rhs)) {
            return false;
        }
        if (!parser_make_binary(parser, token, expr, rhs, &expr)) {
            return false;
        }
    }
    *out_expr = expr;
    return true;
}

static bool parser_parse_and(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_equality(parser, &expr)) {
        return false;
    }
    while (parser_match(parser, TOKEN_KW_AND)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *rhs;
        if (!parser_parse_equality(parser, &rhs)) {
            return false;
        }
        if (!parser_make_binary(parser, token, expr, rhs, &expr)) {
            return false;
        }
    }
    *out_expr = expr;
    return true;
}

static bool parser_parse_or(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_and(parser, &expr)) {
        return false;
    }
    while (parser_match(parser, TOKEN_KW_OR)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *rhs;
        if (!parser_parse_and(parser, &rhs)) {
            return false;
        }
        if (!parser_make_binary(parser, token, expr, rhs, &expr)) {
            return false;
        }
    }
    *out_expr = expr;
    return true;
}

static bool parser_parse_expression(Parser *parser, Expr **out_expr) {
    return parser_parse_or(parser, out_expr);
}

static bool parser_parse_int_literal(Parser *parser, Token token, int64_t *out_value) {
    char *text = arena_copy_slice(parser->arena, token.lexeme, parser->error);
    char *end = NULL;
    long long value;

    if (text == NULL) {
        return false;
    }

    errno = 0;
    value = strtoll(text, &end, 10);
    if (errno == ERANGE) {
        return error_set_at(parser->error, "Parse", token.line, token.column, "Integer literal out of range for Int");
    }
    *out_value = (int64_t)value;
    return true;
}

static bool parser_parse_primary(Parser *parser, Expr **out_expr) {
    if (parser_match(parser, TOKEN_INT_LITERAL)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *expr = parser_new_expr(parser, EXPR_INT);
        if (expr == NULL) {
            return false;
        }
        expr->token = token;
        if (!parser_parse_int_literal(parser, token, &expr->as.int_value)) {
            return false;
        }
        *out_expr = expr;
        return true;
    }
    if (parser_match(parser, TOKEN_BOOL_LITERAL)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *expr = parser_new_expr(parser, EXPR_BOOL);
        if (expr == NULL) {
            return false;
        }
        expr->token = token;
        expr->as.bool_value = slice_equal_cstr(token.lexeme, "true");
        *out_expr = expr;
        return true;
    }
    if (parser_match(parser, TOKEN_IDENTIFIER)) {
        Token token = parser->tokens[parser->index - 1U];
        Expr *expr = parser_new_expr(parser, EXPR_NAME);
        if (expr == NULL) {
            return false;
        }
        expr->token = token;
        expr->as.name = token.lexeme;
        *out_expr = expr;
        return true;
    }
    if (parser_match(parser, TOKEN_OPEN_PAREN)) {
        const Token *token = &parser->tokens[parser->index - 1U];
        Expr *expr;
        if (!parser_parse_expression(parser, &expr)) {
            return false;
        }
        if (!parser_expect(parser, TOKEN_CLOSE_PAREN, "Expected `)` after grouped expression", &token)) {
            return false;
        }
        *out_expr = expr;
        return true;
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected an expression");
}

bool parse_program(const Vec *tokens, Arena *arena, Program *out_program, CompileError *error) {
    Parser parser;
    Decl **slot;

    parser.tokens = (const Token *)tokens->data;
    parser.count = tokens->len;
    parser.index = 0U;
    parser.arena = arena;
    parser.error = error;

    vec_init(&out_program->decls, sizeof(Decl *), arena);
    parser_skip_separators(&parser);

    while (!parser_is(&parser, TOKEN_EOF, 0U)) {
        slot = (Decl **)vec_push(&out_program->decls, error);
        if (slot == NULL) {
            return false;
        }

        if (parser_is(&parser, TOKEN_KW_CONST, 0U)) {
            if (!parser_parse_global_const(&parser, slot)) {
                return false;
            }
        } else if (parser_is(&parser, TOKEN_KW_FN, 0U)) {
            if (!parser_parse_function(&parser, slot)) {
                return false;
            }
        } else if (parser_is(&parser, TOKEN_KW_MUT, 0U)) {
            return error_set_at(error, "Parse", parser_peek(&parser, 0U)->line, parser_peek(&parser, 0U)->column, "Top-level `mut` declarations are not part of Rivel v1");
        } else {
            return error_set_at(error, "Parse", parser_peek(&parser, 0U)->line, parser_peek(&parser, 0U)->column, "Expected a top-level `const` or `fn` declaration");
        }

        if (!parser_consume_decl_end(&parser)) {
            return false;
        }
        parser_skip_separators(&parser);
    }

    return true;
}
