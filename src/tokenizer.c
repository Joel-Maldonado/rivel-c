#include "tokenizer.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    const char *source;
    size_t length;
    size_t index;
    int line;
    int column;
    int paren_depth;
    TokenList tokens;
    CompileError *error;
} Tokenizer;

typedef struct {
    const char *name;
    const char *message;
} UnsupportedIdentifier;

typedef struct {
    const char *name;
    TokenType type;
} Keyword;

static const UnsupportedIdentifier TOKENIZER_UNSUPPORTED_IDENTIFIERS[] = {
    {"let", "Rivel v1 does not support legacy keyword `let`"},
    {"exit", "Rivel v1 does not support legacy keyword `exit`"},
    {"import", "Rivel v1 does not support `import`"},
    {"from", "Rivel v1 does not support `from` imports"},
};

static const Keyword TOKENIZER_KEYWORDS[] = {
    {"true", TOKEN_BOOL_LITERAL},
    {"false", TOKEN_BOOL_LITERAL},
    {"const", TOKEN_KW_CONST},
    {"mut", TOKEN_KW_MUT},
    {"fn", TOKEN_KW_FN},
    {"return", TOKEN_KW_RETURN},
    {"if", TOKEN_KW_IF},
    {"elif", TOKEN_KW_ELIF},
    {"else", TOKEN_KW_ELSE},
    {"while", TOKEN_KW_WHILE},
    {"for", TOKEN_KW_FOR},
    {"in", TOKEN_KW_IN},
    {"and", TOKEN_KW_AND},
    {"or", TOKEN_KW_OR},
    {"not", TOKEN_KW_NOT},
    {"Int", TOKEN_KW_TYPE_INT},
    {"Double", TOKEN_KW_TYPE_DOUBLE},
    {"Bool", TOKEN_KW_TYPE_BOOL},
};

static bool tokenizer_is_at_end(const Tokenizer *tokenizer) {
    return tokenizer->index >= tokenizer->length;
}

static char tokenizer_peek(const Tokenizer *tokenizer, size_t offset) {
    if (tokenizer->index + offset >= tokenizer->length) {
        return '\0';
    }
    return tokenizer->source[tokenizer->index + offset];
}

static char tokenizer_advance(Tokenizer *tokenizer) {
    char ch = tokenizer->source[tokenizer->index];
    tokenizer->index += 1U;
    if (ch == '\n') {
        tokenizer->line += 1;
        tokenizer->column = 1;
    } else {
        tokenizer->column += 1;
    }
    return ch;
}

static bool tokenizer_add_token(Tokenizer *tokenizer, TokenType type, const char *start, size_t len, int line, int column) {
    Token *token = token_list_push(&tokenizer->tokens, tokenizer->error);

    if (token == NULL) {
        return false;
    }
    token->type = type;
    token->lexeme = slice_from_parts(start, len);
    token->line = line;
    token->column = column;
    return true;
}

static bool tokenizer_emit_separator(Tokenizer *tokenizer, int line, int column) {
    Token *last;

    if (token_list_len(&tokenizer->tokens) > 0U) {
        last = token_list_get(&tokenizer->tokens, token_list_len(&tokenizer->tokens) - 1U);
        if (last->type == TOKEN_END_STMT) {
            return true;
        }
    }

    return tokenizer_add_token(tokenizer, TOKEN_END_STMT, ";", 1U, line, column);
}

static bool tokenizer_emit_newline_separator(Tokenizer *tokenizer) {
    int line = tokenizer->line;
    int column = tokenizer->column;

    tokenizer_advance(tokenizer);
    if (tokenizer->paren_depth == 0) {
        return tokenizer_emit_separator(tokenizer, line, column);
    }
    return true;
}

static void tokenizer_skip_comment(Tokenizer *tokenizer) {
    while (!tokenizer_is_at_end(tokenizer) && tokenizer_peek(tokenizer, 0U) != '\n') {
        tokenizer_advance(tokenizer);
    }
}

static const char *tokenizer_unsupported_identifier_message(StrSlice lexeme) {
    size_t index = 0U;

    while (index < sizeof(TOKENIZER_UNSUPPORTED_IDENTIFIERS) / sizeof(TOKENIZER_UNSUPPORTED_IDENTIFIERS[0])) {
        if (slice_equal_cstr(lexeme, TOKENIZER_UNSUPPORTED_IDENTIFIERS[index].name)) {
            return TOKENIZER_UNSUPPORTED_IDENTIFIERS[index].message;
        }
        index += 1U;
    }
    return NULL;
}

static bool tokenizer_lookup_keyword(StrSlice lexeme, TokenType *out_type) {
    size_t index = 0U;

    while (index < sizeof(TOKENIZER_KEYWORDS) / sizeof(TOKENIZER_KEYWORDS[0])) {
        if (slice_equal_cstr(lexeme, TOKENIZER_KEYWORDS[index].name)) {
            *out_type = TOKENIZER_KEYWORDS[index].type;
            return true;
        }
        index += 1U;
    }
    return false;
}

