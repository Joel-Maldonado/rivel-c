#include "sema/types.h"

#include "sema/sema.h"

static Type *ty_new(TypeTable *tt, TypeKind kind, const char *name) {
    Type *t = arena_new(tt->arena, Type);
    t->kind = kind;
    t->name = name;
    vec_push(&tt->all, t);
    return t;
}

void types_init(TypeTable *tt, Arena *arena) {
    memset(tt, 0, sizeof *tt);
    tt->arena = arena;
    tt->t_error = ty_new(tt, TY_ERROR, "<error>");
    tt->t_void = ty_new(tt, TY_VOID, "void");
    tt->t_null = ty_new(tt, TY_NULL, "null");
    tt->t_int = ty_new(tt, TY_INT, "int");
    tt->t_float = ty_new(tt, TY_FLOAT, "float");
    tt->t_bool = ty_new(tt, TY_BOOL, "bool");
    tt->t_str = ty_new(tt, TY_STR, "str");
}

void types_free(TypeTable *tt) {
    vec_free(&tt->all);
}

static Type *ty_find(TypeTable *tt, TypeKind kind, Type *elem, Symbol *st) {
    for (size_t i = 0; i < tt->all.len; i++) {
        Type *t = tt->all.data[i];
        if (t->kind == kind && t->elem == elem && t->st == st) {
            return t;
        }
    }
    return NULL;
}

Type *ty_list(TypeTable *tt, Type *elem) {
    Type *t = ty_find(tt, TY_LIST, elem, NULL);
    if (t == NULL) {
        t = ty_new(tt, TY_LIST, arena_printf(tt->arena, "list[%s]", elem->name));
        t->elem = elem;
    }
    return t;
}

Type *ty_optional(TypeTable *tt, Type *inner) {
    Type *t;
    if (inner->kind == TY_OPTIONAL || inner->kind == TY_ERROR) {
        return inner;
    }
    t = ty_find(tt, TY_OPTIONAL, inner, NULL);
    if (t == NULL) {
        t = ty_new(tt, TY_OPTIONAL, arena_printf(tt->arena, "%s?", inner->name));
        t->elem = inner;
    }
    return t;
}

Type *ty_struct(TypeTable *tt, Symbol *st) {
    Type *t = ty_find(tt, TY_STRUCT, NULL, st);
    if (t == NULL) {
        t = ty_new(tt, TY_STRUCT, arena_strndup(tt->arena, st->name.p, st->name.n));
        t->st = st;
    }
    return t;
}

bool ty_convertible(const Type *from, const Type *to) {
    if (from == to || from->kind == TY_ERROR || to->kind == TY_ERROR) {
        return true;
    }
    if (from->kind == TY_INT && to->kind == TY_FLOAT) {
        return true;
    }
    if (to->kind == TY_OPTIONAL) {
        return from->kind == TY_NULL || ty_convertible(from, to->elem);
    }
    return false;
}
