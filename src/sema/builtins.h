#ifndef RV_SEMA_BUILTINS_H
#define RV_SEMA_BUILTINS_H

#include "base/str.h"

typedef enum Builtin {
    BI_NONE,

    /* global functions */
    BI_PRINT,
    BI_PRINTLN,
    BI_EPRINTLN,
    BI_LEN,
    BI_STR,
    BI_INT,
    BI_FLOAT,
    BI_ORD,
    BI_CHR,
    BI_PANIC,
    BI_ASSERT,
    BI_EXIT,
    BI_ARGS,
    BI_READ_FILE,
    BI_WRITE_FILE,
    BI_READ_LINE,
    BI_SQRT,
    BI_ABS,
    BI_MIN,
    BI_MAX,
    BI_WRAPPING_ADD,
    BI_WRAPPING_SUB,
    BI_WRAPPING_MUL,

    /* str methods */
    BI_STR_CONTAINS,
    BI_STR_STARTS_WITH,
    BI_STR_ENDS_WITH,
    BI_STR_FIND,
    BI_STR_SPLIT,
    BI_STR_JOIN,
    BI_STR_TRIM,
    BI_STR_UPPER,
    BI_STR_LOWER,
    BI_STR_REPLACE,
    BI_STR_REPEAT,

    /* list methods */
    BI_LIST_APPEND,
    BI_LIST_POP,
    BI_LIST_INSERT,
    BI_LIST_REMOVE_AT,
    BI_LIST_CLEAR,
    BI_LIST_CONTAINS,
    BI_LIST_INDEX_OF,

    BI_COUNT
} Builtin;

typedef struct BuiltinInfo {
    const char *name;
    Builtin id;
} BuiltinInfo;

/* Global builtin functions, terminated by a NULL name. */
extern const BuiltinInfo BUILTIN_FUNCS[];
extern const BuiltinInfo STR_METHODS[];
extern const BuiltinInfo LIST_METHODS[];

const char *builtin_name(Builtin b);
Builtin builtin_lookup(const BuiltinInfo *table, Str name);

#endif
