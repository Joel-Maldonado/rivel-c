#include "parser_internal.h"

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

    if (!parser_expect(parser, TOKEN_ASSIGN, "Expected `=` in binding declaration", NULL)) {
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
    Stmt *stmt = parser_new_stmt(parser, STMT_ASSIGN);

    if (stmt == NULL) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a binding name", &name_token)) {
        return false;
    }
    stmt->token = *name_token;
    stmt->as.assign.name = name_token->lexeme;

    if (!parser_expect(parser, TOKEN_ASSIGN, "Expected `=` in assignment", NULL)) {
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
    if_branch_list_init(&stmt->as.if_stmt.elif_branches, parser->arena);

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
        IfBranch *branch = if_branch_list_push(&stmt->as.if_stmt.elif_branches, parser->error);
        if (branch == NULL) {
            return false;
        }
        branch->token = *parser_previous(parser, 0U);
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

bool parser_parse_block(Parser *parser, Block **out_block) {
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
        slot = stmt_list_push(&block->statements, parser->error);
        if (slot == NULL) {
            return false;
        }
        if (!parser_parse_statement(parser, slot)) {
            return false;
        }
        parser_skip_separators(parser);
    }

    if (!parser_expect(parser, TOKEN_CLOSE_BRACE, "Expected `}` to close the block", NULL)) {
        return false;
    }

    *out_block = block;
    return true;
}

bool parser_parse_statement(Parser *parser, Stmt **out_stmt) {
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