static bool tokenizer_add_identifier_like(Tokenizer *tokenizer, const char *start, size_t len, int line, int column) {
    StrSlice lexeme = slice_from_parts(start, len);
    const char *unsupported_message;
    TokenType keyword_type;

    unsupported_message = tokenizer_unsupported_identifier_message(lexeme);
    if (unsupported_message != NULL) {
        return error_set_at(tokenizer->error, "Lexer", line, column, "%s", unsupported_message);
    }
    if (tokenizer_lookup_keyword(lexeme, &keyword_type)) {
        return tokenizer_add_token(tokenizer, keyword_type, start, len, line, column);
    }

    return tokenizer_add_token(tokenizer, TOKEN_IDENTIFIER, start, len, line, column);
}

static bool tokenizer_tokenize_identifier(Tokenizer *tokenizer) {
    const char *start = tokenizer->source + tokenizer->index;
    int line = tokenizer->line;
    int column = tokenizer->column;

    tokenizer_advance(tokenizer);
    while (!tokenizer_is_at_end(tokenizer)) {
        char ch = tokenizer_peek(tokenizer, 0U);
        if (!isalnum((unsigned char)ch) && ch != '_') {
            break;
        }
        tokenizer_advance(tokenizer);
    }

    return tokenizer_add_identifier_like(tokenizer, start, (size_t)(tokenizer->source + tokenizer->index - start), line, column);
}

static bool tokenizer_add_number_token(Tokenizer *tokenizer, const char *start, int line, int column, bool is_double) {
    size_t len = (size_t)(tokenizer->source + tokenizer->index - start);
    TokenType type = is_double ? TOKEN_DOUBLE_LITERAL : TOKEN_INT_LITERAL;

    return tokenizer_add_token(tokenizer, type, start, len, line, column);
}

static bool tokenizer_tokenize_number(Tokenizer *tokenizer) {
    const char *start = tokenizer->source + tokenizer->index;
    int line = tokenizer->line;
    int column = tokenizer->column;
    bool is_double = false;

    tokenizer_advance(tokenizer);
    while (!tokenizer_is_at_end(tokenizer) && isdigit((unsigned char)tokenizer_peek(tokenizer, 0U))) {
        tokenizer_advance(tokenizer);
    }

    if (tokenizer_peek(tokenizer, 0U) == '.' && tokenizer_peek(tokenizer, 1U) != '.') {
        is_double = true;
        tokenizer_advance(tokenizer);
        while (!tokenizer_is_at_end(tokenizer) && isdigit((unsigned char)tokenizer_peek(tokenizer, 0U))) {
            tokenizer_advance(tokenizer);
        }
    }

    if (tokenizer_peek(tokenizer, 0U) == 'e' || tokenizer_peek(tokenizer, 0U) == 'E') {
        return error_set_at(tokenizer->error, "Lexer", line, column, "Exponent notation is not part of Rivel v1 doubles");
    }

    return tokenizer_add_number_token(tokenizer, start, line, column, is_double);
}

static bool tokenizer_single(Tokenizer *tokenizer, TokenType type) {
    const char *start = tokenizer->source + tokenizer->index;
    int line = tokenizer->line;
    int column = tokenizer->column;
    tokenizer_advance(tokenizer);
    return tokenizer_add_token(tokenizer, type, start, 1U, line, column);
}

static bool tokenizer_emit_optional_pair(Tokenizer *tokenizer, char second, TokenType pair_type, TokenType single_type, int line, int column) {
    const char *start = tokenizer->source + tokenizer->index;

    tokenizer_advance(tokenizer);
    if (tokenizer_peek(tokenizer, 0U) == second) {
        tokenizer_advance(tokenizer);
        return tokenizer_add_token(tokenizer, pair_type, start, 2U, line, column);
    }
    return tokenizer_add_token(tokenizer, single_type, start, 1U, line, column);
}

static bool tokenizer_emit_required_pair(Tokenizer *tokenizer, char second, TokenType pair_type, int line, int column, const char *message) {
    const char *start = tokenizer->source + tokenizer->index;

    tokenizer_advance(tokenizer);
    if (tokenizer_peek(tokenizer, 0U) != second) {
        return error_set_at(tokenizer->error, "Lexer", line, column, "%s", message);
    }
    tokenizer_advance(tokenizer);
    return tokenizer_add_token(tokenizer, pair_type, start, 2U, line, column);
}

