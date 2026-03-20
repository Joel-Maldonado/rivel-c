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
    Vec tokens;
    CompileError *error;
} Tokenizer;

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
    Token token;
    token.type = type;
    token.lexeme = slice_from_parts(start, len);
    token.line = line;
    token.column = column;
    return vec_push_copy(&tokenizer->tokens, &token, tokenizer->error);
}

static bool tokenizer_emit_separator(Tokenizer *tokenizer, int line, int column) {
    Token *last;

    if (tokenizer->tokens.len > 0U) {
        last = (Token *)vec_get(&tokenizer->tokens, tokenizer->tokens.len - 1U);
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

static bool tokenizer_add_identifier_like(Tokenizer *tokenizer, const char *start, size_t len, int line, int column) {
    StrSlice lexeme = slice_from_parts(start, len);

    if (slice_equal_cstr(lexeme, "true") || slice_equal_cstr(lexeme, "false")) {
        return tokenizer_add_token(tokenizer, TOKEN_BOOL_LITERAL, start, len, line, column);
    }

    if (slice_equal_cstr(lexeme, "let")) {
        return error_set_at(tokenizer->error, "Lexer", line, column, "Rivel v1 does not support legacy keyword `let`");
    }
    if (slice_equal_cstr(lexeme, "exit")) {
        return error_set_at(tokenizer->error, "Lexer", line, column, "Rivel v1 does not support legacy keyword `exit`");
    }
    if (slice_equal_cstr(lexeme, "import")) {
        return error_set_at(tokenizer->error, "Lexer", line, column, "Rivel v1 does not support `import`");
    }
    if (slice_equal_cstr(lexeme, "from")) {
        return error_set_at(tokenizer->error, "Lexer", line, column, "Rivel v1 does not support `from` imports");
    }
    if (slice_equal_cstr(lexeme, "for")) {
        return error_set_at(tokenizer->error, "Lexer", line, column, "Rivel v1 does not support `for ... in ...`");
    }

    if (slice_equal_cstr(lexeme, "const")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_CONST, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "mut")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_MUT, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "fn")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_FN, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "return")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_RETURN, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "if")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_IF, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "elif")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_ELIF, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "else")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_ELSE, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "while")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_WHILE, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "and")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_AND, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "or")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_OR, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "not")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_NOT, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "Int")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_TYPE_INT, start, len, line, column);
    }
    if (slice_equal_cstr(lexeme, "Bool")) {
        return tokenizer_add_token(tokenizer, TOKEN_KW_TYPE_BOOL, start, len, line, column);
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

static bool tokenizer_tokenize_int(Tokenizer *tokenizer) {
    const char *start = tokenizer->source + tokenizer->index;
    int line = tokenizer->line;
    int column = tokenizer->column;

    tokenizer_advance(tokenizer);
    while (!tokenizer_is_at_end(tokenizer) && isdigit((unsigned char)tokenizer_peek(tokenizer, 0U))) {
        tokenizer_advance(tokenizer);
    }

    return tokenizer_add_token(tokenizer, TOKEN_INT_LITERAL, start, (size_t)(tokenizer->source + tokenizer->index - start), line, column);
}

static bool tokenizer_single(Tokenizer *tokenizer, TokenType type) {
    const char *start = tokenizer->source + tokenizer->index;
    int line = tokenizer->line;
    int column = tokenizer->column;
    tokenizer_advance(tokenizer);
    return tokenizer_add_token(tokenizer, type, start, 1U, line, column);
}

bool tokenize_source(const char *source, Arena *arena, Vec *out_tokens, CompileError *error) {
    Tokenizer tokenizer;

    tokenizer.source = source;
    tokenizer.length = strlen(source);
    tokenizer.index = 0U;
    tokenizer.line = 1;
    tokenizer.column = 1;
    tokenizer.paren_depth = 0;
    tokenizer.error = error;
    vec_init(&tokenizer.tokens, sizeof(Token), arena);

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
            if (!tokenizer_tokenize_int(&tokenizer)) {
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
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) == '>') {
                    tokenizer_advance(&tokenizer);
                    if (!tokenizer_add_token(&tokenizer, TOKEN_ARROW, tokenizer.source + tokenizer.index - 2U, 2U, line, column)) {
                        return false;
                    }
                } else if (!tokenizer_add_token(&tokenizer, TOKEN_MINUS, tokenizer.source + tokenizer.index - 1U, 1U, line, column)) {
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
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) == '=') {
                    tokenizer_advance(&tokenizer);
                    if (!tokenizer_add_token(&tokenizer, TOKEN_EQ_EQ, tokenizer.source + tokenizer.index - 2U, 2U, line, column)) {
                        return false;
                    }
                } else if (!tokenizer_add_token(&tokenizer, TOKEN_ASSIGN, tokenizer.source + tokenizer.index - 1U, 1U, line, column)) {
                    return false;
                }
                break;
            case '!':
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) == '=') {
                    tokenizer_advance(&tokenizer);
                    if (!tokenizer_add_token(&tokenizer, TOKEN_BANG_EQ, tokenizer.source + tokenizer.index - 2U, 2U, line, column)) {
                        return false;
                    }
                } else {
                    return error_set_at(error, "Lexer", line, column, "Unexpected `!`; use `not` for logical negation");
                }
                break;
            case '<':
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) == '=') {
                    tokenizer_advance(&tokenizer);
                    if (!tokenizer_add_token(&tokenizer, TOKEN_LESS_EQ, tokenizer.source + tokenizer.index - 2U, 2U, line, column)) {
                        return false;
                    }
                } else if (!tokenizer_add_token(&tokenizer, TOKEN_LESS, tokenizer.source + tokenizer.index - 1U, 1U, line, column)) {
                    return false;
                }
                break;
            case '>':
                tokenizer_advance(&tokenizer);
                if (tokenizer_peek(&tokenizer, 0U) == '=') {
                    tokenizer_advance(&tokenizer);
                    if (!tokenizer_add_token(&tokenizer, TOKEN_GREATER_EQ, tokenizer.source + tokenizer.index - 2U, 2U, line, column)) {
                        return false;
                    }
                } else if (!tokenizer_add_token(&tokenizer, TOKEN_GREATER, tokenizer.source + tokenizer.index - 1U, 1U, line, column)) {
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
                return error_set_at(error, "Lexer", line, column, "Member access is not part of Rivel v1");
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
