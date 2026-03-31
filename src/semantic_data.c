#include "semantic_internal.h"

void semantic_table_init(SemanticTable *table, size_t entry_size, Arena *arena) {
    vec_init(&table->entries, entry_size, arena);
    strmap_init(&table->names);
}

void semantic_table_free(SemanticTable *table) {
    strmap_free(&table->names);
}

size_t semantic_table_len(const SemanticTable *table) {
    return table->entries.len;
}

void *semantic_table_add(SemanticTable *table, StrSlice name, CompileError *error) {
    void *entry = vec_push(&table->entries, error);

    if (entry == NULL) {
        return NULL;
    }
    if (!strmap_set(&table->names, name, (uintptr_t)table->entries.len, false, error)) {
        return NULL;
    }
    return entry;
}

void *semantic_table_get(const SemanticTable *table, size_t index) {
    return vec_get(&table->entries, index);
}

bool semantic_table_contains(const SemanticTable *table, StrSlice name) {
    return strmap_contains(&table->names, name);
}

void *semantic_table_lookup(const SemanticTable *table, StrSlice name) {
    uintptr_t value;

    if (!strmap_get(&table->names, name, &value)) {
        return NULL;
    }
    return vec_get(&table->entries, (size_t)(value - 1U));
}
