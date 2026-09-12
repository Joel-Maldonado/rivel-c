#include "vec.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

static bool vec_reserve(Vec *vec, size_t wanted, CompileError *error) {
    size_t new_cap;
    unsigned char *data;

    if (wanted <= vec->cap) {
        return true;
    }

    new_cap = vec->cap == 0U ? 8U : vec->cap;
    while (new_cap < wanted) {
        new_cap *= 2U;
    }

    if (vec->arena != NULL) {
        data = (unsigned char *)arena_alloc(vec->arena, new_cap * vec->elem_size, alignof(max_align_t), error);
        if (data == NULL) {
            return false;
        }
        if (vec->len > 0U) {
            memcpy(data, vec->data, vec->len * vec->elem_size);
        }
        vec->data = data;
        vec->cap = new_cap;
        return true;
    }

    data = (unsigned char *)realloc(vec->data, new_cap * vec->elem_size);
    if (data == NULL) {
        return error_set_oom(error, "Vec");
    }

    vec->data = data;
    vec->cap = new_cap;
    return true;
}

void vec_init(Vec *vec, size_t elem_size, Arena *arena) {
    vec->data = NULL;
    vec->len = 0U;
    vec->cap = 0U;
    vec->elem_size = elem_size;
    vec->arena = arena;
}

void vec_free(Vec *vec) {
    if (vec->arena == NULL) {
        free(vec->data);
    }
    vec->data = NULL;
    vec->len = 0U;
    vec->cap = 0U;
}

void vec_clear(Vec *vec) {
    vec->len = 0U;
}

void *vec_get(const Vec *vec, size_t index) {
    return vec->data + (index * vec->elem_size);
}

void *vec_push(Vec *vec, CompileError *error) {
    void *slot;

    if (!vec_reserve(vec, vec->len + 1U, error)) {
        return NULL;
    }

    slot = vec->data + (vec->len * vec->elem_size);
    memset(slot, 0, vec->elem_size);
    vec->len += 1U;
    return slot;
}

bool vec_push_copy(Vec *vec, const void *value, CompileError *error) {
    void *slot = vec_push(vec, error);
    if (slot == NULL) {
        return false;
    }
    memcpy(slot, value, vec->elem_size);
    return true;
}
