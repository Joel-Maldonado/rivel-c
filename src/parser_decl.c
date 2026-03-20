#include "parser_internal.h"

static bool parser_parse_param(Parser *parser, Param *out_param) {
    const Token *name_token;

    if (!parser_expect(parser, TOKEN_IDENTIFIER, "Expected a parameter name", &name_token)) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_COLON, "Expected `:` after parameter name", NULL)) {
        return false;
    }

    out_param->token = *parser_previous(parser, 1U);
    out_param->name = out_param->token.lexeme;
    return parser_parse_type(parser, &out_param->type);
}

bool parser_parse_global_const(Parser *parser, Decl **out_decl) {
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

    if (!parser_expect(parser, TOKEN_ASSIGN, "Expected `=` in constant declaration", NULL)) {
        return false;
    }
    if (!parser_parse_expression(parser, &decl->as.global_const.initializer)) {
        return false;
    }

    *out_decl = decl;
    return true;
}

bool parser_parse_function(Parser *parser, Decl **out_decl) {
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
    param_list_init(&decl->as.function.params, parser->arena);

    if (!parser_expect(parser, TOKEN_OPEN_PAREN, "Expected `(` after function name", NULL)) {
        return false;
    }
    if (!parser_is(parser, TOKEN_CLOSE_PAREN, 0U)) {
        do {
            Param *param = param_list_push(&decl->as.function.params, parser->error);
            if (param == NULL) {
                return false;
            }
            if (!parser_parse_param(parser, param)) {
                return false;
            }
        } while (parser_match(parser, TOKEN_COMMA));
    }
    if (!parser_expect(parser, TOKEN_CLOSE_PAREN, "Expected `)` after parameter list", NULL)) {
        return false;
    }
    if (!parser_expect(parser, TOKEN_ARROW, "Expected `->` before function return type", NULL)) {
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
