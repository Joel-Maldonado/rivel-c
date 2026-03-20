#include "parser_internal.h"

bool parse_program(const TokenList *tokens, Arena *arena, Program *out_program, CompileError *error) {
    Parser parser;
    Decl **slot;

    parser.tokens = tokens;
    parser.index = 0U;
    parser.arena = arena;
    parser.error = error;

    decl_list_init(&out_program->decls, arena);
    parser_skip_separators(&parser);

    while (!parser_is(&parser, TOKEN_EOF, 0U)) {
        slot = decl_list_push(&out_program->decls, error);
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