bool tokenize_source(const char *source, Arena *arena, TokenList *out_tokens, CompileError *error) {
    Tokenizer tokenizer;

    tokenizer.source = source;
    tokenizer.length = strlen(source);
    tokenizer.index = 0U;
    tokenizer.line = 1;
    tokenizer.column = 1;
    tokenizer.paren_depth = 0;
    tokenizer.error = error;
    token_list_init(&tokenizer.tokens, arena);

    while (!tokenizer_is_at_end(&tokenizer)) {
        char current = tokenizer_peek(&tokenizer, 0U);
        int line = tokenizer.line;
        int column = tokenizer.column;

        if (current == ' ' || current == '\t' || current == '\r') {
            tokenizer_advance(&tokenizer);
            continue;
        }
        if (current == '\n') {
            if (!tokenizer_emit_newline_separator(&tokenizer)) {
                return false;
            }
            continue;
        }
        if (current == '#') {
            tokenizer_skip_comment(&tokenizer);
            continue;
        }
        if (isalpha((unsigned char)current) || current == '_') {
            if (!tokenizer_tokenize_identifier(&tokenizer)) {
                return false;
            }
            continue;
        }
        if (isdigit((unsigned char)current)) {
            if (!tokenizer_tokenize_number(&tokenizer)) {
                return false;
            }
            continue;
        }

        switch (current) {
            case ';':
                tokenizer_advance(&tokenizer);
                if (!tokenizer_emit_separator(&tokenizer, line, column)) {
                    return false;
                }
                break;
            case '(':
                tokenizer.paren_depth += 1;
                if (!tokenizer_single(&tokenizer, TOKEN_OPEN_PAREN)) {
                    return false;
                }
                break;
            case ')':
                if (tokenizer.paren_depth > 0) {
                    tokenizer.paren_depth -= 1;
                }
                if (!tokenizer_single(&tokenizer, TOKEN_CLOSE_PAREN)) {
                    return false;
                }
                break;
            case '{':
                if (!tokenizer_single(&tokenizer, TOKEN_OPEN_BRACE)) {
                    return false;
                }
                break;
            case '}':
                if (!tokenizer_single(&tokenizer, TOKEN_CLOSE_BRACE)) {
                    return false;
                }
                break;
            case ',':
                if (!tokenizer_single(&tokenizer, TOKEN_COMMA)) {
                    return false;
                }
                break;
            case ':':
                if (!tokenizer_single(&tokenizer, TOKEN_COLON)) {
                    return false;
                }
                break;
            case '+':
                if (!tokenizer_single(&tokenizer, TOKEN_PLUS)) {
                    return false;
                }
                break;
            case '-':
                if (!tokenizer_emit_optional_pair(&tokenizer, '>', TOKEN_ARROW, TOKEN_MINUS, line, column)) {
                    return false;
                }
                break;
            case '*':
                if (!tokenizer_single(&tokenizer, TOKEN_STAR)) {
                    return false;
                }
                break;
            case '/':
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) == '/' || tokenizer_peek(&tokenizer, 0U) == '*') {
                    return error_set_at(error, "Lexer", line, column, "Rivel v1 only supports `#` comments");
                }
                if (!tokenizer_add_token(&tokenizer, TOKEN_SLASH, tokenizer.source + tokenizer.index - 1U, 1U, line, column)) {
                    return false;
                }
                break;
            case '%':
                if (!tokenizer_single(&tokenizer, TOKEN_PERCENT)) {
                    return false;
                }
                break;
            case '=':
                if (!tokenizer_emit_optional_pair(&tokenizer, '=', TOKEN_EQ_EQ, TOKEN_ASSIGN, line, column)) {
                    return false;
                }
                break;
            case '!':
                if (!tokenizer_emit_required_pair(&tokenizer, '=', TOKEN_BANG_EQ, line, column, "Unexpected `!`; use `not` for logical negation")) {
                    return false;
                }
                break;
            case '<':
                if (!tokenizer_emit_optional_pair(&tokenizer, '=', TOKEN_LESS_EQ, TOKEN_LESS, line, column)) {
                    return false;
                }
                break;
            case '>':
                if (!tokenizer_emit_optional_pair(&tokenizer, '=', TOKEN_GREATER_EQ, TOKEN_GREATER, line, column)) {
                    return false;
                }
                break;
            case '"':
                return error_set_at(error, "Lexer", line, column, "String literals are not part of Rivel v1");
            case '\'':
                return error_set_at(error, "Lexer", line, column, "Character literals are not part of Rivel v1");
            case '[':
            case ']':
                return error_set_at(error, "Lexer", line, column, "List syntax is not part of Rivel v1");
            case '.':
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) != '.') {
                    return error_set_at(error, "Lexer", line, column, "Member access is not part of Rivel v1");
                }
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) == '=') {
                    tokenizer_advance(&tokenizer);
                    if (!tokenizer_add_token(&tokenizer, TOKEN_DOT_DOT_EQ, tokenizer.source + tokenizer.index - 3U, 3U, line, column)) {
                        return false;
                    }
                    break;
                }
                if (!tokenizer_add_token(&tokenizer, TOKEN_DOT_DOT, tokenizer.source + tokenizer.index - 2U, 2U, line, column)) {
                    return false;
                }
                break;
            default:
                return error_set_at(error, "Lexer", line, column, "Unexpected character `%c`", current);
        }
    }

    if (!tokenizer_add_token(&tokenizer, TOKEN_EOF, tokenizer.source + tokenizer.length, 0U, tokenizer.line, tokenizer.column)) {
        return false;
    }

    *out_tokens = tokenizer.tokens;
    return true;
}
