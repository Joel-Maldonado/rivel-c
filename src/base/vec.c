#include "base/vec.h"

#include "base/arena.h"
#include "base/util.h"

void vec__grow(void **data, size_t *cap, size_t want, size_t elem_size) {
    size_t new_cap;

    if (want <= *cap) {
        return;
    }
    new_cap = *cap ? *cap : 8;
    while (new_cap < want) {
        new_cap *= 2;
    }
    *data = xrealloc(*data, new_cap * elem_size);
    *cap = new_cap;
}

void vec__grow_arena(Arena *a, void **data, size_t *cap, size_t len, size_t want, size_t elem_size) {
    size_t new_cap;
    void *fresh;

    if (want <= *cap) {
        return;
    }
    new_cap = *cap ? *cap : 8;
    while (new_cap < want) {
        new_cap *= 2;
    }
    fresh = arena_alloc(a, new_cap * elem_size);
    if (len > 0) {
        memcpy(fresh, *data, len * elem_size);
    }
    *data = fresh;
    *cap = new_cap;
}
