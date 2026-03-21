#ifndef RIVEL_TOKEN_H
#define RIVEL_TOKEN_H

#include <stddef.h>

#include "arena.h"
#include "error.h"
#include "slice.h"
#include "vec.h"

typedef enum {
    TOKEN_EOF,
    TOKEN_END_STMT,
    TOKEN_IDENTIFIER,
    TOKEN_INT_LITERAL,
    TOKEN_BOOL_LITERAL,
    TOKEN_KW_CONST,
    TOKEN_KW_MUT,
    TOKEN_KW_FN,
    TOKEN_KW_RETURN,
    TOKEN_KW_IF,
    TOKEN_KW_ELIF,
    TOKEN_KW_ELSE,
    TOKEN_KW_WHILE,
    TOKEN_KW_FOR,
    TOKEN_KW_IN,
    TOKEN_KW_AND,
    TOKEN_KW_OR,
    TOKEN_KW_NOT,
    TOKEN_KW_TYPE_INT,
    TOKEN_KW_TYPE_BOOL,
    TOKEN_OPEN_PAREN,
    TOKEN_CLOSE_PAREN,
    TOKEN_OPEN_BRACE,
    TOKEN_CLOSE_BRACE,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_DOT_DOT,
    TOKEN_DOT_DOT_EQ,
    TOKEN_ARROW,
    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_EQ_EQ,
    TOKEN_BANG_EQ,
    TOKEN_LESS,
    TOKEN_LESS_EQ,
    TOKEN_GREATER,
    TOKEN_GREATER_EQ
} TokenType;

typedef struct {
    TokenType type;
    StrSlice lexeme;
    int line;
    int column;
} Token;

typedef struct {
    Vec storage;
} TokenList;

void token_list_init(TokenList *list, Arena *arena);
size_t token_list_len(const TokenList *list);
Token *token_list_push(TokenList *list, CompileError *error);
Token *token_list_get(TokenList *list, size_t index);
const Token *token_list_get_const(const TokenList *list, size_t index);

const char *token_name(TokenType type);

#endif
