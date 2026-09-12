#ifndef RV_SEMA_TYPES_H
#define RV_SEMA_TYPES_H

#include <stdbool.h>

#include "base/arena.h"
#include "base/vec.h"

#include "ast/ast.h"

typedef enum TypeKind {
    TY_ERROR, /* poison: produced by a reported error, converts to and from anything silently */
    TY_VOID,
    TY_NULL, /* the type of a bare `null` before context gives it an optional type */
    TY_INT,
    TY_FLOAT,
    TY_BOOL,
    TY_STR,
    TY_LIST,
    TY_OPTIONAL,
    TY_STRUCT,
} TypeKind;

/* Types are interned: two types are equal iff their pointers are equal. */
struct Type {
    TypeKind kind;
    Type *elem;       /* TY_LIST element, TY_OPTIONAL inner */
    Symbol *st;       /* TY_STRUCT */
    const char *name; /* display form, e.g. "list[int]", "Point?" */
};

typedef Vec(Type *) TypeVec;

typedef struct TypeTable {
    Arena *arena;
    TypeVec all;
    Type *t_error;
    Type *t_void;
    Type *t_null;
    Type *t_int;
    Type *t_float;
    Type *t_bool;
    Type *t_str;
} TypeTable;

void types_init(TypeTable *tt, Arena *arena);
void types_free(TypeTable *tt);
Type *ty_list(TypeTable *tt, Type *elem);
Type *ty_optional(TypeTable *tt, Type *inner);
Type *ty_struct(TypeTable *tt, Symbol *st);

static inline bool ty_is_numeric(const Type *t) {
    return t->kind == TY_INT || t->kind == TY_FLOAT;
}

/* Values of these types are pointers at run time. */
static inline bool ty_is_ref(const Type *t) {
    return t->kind == TY_STR || t->kind == TY_LIST || t->kind == TY_OPTIONAL || t->kind == TY_STRUCT;
}

/* Types accepted by str(), print, and f-string holes. */
static inline bool ty_is_printable(const Type *t) {
    return t->kind == TY_INT || t->kind == TY_FLOAT || t->kind == TY_BOOL || t->kind == TY_STR;
}

/* Implicit conversion: identity, int to float, T to T?, null to T?. */
bool ty_convertible(const Type *from, const Type *to);

#endif
