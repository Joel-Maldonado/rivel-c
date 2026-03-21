#include "parser_internal.h"

#include <errno.h>
#include <stdlib.h>

static bool parser_parse_primary(Parser *parser, Expr **out_expr);
static bool parser_parse_struct_literal(Parser *parser, Token name_token, Expr **out_expr);
static bool parser_starts_struct_literal(const Parser *parser);

static bool parser_decode_string_literal(Parser *parser, Token token, StrSlice *out_value) {
    size_t max_len = token.lexeme.len >= 2U ? token.lexeme.len - 2U : 0U;
    char *buffer;
    size_t src_index = 1U;
    size_t dst_index = 0U;

    if (max_len == 0U) {
        buffer = arena_copy_cstr(parser->arena, "", parser->error);
    } else {
        buffer = (char *)arena_alloc(parser->arena, max_len, _Alignof(char), parser->error);
    }
    if (buffer == NULL) {
        return false;
    }

    while (src_index + 1U < token.lexeme.len) {
        char current = token.lexeme.data[src_index];

        if (current != '\\') {
            buffer[dst_index++] = current;
            src_index += 1U;
            continue;
        }

        src_index += 1U;
        if (src_index >= token.lexeme.len - 1U) {
            return error_set_at(parser->error, "Parse", token.line, token.column + (int)src_index, "Unterminated string escape");
        }

        current = token.lexeme.data[src_index];
        if (current == '\\') {
            buffer[dst_index++] = '\\';
        } else if (current == '"') {
            buffer[dst_index++] = '"';
        } else if (current == 'n') {
            buffer[dst_index++] = '\n';
        } else if (current == 'r') {
            buffer[dst_index++] = '\r';
        } else if (current == 't') {
            buffer[dst_index++] = '\t';
        } else {
            return error_set_at(parser->error, "Parse", token.line, token.column + (int)src_index, "Unsupported escape sequence");
        }
        src_index += 1U;
    }

    *out_value = slice_from_parts(buffer, dst_index);
    return true;
}

static bool parser_starts_struct_literal(const Parser *parser) {
    size_t offset = 0U;

    if (!parser_is(parser, TOKEN_OPEN_BRACE, 0U)) {
        return false;
    }
    while (parser_is(parser, TOKEN_END_STMT, offset + 1U)) {
        offset += 1U;
    }
    if (parser_is(parser, TOKEN_CLOSE_BRACE, offset + 1U)) {
        return true;
    }
    return parser_is(parser, TOKEN_IDENTIFIER, offset + 1U) && parser_is(parser, TOKEN_COLON, offset + 2U);
}

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

static bool parser_parse_postfix(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_primary(parser, &expr)) {
        return false;
    }

    while (true) {
        if (parser_match(parser, TOKEN_OPEN_PAREN)) {
            Token open_paren = *parser_previous(parser, 0U);
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
            expr_list_init(&call_expr->as.call.args, parser->arena);

            if (!parser_is(parser, TOKEN_CLOSE_PAREN, 0U)) {
                do {
                    Expr **arg = expr_list_push(&call_expr->as.call.args, parser->error);
                    if (arg == NULL) {
                        return false;
                    }
                    if (!parser_parse_expression(parser, arg)) {
                        return false;
                    }
                } while (parser_match(parser, TOKEN_COMMA));
            }
            if (!parser_expect(parser, TOKEN_CLOSE_PAREN, "Expected `)` after call arguments", NULL)) {
                return false;
            }
            expr = call_expr;
            continue;
        }
        if (parser_match(parser, TOKEN_DOT)) {
            const Token *field_token;
            Expr *field_expr = parser_new_expr(parser, EXPR_FIELD);

            if (field_expr == NULL) {
                return false;
            }
            if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a field name after `.`", &field_token)) {
                return false;
            }
            field_expr->token = *field_token;
            field_expr->as.field.base = expr;
            field_expr->as.field.name = field_token->lexeme;
            expr = field_expr;
            continue;
        }
        break;
    }

    *out_expr = expr;
    return true;
}

