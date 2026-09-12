#ifndef RV_LEX_TOKEN_H
#define RV_LEX_TOKEN_H

#include <stdint.h>

#include "base/source.h"
#include "base/str.h"
#include "base/vec.h"

typedef enum TokKind {
    TOK_EOF,

    TOK_IDENT,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,

    /* f"text{expr}text" lexes as FSTR_START (TEXT | EXPR_START tokens EXPR_END)* FSTR_END */
    TOK_FSTR_START,
    TOK_FSTR_TEXT,
    TOK_FSTR_EXPR_START,
    TOK_FSTR_EXPR_END,
    TOK_FSTR_END,

    TOK_KW_FUNC,
    TOK_KW_STRUCT,
    TOK_KW_SELF,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_FOR,
    TOK_KW_IN,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_RETURN,
    TOK_KW_TRUE,
    TOK_KW_FALSE,
    TOK_KW_NULL,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_DOT,
    TOK_DOTDOT,
    TOK_DOTDOTEQ,
    TOK_COLON,
    TOK_SEMI,
    TOK_ARROW,
    TOK_QUESTION,
    TOK_QQ,

    TOK_ASSIGN,
    TOK_DEFINE,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN,
    TOK_SLASH_ASSIGN,
    TOK_PERCENT_ASSIGN,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE,
    TOK_SHL,
    TOK_SHR,

    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_ANDAND,
    TOK_OROR,
    TOK_BANG,

    TOK_KIND_COUNT
} TokKind;

typedef struct Token {
    TokKind kind;
    Span span;
    Str text; /* the lexeme as written */
    union {
        uint64_t int_val; /* TOK_INT: magnitude; may be 2^63 so that -2^63 can be written */
        double float_val; /* TOK_FLOAT */
        Str str_val;      /* TOK_STRING, TOK_FSTR_TEXT: decoded bytes, arena-owned */
    } as;
} Token;

typedef Vec(Token) TokenVec;

/* Human-readable name for diagnostics: "`;`", "identifier", "integer literal". */
const char *tok_kind_name(TokKind kind);

#endif
