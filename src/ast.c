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
DEFINE_AST_LIST_FUNCS(StructFieldDeclList, StructFieldDecl, struct_field_decl_list, struct_field_decl_list_get, struct_field_decl_list_get_const)
DEFINE_AST_LIST_FUNCS(StructLiteralFieldList, StructLiteralField, struct_literal_field_list, struct_literal_field_list_get, struct_literal_field_list_get_const)

static Type type_make_simple(TypeKind kind) {
    Type type;

    type.kind = kind;
    type.struct_name = slice_from_parts(NULL, 0U);
    type.struct_name_cstr = NULL;
    return type;
}

Type type_make_int(void) {
    return type_make_simple(TYPE_INT);
}

Type type_make_double(void) {
    return type_make_simple(TYPE_DOUBLE);
}

Type type_make_bool(void) {
    return type_make_simple(TYPE_BOOL);
}

Type type_make_string(void) {
    return type_make_simple(TYPE_STRING);
}

Type type_make_struct(StrSlice struct_name, const char *struct_name_cstr) {
    Type type;

    type.kind = TYPE_STRUCT;
    type.struct_name = struct_name;
    type.struct_name_cstr = struct_name_cstr;
    return type;
}

const char *type_display_name(Type type) {
    switch (type.kind) {
        case TYPE_INT:
            return "Int";
        case TYPE_DOUBLE:
            return "Double";
        case TYPE_BOOL:
            return "Bool";
        case TYPE_STRING:
            return "String";
        case TYPE_STRUCT:
            return type.struct_name_cstr != NULL ? type.struct_name_cstr : "<struct>";
    }

    return "<type>";
}

bool type_equal(Type lhs, Type rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }
    if (lhs.kind != TYPE_STRUCT) {
        return true;
    }
    return slice_equal(lhs.struct_name, rhs.struct_name);
}

bool type_is_numeric(Type type) {
    return type.kind == TYPE_INT || type.kind == TYPE_DOUBLE;
}

bool type_can_widen_to(Type source, Type target) {
    if (type_equal(source, target)) {
        return true;
    }
    return source.kind == TYPE_INT && target.kind == TYPE_DOUBLE;
}

ConstValue const_value_make_int(int64_t value) {
    ConstValue out;

    out.type = type_make_int();
    out.int_value = value;
    out.double_value = 0.0;
    out.bool_value = false;
    out.string_value = slice_from_parts(NULL, 0U);
    return out;
}

ConstValue const_value_make_double(double value) {
    ConstValue out;

    out.type = type_make_double();
    out.int_value = 0;
    out.double_value = value;
    out.bool_value = false;
    out.string_value = slice_from_parts(NULL, 0U);
    return out;
}

ConstValue const_value_make_bool(bool value) {
    ConstValue out;

    out.type = type_make_bool();
    out.int_value = 0;
    out.double_value = 0.0;
    out.bool_value = value;
    out.string_value = slice_from_parts(NULL, 0U);
    return out;
}

ConstValue const_value_make_string(StrSlice value) {
    ConstValue out;

    out.type = type_make_string();
    out.int_value = 0;
    out.double_value = 0.0;
    out.bool_value = false;
    out.string_value = value;
    return out;
}

void expr_set_resolved_type(Expr *expr, Type type) {
    expr->semantics.has_type = true;
    expr->semantics.type = type;
}

bool expr_resolved_type(const Expr *expr, Type *out_type) {
    if (!expr->semantics.has_type) {
        return false;
    }
    if (out_type != NULL) {
        *out_type = expr->semantics.type;
    }
    return true;
}

void expr_set_const_value(Expr *expr, ConstValue value) {
    expr->semantics.has_const_value = true;
    expr->semantics.const_value = value;
}

bool expr_const_value(const Expr *expr, ConstValue *out_value) {
    if (!expr->semantics.has_const_value) {
        return false;
    }
    if (out_value != NULL) {
        *out_value = expr->semantics.const_value;
    }
    return true;
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
