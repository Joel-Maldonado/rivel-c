#ifndef RV_AST_AST_H
#define RV_AST_AST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "base/source.h"
#include "base/str.h"
#include "base/vec.h"

#include "lex/token.h"

/*
 * Syntax tree produced by the parser and annotated in place by the checker.
 * Every node carries a Span. Fields marked "sema:" are NULL or zero until
 * the checker fills them; the lowering pass reads them and never re-resolves.
 */

typedef struct Type Type;     /* sema/types.h */
typedef struct Symbol Symbol; /* sema/symbols.h */

typedef struct TypeExpr TypeExpr;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;
typedef struct Decl Decl;
typedef struct FuncDecl FuncDecl;
typedef struct StructDecl StructDecl;

typedef Vec(Expr *) ExprVec;
typedef Vec(Stmt *) StmtVec;
typedef Vec(Decl *) DeclVec;

/* ---- types as written --------------------------------------------------- */

typedef enum TypeExprKind {
    TYPEX_NAME,     /* int, float, bool, str, void, or a struct name */
    TYPEX_LIST,     /* list[inner] */
    TYPEX_OPTIONAL, /* inner? */
} TypeExprKind;

struct TypeExpr {
    TypeExprKind kind;
    Span span;
    Str name;        /* TYPEX_NAME */
    TypeExpr *inner; /* TYPEX_LIST, TYPEX_OPTIONAL */
};

/* ---- expressions ------------------------------------------------------- */

typedef enum ExprKind {
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_NULL,
    EXPR_NAME,    /* identifier or self */
    EXPR_FSTRING, /* parts: EXPR_STRING for literal text, any expression for holes */
    EXPR_LIST,    /* [a, b, c] */
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL,  /* callee(args); construction, function, method, and builtin calls all start here */
    EXPR_FIELD, /* base.name */
    EXPR_INDEX, /* base[index] */
    EXPR_SLICE, /* base[lo..hi] */
    EXPR_RANGE, /* lo..hi or lo..=hi; only valid as a for-loop iterable */
} ExprKind;

typedef struct Arg {
    Str label; /* empty for positional */
    Span label_span;
    Expr *value;
    int field_index; /* sema: for struct construction, the field this label names */
} Arg;

typedef Vec(Arg) ArgVec;

/* sema: how a call resolved */
typedef enum CallKind {
    CALL_UNRESOLVED,
    CALL_FUNC,      /* free function: callee_sym */
    CALL_METHOD,    /* struct method: callee_sym, self is callee->as.field.base */
    CALL_CONSTRUCT, /* struct construction: struct_sym */
    CALL_BUILTIN,   /* builtin function: builtin */
    CALL_STR_METHOD,
    CALL_LIST_METHOD,
} CallKind;

struct Expr {
    ExprKind kind;
    Span span;
    Type *type; /* sema: never NULL after checking; TY_ERROR on failure */
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        Str str_val;
        struct {
            Str name;
            Symbol *sym; /* sema */
        } name;
        struct {
            ExprVec parts;
        } fstring;
        struct {
            ExprVec items;
        } list;
        struct {
            TokKind op;
            Expr *operand;
        } unary;
        struct {
            TokKind op;
            Expr *lhs;
            Expr *rhs;
        } binary;
        struct {
            Expr *callee; /* EXPR_NAME or EXPR_FIELD */
            ArgVec args;
            CallKind call_kind; /* sema */
            Symbol *callee_sym; /* sema: function, method, or struct */
            int builtin;        /* sema: BUILTIN_* for CALL_BUILTIN and the method kinds */
        } call;
        struct {
            Expr *base;
            Str name;
            Span name_span;
            int field_index; /* sema */
        } field;
        struct {
            Expr *base;
            Expr *index;
        } index;
        struct {
            Expr *base;
            Expr *lo;
            Expr *hi;
        } slice;
        struct {
            Expr *lo;
            Expr *hi;
            bool inclusive;
        } range;
    } as;
};

/* ---- statements -------------------------------------------------------- */

typedef enum StmtKind {
    STMT_VAR,
    STMT_ASSIGN,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_RETURN,
    STMT_BLOCK,
} StmtKind;

struct Block {
    Span span;
    StmtVec stmts;
};

struct Stmt {
    StmtKind kind;
    Span span;
    union {
        struct {
            Str name;
            Span name_span;
            TypeExpr *type; /* NULL for := */
            Expr *init;
            Symbol *sym; /* sema */
        } var;
        struct {
            Expr *target;
            TokKind op; /* TOK_ASSIGN or a compound assignment token */
            Expr *value;
        } assign;
        struct {
            Expr *expr;
        } expr;
        struct {
            Expr *cond;
            Block *then_block;
            Stmt *else_stmt; /* NULL, STMT_BLOCK, or STMT_IF */
        } if_stmt;
        struct {
            Expr *cond;
            Block *body;
        } while_stmt;
        struct {
            Str name;
            Span name_span;
            Expr *iter; /* EXPR_RANGE, or an expression of list or str type */
            Block *body;
            Symbol *sym; /* sema */
        } for_stmt;
        struct {
            Expr *value; /* NULL for a bare return */
        } ret;
        Block *block;
    } as;
};

/* ---- declarations ------------------------------------------------------ */

typedef struct Param {
    Str name;
    Span span;
    TypeExpr *type; /* NULL for self */
    Symbol *sym;    /* sema */
} Param;

typedef Vec(Param) ParamVec;

struct FuncDecl {
    Str name;
    Span name_span;
    Span span;
    ParamVec params; /* for methods, params[0] is self */
    TypeExpr *ret;   /* NULL for void */
    Block *body;
    StructDecl *owner; /* NULL for free functions */
    Symbol *sym;       /* sema */
};

typedef Vec(FuncDecl *) FuncDeclVec;

typedef struct FieldDecl {
    Str name;
    Span span;
    TypeExpr *type;
} FieldDecl;

typedef Vec(FieldDecl) FieldDeclVec;

struct StructDecl {
    Str name;
    Span name_span;
    Span span;
    FieldDeclVec fields;
    FuncDeclVec methods;
    Symbol *sym; /* sema */
};

typedef struct GlobalDecl {
    Str name;
    Span name_span;
    Span span;
    TypeExpr *type; /* NULL for := */
    Expr *init;
    Symbol *sym; /* sema */
} GlobalDecl;

typedef enum DeclKind {
    DECL_FUNC,
    DECL_STRUCT,
    DECL_GLOBAL,
} DeclKind;

struct Decl {
    DeclKind kind;
    Span span;
    union {
        FuncDecl *func;
        StructDecl *st;
        GlobalDecl *global;
    } as;
};

typedef struct Module {
    const Source *src;
    DeclVec decls;
} Module;

/* Prints the tree in an indented S-expression style, for --dump-ast and tests. */
void ast_dump(const Module *m, FILE *out);

#endif
