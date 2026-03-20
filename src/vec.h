#ifndef RIVEL_VEC_H
#define RIVEL_VEC_H

#include <stddef.h>

#include "arena.h"
#include "error.h"

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    size_t elem_size;
    Arena *arena;
} Vec;

void vec_init(Vec *vec, size_t elem_size, Arena *arena);
void vec_free(Vec *vec);
void vec_clear(Vec *vec);
void *vec_get(const Vec *vec, size_t index);
void *vec_push(Vec *vec, CompileError *error);
bool vec_push_copy(Vec *vec, const void *value, CompileError *error);

#endif
