#ifndef RV_BASE_STRMAP_H
#define RV_BASE_STRMAP_H

#include <stdbool.h>
#include <stddef.h>

#include "base/str.h"

/*
 * Open-addressing hash map from Str to void *. Keys are borrowed: the bytes
 * must outlive the map. A NULL value is indistinguishable from absence in
 * strmap_get; use strmap_has when NULL is a legal value.
 */
typedef struct StrMapEntry {
    Str key;
    void *value;
    bool used;
} StrMapEntry;

typedef struct StrMap {
    StrMapEntry *entries;
    size_t count;
    size_t cap;
} StrMap;

void strmap_init(StrMap *m);
void strmap_free(StrMap *m);
void *strmap_get(const StrMap *m, Str key);
bool strmap_has(const StrMap *m, Str key);
/* Returns the previous value, or NULL. */
void *strmap_put(StrMap *m, Str key, void *value);

#endif
