#include "semantic_internal.h"

#define DEFINE_SEMANTIC_TABLE_FUNCTIONS(TableType, EntryType, prefix) \
    void prefix##_init(TableType *table, Arena *arena) { \
        vec_init(&table->storage, sizeof(EntryType), arena); \
    } \
    size_t prefix##_len(const TableType *table) { \
        return table->storage.len; \
    } \
    EntryType *prefix##_push(TableType *table, CompileError *error) { \
        return (EntryType *)vec_push(&table->storage, error); \
    } \
    EntryType *prefix##_get(TableType *table, size_t index) { \
        return (EntryType *)vec_get(&table->storage, index); \
    } \
    const EntryType *prefix##_get_const(const TableType *table, size_t index) { \
        return (const EntryType *)vec_get(&table->storage, index); \
    }

DEFINE_SEMANTIC_TABLE_FUNCTIONS(SemanticGlobalTable, SemanticGlobalRecord, semantic_global_table)
DEFINE_SEMANTIC_TABLE_FUNCTIONS(SemanticFunctionTable, SemanticFunctionInfo, semantic_function_table)
DEFINE_SEMANTIC_TABLE_FUNCTIONS(SemanticStructTable, SemanticStructInfo, semantic_struct_table)
DEFINE_SEMANTIC_TABLE_FUNCTIONS(SemanticBuiltinTable, SemanticBuiltinInfo, semantic_builtin_table)

void semantic_symbol_table_init(SemanticSymbolTable *table) {
    strmap_init(&table->storage);
}

void semantic_symbol_table_free(SemanticSymbolTable *table) {
    strmap_free(&table->storage);
}

bool semantic_symbol_table_contains(const SemanticSymbolTable *table, StrSlice name) {
    return strmap_contains(&table->storage, name);
}

bool semantic_symbol_table_set(SemanticSymbolTable *table, StrSlice name, size_t index, CompileError *error) {
    return strmap_set(&table->storage, name, semantic_index_value(index), false, error);
}

bool semantic_symbol_table_get(const SemanticSymbolTable *table, StrSlice name, size_t *out_index) {
    uintptr_t value;

    if (!strmap_get(&table->storage, name, &value)) {
        return false;
    }
    if (out_index != NULL) {
        *out_index = semantic_map_index(value);
    }
    return true;
}

uintptr_t semantic_index_value(size_t index) {
    return (uintptr_t)(index + 1U);
}

size_t semantic_map_index(uintptr_t value) {
    return (size_t)(value - 1U);
}

bool semantic_record_expr_type(SemanticResult *result, const Expr *expr, Type type, CompileError *error) {
    (void)result;
    (void)error;
    expr_set_resolved_type((Expr *)expr, type);
    return true;
}

bool semantic_record_expr_const(SemanticResult *result, const Expr *expr, ConstValue value, CompileError *error) {
    (void)result;
    (void)error;
    expr_set_const_value((Expr *)expr, value);
    return true;
}

bool semantic_lookup_recorded_expr_type(const SemanticResult *result, const Expr *expr, Type *out_type) {
    (void)result;
    return expr_resolved_type(expr, out_type);
}

bool semantic_lookup_recorded_expr_const(const SemanticResult *result, const Expr *expr, ConstValue *out_value) {
    (void)result;
    return expr_const_value(expr, out_value);
}
