#ifndef RV_SEMA_SEMA_H
#define RV_SEMA_SEMA_H

#include <stdbool.h>
#include <stdint.h>

#include "base/arena.h"
#include "base/diag.h"
#include "base/strmap.h"
#include "base/vec.h"

#include "ast/ast.h"
#include "sema/builtins.h"
#include "sema/types.h"

typedef enum SymKind {
    SYM_TYPE,    /* int, float, bool, str, list, void */
    SYM_BUILTIN, /* a global builtin function */
    SYM_FUNC,
    SYM_METHOD,
    SYM_STRUCT,
    SYM_GLOBAL,
    SYM_PARAM,
    SYM_LOCAL,
} SymKind;

typedef struct FieldInfo {
    Str name;
    Span span;
    Type *type;
    int index;
} FieldInfo;

typedef Vec(FieldInfo) FieldInfoVec;

/* A folded global initializer. */
typedef struct ConstValue {
    Type *type; /* int, float, bool, str, or an optional holding null */
    bool is_null;
    union {
        int64_t i;
        double f;
        bool b;
        Str s;
    } as;
} ConstValue;

typedef Vec(Symbol *) SymVec;

struct Symbol {
    SymKind kind;
    Str name;
    Span span;
    Type *type;          /* TYPE: the type; variables: their type; functions: return type; STRUCT: the struct type */
    const char *mangled; /* backend symbol name for functions, globals, and structs */
    Builtin builtin;     /* SYM_BUILTIN */

    /* SYM_FUNC, SYM_METHOD */
    FuncDecl *func;
    TypeVec params; /* parameter types, self included for methods */
    Symbol *owner;  /* SYM_METHOD: the struct */
    int nlocals;    /* parameters and locals declared in the body */

    /* SYM_STRUCT */
    StructDecl *st;
    FieldInfoVec fields;
    StrMap members; /* field name -> FieldInfo *, method name -> Symbol * (distinguished by kind tag below) */
    StrMap methods; /* method name -> Symbol * */

    /* SYM_GLOBAL */
    GlobalDecl *global;
    ConstValue value;
    bool folded;

    /* SYM_PARAM, SYM_LOCAL */
    int slot; /* index within the enclosing function */
    bool immutable;
    bool used;
};

/* Everything the lowering pass needs, produced by sema_check. */
typedef struct Program {
    Module *module;
    TypeTable types;
    SymVec funcs;   /* free functions and methods, in declaration order */
    SymVec structs; /* in declaration order */
    SymVec globals; /* in declaration order */
    Symbol *main_fn;
} Program;

/* Runs name resolution, type checking, and constant folding. Returns false when diags has errors. */
bool sema_check(Module *m, Arena *arena, Diags *diags, Program *out);
void program_free(Program *p);

#endif
