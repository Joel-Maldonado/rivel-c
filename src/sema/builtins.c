#include "sema/builtins.h"

#include <stddef.h>

const BuiltinInfo BUILTIN_FUNCS[] = {
    {"print", BI_PRINT},
    {"println", BI_PRINTLN},
    {"eprintln", BI_EPRINTLN},
    {"len", BI_LEN},
    {"ord", BI_ORD},
    {"chr", BI_CHR},
    {"panic", BI_PANIC},
    {"assert", BI_ASSERT},
    {"exit", BI_EXIT},
    {"args", BI_ARGS},
    {"read_file", BI_READ_FILE},
    {"write_file", BI_WRITE_FILE},
    {"read_line", BI_READ_LINE},
    {"sqrt", BI_SQRT},
    {"abs", BI_ABS},
    {"min", BI_MIN},
    {"max", BI_MAX},
    {"wrapping_add", BI_WRAPPING_ADD},
    {"wrapping_sub", BI_WRAPPING_SUB},
    {"wrapping_mul", BI_WRAPPING_MUL},
    {NULL, BI_NONE},
};

const BuiltinInfo STR_METHODS[] = {
    {"contains", BI_STR_CONTAINS},   {"starts_with", BI_STR_STARTS_WITH},
    {"ends_with", BI_STR_ENDS_WITH}, {"find", BI_STR_FIND},
    {"split", BI_STR_SPLIT},         {"join", BI_STR_JOIN},
    {"trim", BI_STR_TRIM},           {"upper", BI_STR_UPPER},
    {"lower", BI_STR_LOWER},         {"replace", BI_STR_REPLACE},
    {"repeat", BI_STR_REPEAT},       {NULL, BI_NONE},
};

const BuiltinInfo LIST_METHODS[] = {
    {"append", BI_LIST_APPEND},       {"pop", BI_LIST_POP},     {"insert", BI_LIST_INSERT},
    {"remove_at", BI_LIST_REMOVE_AT}, {"clear", BI_LIST_CLEAR}, {"contains", BI_LIST_CONTAINS},
    {"index_of", BI_LIST_INDEX_OF},   {NULL, BI_NONE},
};

const char *builtin_name(Builtin b) {
    static const char *const NAMES[BI_COUNT] = {
        [BI_NONE] = "?",
        [BI_PRINT] = "print",
        [BI_PRINTLN] = "println",
        [BI_EPRINTLN] = "eprintln",
        [BI_LEN] = "len",
        [BI_STR] = "str",
        [BI_INT] = "int",
        [BI_FLOAT] = "float",
        [BI_ORD] = "ord",
        [BI_CHR] = "chr",
        [BI_PANIC] = "panic",
        [BI_ASSERT] = "assert",
        [BI_EXIT] = "exit",
        [BI_ARGS] = "args",
        [BI_READ_FILE] = "read_file",
        [BI_WRITE_FILE] = "write_file",
        [BI_READ_LINE] = "read_line",
        [BI_SQRT] = "sqrt",
        [BI_ABS] = "abs",
        [BI_MIN] = "min",
        [BI_MAX] = "max",
        [BI_WRAPPING_ADD] = "wrapping_add",
        [BI_WRAPPING_SUB] = "wrapping_sub",
        [BI_WRAPPING_MUL] = "wrapping_mul",
        [BI_STR_CONTAINS] = "contains",
        [BI_STR_STARTS_WITH] = "starts_with",
        [BI_STR_ENDS_WITH] = "ends_with",
        [BI_STR_FIND] = "find",
        [BI_STR_SPLIT] = "split",
        [BI_STR_JOIN] = "join",
        [BI_STR_TRIM] = "trim",
        [BI_STR_UPPER] = "upper",
        [BI_STR_LOWER] = "lower",
        [BI_STR_REPLACE] = "replace",
        [BI_STR_REPEAT] = "repeat",
        [BI_LIST_APPEND] = "append",
        [BI_LIST_POP] = "pop",
        [BI_LIST_INSERT] = "insert",
        [BI_LIST_REMOVE_AT] = "remove_at",
        [BI_LIST_CLEAR] = "clear",
        [BI_LIST_CONTAINS] = "contains",
        [BI_LIST_INDEX_OF] = "index_of",
    };
    return b < BI_COUNT ? NAMES[b] : "?";
}

Builtin builtin_lookup(const BuiltinInfo *table, Str name) {
    for (const BuiltinInfo *b = table; b->name != NULL; b++) {
        if (str_eq_c(name, b->name)) {
            return b->id;
        }
    }
    return BI_NONE;
}
