#include "ast.h"

#include <stdio.h>

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

#define DEFINE_AST_PTR_LIST_FUNCS(ListType, ElemType, prefix) \
    void prefix##_init(ListType *list, Arena *arena) { \
        vec_init(&list->storage, sizeof(ElemType *), arena); \
    } \
    size_t prefix##_len(const ListType *list) { \
        return list->storage.len; \
    } \
    ElemType **prefix##_push(ListType *list, CompileError *error) { \
        return (ElemType **)vec_push(&list->storage, error); \
    } \
    ElemType *prefix##_get(const ListType *list, size_t index) { \
        return *(ElemType **)vec_get(&list->storage, index); \
    }

DEFINE_AST_LIST_FUNCS(IfBranchList, IfBranch, if_branch_list, if_branch_list_get, if_branch_list_get_const)
DEFINE_AST_LIST_FUNCS(ParamList, Param, param_list, param_list_get, param_list_get_const)
DEFINE_AST_LIST_FUNCS(StructFieldDeclList, StructFieldDecl, struct_field_decl_list, struct_field_decl_list_get, struct_field_decl_list_get_const)
DEFINE_AST_LIST_FUNCS(StructLiteralFieldList, StructLiteralField, struct_literal_field_list, struct_literal_field_list_get, struct_literal_field_list_get_const)

static Type type_make_simple(TypeKind kind) {
    Type type;

    type.kind = kind;
    type.struct_name = slice_from_parts(NULL, 0U);
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

Type type_make_struct(StrSlice struct_name) {
    Type type;

    type.kind = TYPE_STRUCT;
    type.struct_name = struct_name;
    return type;
}

const char *type_display_name(Type type) {
    static char buf[256];

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
            if (type.struct_name.data == NULL) {
                return "<struct>";
            }
            snprintf(buf, sizeof(buf), "%.*s", (int)type.struct_name.len, type.struct_name.data);
            return buf;
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
    ConstValue out = {0};

    out.type = type_make_int();
    out.as.int_value = value;
    return out;
}

ConstValue const_value_make_double(double value) {
    ConstValue out = {0};

    out.type = type_make_double();
    out.as.double_value = value;
    return out;
}

ConstValue const_value_make_bool(bool value) {
    ConstValue out = {0};

    out.type = type_make_bool();
    out.as.bool_value = value;
    return out;
}

ConstValue const_value_make_string(StrSlice value) {
    ConstValue out = {0};

    out.type = type_make_string();
    out.as.string_value = value;
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

DEFINE_AST_PTR_LIST_FUNCS(ExprList, Expr, expr_list)
DEFINE_AST_PTR_LIST_FUNCS(StmtList, Stmt, stmt_list)
DEFINE_AST_PTR_LIST_FUNCS(DeclList, Decl, decl_list)
