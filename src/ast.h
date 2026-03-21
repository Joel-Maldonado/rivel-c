#ifndef RIVEL_AST_H
#define RIVEL_AST_H

#include <stdbool.h>
#include <stdint.h>

#include "token.h"
#include "vec.h"

typedef enum {
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_STRUCT
} TypeKind;

typedef struct {
    TypeKind kind;
    StrSlice struct_name;
    const char *struct_name_cstr;
} Type;

const char *type_display_name(Type type);
bool type_equal(Type lhs, Type rhs);
bool type_is_numeric(Type type);
bool type_can_widen_to(Type source, Type target);

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;
typedef struct Decl Decl;
typedef struct Param Param;
typedef struct IfBranch IfBranch;
typedef struct StructFieldDecl StructFieldDecl;
typedef struct StructLiteralField StructLiteralField;

typedef struct ExprList {
    Vec storage;
} ExprList;

typedef struct StmtList {
    Vec storage;
} StmtList;

typedef struct IfBranchList {
    Vec storage;
} IfBranchList;

typedef struct ParamList {
    Vec storage;
} ParamList;

typedef struct StructFieldDeclList {
    Vec storage;
} StructFieldDeclList;

typedef struct StructLiteralFieldList {
    Vec storage;
} StructLiteralFieldList;

typedef struct DeclList {
    Vec storage;
} DeclList;

typedef enum {
    EXPR_INT,
    EXPR_DOUBLE,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_NAME,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL,
    EXPR_STRUCT_LITERAL,
    EXPR_FIELD
} ExprKind;

struct Expr {
    ExprKind kind;
    Token token;
    union {
        int64_t int_value;
        double double_value;
        bool bool_value;
        StrSlice string_value;
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
            ExprList args;
        } call;
        struct {
            StrSlice struct_name;
            StructLiteralFieldList fields;
        } struct_literal;
        struct {
            Expr *base;
            StrSlice name;
        } field;
    } as;
};

typedef enum {
    STMT_BINDING,
    STMT_ASSIGN,
    STMT_RETURN,
    STMT_CALL,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR_RANGE
} StmtKind;

struct Param {
    Token token;
    StrSlice name;
    Type type;
};

struct StructFieldDecl {
    Token token;
    StrSlice name;
    Type type;
};

struct StructLiteralField {
    Token token;
    StrSlice name;
    Expr *value;
};

struct IfBranch {
    Token token;
    Expr *condition;
    Block *body;
};

void expr_list_init(ExprList *list, Arena *arena);
size_t expr_list_len(const ExprList *list);
Expr **expr_list_push(ExprList *list, CompileError *error);
Expr *expr_list_get(const ExprList *list, size_t index);

void stmt_list_init(StmtList *list, Arena *arena);
size_t stmt_list_len(const StmtList *list);
Stmt **stmt_list_push(StmtList *list, CompileError *error);
Stmt *stmt_list_get(const StmtList *list, size_t index);

void if_branch_list_init(IfBranchList *list, Arena *arena);
size_t if_branch_list_len(const IfBranchList *list);
IfBranch *if_branch_list_push(IfBranchList *list, CompileError *error);
const IfBranch *if_branch_list_get_const(const IfBranchList *list, size_t index);
IfBranch *if_branch_list_get(const IfBranchList *list, size_t index);

void param_list_init(ParamList *list, Arena *arena);
size_t param_list_len(const ParamList *list);
Param *param_list_push(ParamList *list, CompileError *error);
Param *param_list_get(const ParamList *list, size_t index);
const Param *param_list_get_const(const ParamList *list, size_t index);

void struct_field_decl_list_init(StructFieldDeclList *list, Arena *arena);
size_t struct_field_decl_list_len(const StructFieldDeclList *list);
StructFieldDecl *struct_field_decl_list_push(StructFieldDeclList *list, CompileError *error);
StructFieldDecl *struct_field_decl_list_get(const StructFieldDeclList *list, size_t index);
const StructFieldDecl *struct_field_decl_list_get_const(const StructFieldDeclList *list, size_t index);

void struct_literal_field_list_init(StructLiteralFieldList *list, Arena *arena);
size_t struct_literal_field_list_len(const StructLiteralFieldList *list);
StructLiteralField *struct_literal_field_list_push(StructLiteralFieldList *list, CompileError *error);
StructLiteralField *struct_literal_field_list_get(const StructLiteralFieldList *list, size_t index);
const StructLiteralField *struct_literal_field_list_get_const(const StructLiteralFieldList *list, size_t index);

void decl_list_init(DeclList *list, Arena *arena);
size_t decl_list_len(const DeclList *list);
Decl **decl_list_push(DeclList *list, CompileError *error);
Decl *decl_list_get(const DeclList *list, size_t index);

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
            Expr *target;
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
            IfBranchList elif_branches;
            Block *else_block;
        } if_stmt;
        struct {
            Expr *condition;
            Block *body;
        } while_stmt;
        struct {
            StrSlice name;
            Expr *start;
            Expr *end;
            bool is_inclusive;
            Block *body;
        } for_range;
    } as;
};

struct Block {
    Token token;
    StmtList statements;
};

typedef enum {
    DECL_GLOBAL_CONST,
    DECL_FUNCTION,
    DECL_STRUCT
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
            ParamList params;
            Type return_type;
            Block *body;
        } function;
        struct {
            StructFieldDeclList fields;
        } struct_decl;
    } as;
};

typedef struct {
    DeclList decls;
} Program;

#endif
