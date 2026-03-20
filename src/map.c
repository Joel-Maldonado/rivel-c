#include "map.h"

#include <stdlib.h>
#include <string.h>

static bool strmap_should_grow(const StringMap *map) {
    if (map->cap == 0U) {
        return true;
    }
    return (map->count + 1U) * 10U >= map->cap * 7U;
}

static size_t strmap_index_for(size_t cap, StrSlice key) {
    return (size_t)(slice_hash(key) % cap);
}

static bool strmap_insert_entry(StringMapEntry *entries, size_t cap, StrSlice key, uintptr_t value, bool replace) {
    size_t index = strmap_index_for(cap, key);

    while (entries[index].occupied) {
        if (slice_equal(entries[index].key, key)) {
            if (replace) {
                entries[index].value = value;
            }
            return replace;
        }
        index = (index + 1U) % cap;
    }

    entries[index].occupied = true;
    entries[index].key = key;
    entries[index].value = value;
    return true;
}

static bool strmap_grow(StringMap *map, CompileError *error) {
    size_t new_cap = map->cap == 0U ? 16U : map->cap * 2U;
    StringMapEntry *entries = (StringMapEntry *)calloc(new_cap, sizeof(*entries));
    size_t index = 0U;

    if (entries == NULL) {
        return error_set_oom(error, "Map");
    }

    while (index < map->cap) {
        if (map->entries[index].occupied) {
            (void)strmap_insert_entry(entries, new_cap, map->entries[index].key, map->entries[index].value, true);
        }
        ++index;
    }

    free(map->entries);
    map->entries = entries;
    map->cap = new_cap;
    return true;
}

void strmap_init(StringMap *map) {
    map->entries = NULL;
    map->count = 0U;
    map->cap = 0U;
}

void strmap_free(StringMap *map) {
    free(map->entries);
    map->entries = NULL;
    map->count = 0U;
    map->cap = 0U;
}

bool strmap_get(const StringMap *map, StrSlice key, uintptr_t *out_value) {
    size_t index;

    if (map->cap == 0U) {
        return false;
    }

    index = strmap_index_for(map->cap, key);
    while (map->entries[index].occupied) {
        if (slice_equal(map->entries[index].key, key)) {
            if (out_value != NULL) {
                *out_value = map->entries[index].value;
            }
            return true;
        }
        index = (index + 1U) % map->cap;
    }

    return false;
}

bool strmap_contains(const StringMap *map, StrSlice key) {
    return strmap_get(map, key, NULL);
}

bool strmap_set(StringMap *map, StrSlice key, uintptr_t value, bool replace, CompileError *error) {
    size_t index;

    if (strmap_should_grow(map) && !strmap_grow(map, error)) {
        return false;
    }

    index = strmap_index_for(map->cap, key);
    while (map->entries[index].occupied) {
        if (slice_equal(map->entries[index].key, key)) {
            if (!replace) {
                return true;
            }
            map->entries[index].value = value;
            return true;
        }
        index = (index + 1U) % map->cap;
    }

    map->entries[index].occupied = true;
    map->entries[index].key = key;
    map->entries[index].value = value;
    map->count += 1U;
    return true;
}
