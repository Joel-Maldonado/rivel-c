#ifndef RIVEL_AST_H
#define RIVEL_AST_H

#include <stdbool.h>
#include <stdint.h>

#include "token.h"
#include "vec.h"

typedef enum {
    TYPE_INT,
    TYPE_BOOL
} TypeKind;

typedef struct {
    TypeKind kind;
} Type;

const char *type_display_name(Type type);
bool type_equal(Type lhs, Type rhs);

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;
typedef struct Decl Decl;

typedef enum {
    EXPR_INT,
    EXPR_BOOL,
    EXPR_NAME,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL
} ExprKind;

struct Expr {
    ExprKind kind;
    Token token;
    bool has_inferred_type;
    Type inferred_type;
    union {
        int64_t int_value;
        bool bool_value;
        StrSlice name;
        struct {
            TokenType op;
            Expr *operand;
        } unary;
        struct {
            TokenType op;
            Expr *lhs;
            Expr *rhs;
        } binary;
        struct {
            StrSlice callee;
            Vec args;
        } call;
    } as;
};

typedef enum {
    STMT_BINDING,
    STMT_ASSIGN,
    STMT_RETURN,
    STMT_CALL,
    STMT_IF,
    STMT_WHILE
} StmtKind;

typedef struct {
    Token token;
    StrSlice name;
    Type type;
} Param;

typedef struct {
    Token token;
    Expr *condition;
    Block *body;
} IfBranch;

struct Stmt {
    StmtKind kind;
    Token token;
    union {
        struct {
            bool is_mutable;
            StrSlice name;
            bool has_annotation;
            Type annotation;
            Expr *initializer;
        } binding;
        struct {
            StrSlice name;
            Expr *value;
        } assign;
        struct {
            Expr *value;
        } ret;
        struct {
            Expr *call;
        } call;
        struct {
            Expr *condition;
            Block *then_block;
            Vec elif_branches;
            Block *else_block;
        } if_stmt;
        struct {
            Expr *condition;
            Block *body;
        } while_stmt;
    } as;
};

struct Block {
    Token token;
    Vec statements;
};

typedef enum {
    DECL_GLOBAL_CONST,
    DECL_FUNCTION
} DeclKind;

struct Decl {
    DeclKind kind;
    Token token;
    StrSlice name;
    union {
        struct {
            bool has_annotation;
            Type annotation;
            Expr *initializer;
        } global_const;
        struct {
            Vec params;
            Type return_type;
            Block *body;
        } function;
    } as;
};

typedef struct {
    Vec decls;
} Program;

#endif
