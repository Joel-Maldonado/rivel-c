#ifndef RIVEL_MAP_H
#define RIVEL_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "slice.h"

typedef struct {
    StrSlice key;
    uintptr_t value;
    bool occupied;
} StringMapEntry;

typedef struct {
    StringMapEntry *entries;
    size_t count;
    size_t cap;
} StringMap;

void strmap_init(StringMap *map);
void strmap_free(StringMap *map);
bool strmap_get(const StringMap *map, StrSlice key, uintptr_t *out_value);
bool strmap_contains(const StringMap *map, StrSlice key);
bool strmap_set(StringMap *map, StrSlice key, uintptr_t value, bool replace, CompileError *error);

#endif
