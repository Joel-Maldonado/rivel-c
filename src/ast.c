#include "ast.h"

#define DEFINE_AST_LIST_FUNCS(ListType, ValueType, push_name, get_name, get_const_name) \
    void push_name##_init(ListType *list, Arena *arena) { \
        vec_init(&list->storage, sizeof(ValueType), arena); \
    } \
    size_t push_name##_len(const ListType *list) { \
        return list->storage.len; \
    } \
    ValueType *push_name##_push(ListType *list, CompileError *error) { \
        return (ValueType *)vec_push(&list->storage, error); \
    } \
    ValueType *get_name(const ListType *list, size_t index) { \
        return (ValueType *)vec_get(&list->storage, index); \
    } \
    const ValueType *get_const_name(const ListType *list, size_t index) { \
        return (const ValueType *)vec_get(&list->storage, index); \
    }

static void expr_list_storage_init(ExprList *list, Arena *arena) {
    vec_init(&list->storage, sizeof(Expr *), arena);
}

static size_t expr_list_storage_len(const ExprList *list) {
    return list->storage.len;
}

static Expr **expr_list_storage_push(ExprList *list, CompileError *error) {
    return (Expr **)vec_push(&list->storage, error);
}

static Expr **expr_list_storage_get(const ExprList *list, size_t index) {
    return (Expr **)vec_get(&list->storage, index);
}

static void stmt_list_storage_init(StmtList *list, Arena *arena) {
    vec_init(&list->storage, sizeof(Stmt *), arena);
}

static size_t stmt_list_storage_len(const StmtList *list) {
    return list->storage.len;
}

static Stmt **stmt_list_storage_push(StmtList *list, CompileError *error) {
    return (Stmt **)vec_push(&list->storage, error);
}

static Stmt **stmt_list_storage_get(const StmtList *list, size_t index) {
    return (Stmt **)vec_get(&list->storage, index);
}

static void decl_list_storage_init(DeclList *list, Arena *arena) {
    vec_init(&list->storage, sizeof(Decl *), arena);
}

static size_t decl_list_storage_len(const DeclList *list) {
    return list->storage.len;
}

static Decl **decl_list_storage_push(DeclList *list, CompileError *error) {
    return (Decl **)vec_push(&list->storage, error);
}

static Decl **decl_list_storage_get(const DeclList *list, size_t index) {
    return (Decl **)vec_get(&list->storage, index);
}

DEFINE_AST_LIST_FUNCS(IfBranchList, IfBranch, if_branch_list, if_branch_list_get, if_branch_list_get_const)
DEFINE_AST_LIST_FUNCS(ParamList, Param, param_list, param_list_get, param_list_get_const)

const char *type_display_name(Type type) {
    switch (type.kind) {
        case TYPE_INT:
            return "Int";
        case TYPE_BOOL:
            return "Bool";
    }

    return "<type>";
}

bool type_equal(Type lhs, Type rhs) {
    return lhs.kind == rhs.kind;
}

void expr_list_init(ExprList *list, Arena *arena) {
    expr_list_storage_init(list, arena);
}

size_t expr_list_len(const ExprList *list) {
    return expr_list_storage_len(list);
}

Expr **expr_list_push(ExprList *list, CompileError *error) {
    return expr_list_storage_push(list, error);
}

Expr *expr_list_get(const ExprList *list, size_t index) {
    return *expr_list_storage_get(list, index);
}

void stmt_list_init(StmtList *list, Arena *arena) {
    stmt_list_storage_init(list, arena);
}

size_t stmt_list_len(const StmtList *list) {
    return stmt_list_storage_len(list);
}

Stmt **stmt_list_push(StmtList *list, CompileError *error) {
    return stmt_list_storage_push(list, error);
}

Stmt *stmt_list_get(const StmtList *list, size_t index) {
    return *stmt_list_storage_get(list, index);
}

void decl_list_init(DeclList *list, Arena *arena) {
    decl_list_storage_init(list, arena);
}

size_t decl_list_len(const DeclList *list) {
    return decl_list_storage_len(list);
}

Decl **decl_list_push(DeclList *list, CompileError *error) {
    return decl_list_storage_push(list, error);
}

Decl *decl_list_get(const DeclList *list, size_t index) {
    return *decl_list_storage_get(list, index);
}