static bool parser_parse_unary(Parser *parser, Expr **out_expr) {
    if (parser_match(parser, TOKEN_KW_NOT) || parser_match(parser, TOKEN_MINUS)) {
        Token token = *parser_previous(parser, 0U);
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
    return parser_parse_postfix(parser, out_expr);
}

static bool parser_parse_multiplicative(Parser *parser, Expr **out_expr) {
    Expr *expr;

    if (!parser_parse_unary(parser, &expr)) {
        return false;
    }
    while (parser_match(parser, TOKEN_STAR) || parser_match(parser, TOKEN_SLASH) || parser_match(parser, TOKEN_PERCENT)) {
        Token token = *parser_previous(parser, 0U);
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
        Token token = *parser_previous(parser, 0U);
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
        Token token = *parser_previous(parser, 0U);
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
        Token token = *parser_previous(parser, 0U);
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
        Token token = *parser_previous(parser, 0U);
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
        Token token = *parser_previous(parser, 0U);
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

bool parser_parse_expression(Parser *parser, Expr **out_expr) {
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

static bool parser_parse_double_literal(Parser *parser, Token token, double *out_value) {
    char *text = arena_copy_slice(parser->arena, token.lexeme, parser->error);
    char *end = NULL;
    double value;

    if (text == NULL) {
        return false;
    }

    errno = 0;
    value = strtod(text, &end);
    if (errno == ERANGE) {
        return error_set_at(parser->error, "Parse", token.line, token.column, "Double literal out of range for Double");
    }
    *out_value = value;
    return true;
}

static bool parser_parse_struct_literal(Parser *parser, Token name_token, Expr **out_expr) {
    Expr *expr = parser_new_expr(parser, EXPR_STRUCT_LITERAL);

    if (expr == NULL) {
        return false;
    }
    expr->token = name_token;
    expr->as.struct_literal.struct_name = name_token.lexeme;
    struct_literal_field_list_init(&expr->as.struct_literal.fields, parser->arena);

    if (!parser_expect(parser, TOKEN_OPEN_BRACE, "Expected `{` after struct name", NULL)) {
        return false;
    }
    parser_skip_separators(parser);
    while (!parser_is(parser, TOKEN_CLOSE_BRACE, 0U)) {
        const Token *field_token;
        StructLiteralField *field = struct_literal_field_list_push(&expr->as.struct_literal.fields, parser->error);

        if (field == NULL) {
            return false;
        }
        if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a field name", &field_token)) {
            return false;
        }
        if (!parser_expect(parser, TOKEN_COLON, "Expected `:` after field name", NULL)) {
            return false;
        }
        field->token = *field_token;
        field->name = field_token->lexeme;
        if (!parser_parse_expression(parser, &field->value)) {
            return false;
        }

        if (parser_match(parser, TOKEN_COMMA) || parser_match(parser, TOKEN_END_STMT)) {
            parser_skip_separators(parser);
            continue;
        }
        if (!parser_is(parser, TOKEN_CLOSE_BRACE, 0U)) {
            return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected `,` or `}` after struct literal field");
        }
    }
    if (!parser_expect(parser, TOKEN_CLOSE_BRACE, "Expected `}` after struct literal", NULL)) {
        return false;
    }

    *out_expr = expr;
    return true;
}

static bool parser_parse_primary(Parser *parser, Expr **out_expr) {
    if (parser_match(parser, TOKEN_INT_LITERAL)) {
        Token token = *parser_previous(parser, 0U);
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
    if (parser_match(parser, TOKEN_DOUBLE_LITERAL)) {
        Token token = *parser_previous(parser, 0U);
        Expr *expr = parser_new_expr(parser, EXPR_DOUBLE);

        if (expr == NULL) {
            return false;
        }
        expr->token = token;
        if (!parser_parse_double_literal(parser, token, &expr->as.double_value)) {
            return false;
        }
        *out_expr = expr;
        return true;
    }
    if (parser_match(parser, TOKEN_BOOL_LITERAL)) {
        Token token = *parser_previous(parser, 0U);
        Expr *expr = parser_new_expr(parser, EXPR_BOOL);

        if (expr == NULL) {
            return false;
        }
        expr->token = token;
        expr->as.bool_value = slice_equal_cstr(token.lexeme, "true");
        *out_expr = expr;
        return true;
    }
    if (parser_match(parser, TOKEN_STRING_LITERAL)) {
        Token token = *parser_previous(parser, 0U);
        Expr *expr = parser_new_expr(parser, EXPR_STRING);

        if (expr == NULL) {
            return false;
        }
        expr->token = token;
        if (!parser_decode_string_literal(parser, token, &expr->as.string_value)) {
            return false;
        }
        *out_expr = expr;
        return true;
    }
    if (parser_match(parser, TOKEN_IDENTIFIER)) {
        Token token = *parser_previous(parser, 0U);
        Expr *expr = parser_new_expr(parser, EXPR_NAME);

        if (expr == NULL) {
            return false;
        }
        if (parser_starts_struct_literal(parser)) {
            return parser_parse_struct_literal(parser, token, out_expr);
        }
        expr->token = token;
        expr->as.name = token.lexeme;
        *out_expr = expr;
        return true;
    }
    if (parser_match(parser, TOKEN_OPEN_PAREN)) {
        Expr *expr;

        if (!parser_parse_expression(parser, &expr)) {
            return false;
        }
        if (!parser_expect(parser, TOKEN_CLOSE_PAREN, "Expected `)` after grouped expression", NULL)) {
            return false;
        }
        *out_expr = expr;
        return true;
    }
    return error_set_at(parser->error, "Parse", parser_peek(parser, 0U)->line, parser_peek(parser, 0U)->column, "Expected an expression");
}
