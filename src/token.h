#ifndef RIVEL_TOKEN_H
#define RIVEL_TOKEN_H

#include "slice.h"

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

const char *token_name(TokenType type);

#endif
