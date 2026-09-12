#ifndef RV_BASE_VEC_H
#define RV_BASE_VEC_H

#include <stddef.h>
#include <stdlib.h>

typedef struct Arena Arena;

/*
 * Typed growable array. Declare a named type before storing one in a struct
 * or passing it around:
 *
 *     typedef Vec(int) IntVec;
 *     IntVec xs = {0};
 *     vec_push(&xs, 1);
 *     for (size_t i = 0; i < xs.len; i++) use(xs.data[i]);
 *     vec_free(&xs);
 */
#define Vec(T)                                                                                                         \
    struct {                                                                                                           \
        T *data;                                                                                                       \
        size_t len;                                                                                                    \
        size_t cap;                                                                                                    \
    }

void vec__grow(void **data, size_t *cap, size_t want, size_t elem_size);
void vec__grow_arena(Arena *a, void **data, size_t *cap, size_t len, size_t want, size_t elem_size);

#define vec_reserve(v, n) vec__grow((void **)&(v)->data, &(v)->cap, (n), sizeof *(v)->data)
#define vec_push(v, x) (vec_reserve((v), (v)->len + 1), (v)->data[(v)->len++] = (x))
#define vec_pop(v) ((v)->data[--(v)->len])
#define vec_last(v) ((v)->data[(v)->len - 1])
#define vec_clear(v) ((void)((v)->len = 0))
#define vec_free(v) (free((v)->data), (v)->data = NULL, (v)->len = 0, (v)->cap = 0)

/*
 * Arena-owned variant: storage comes from the arena and is never freed
 * individually. Use for vectors embedded in arena-allocated structures such
 * as syntax tree nodes. Never vec_free one of these.
 */
#define avec_reserve(a, v, n) vec__grow_arena((a), (void **)&(v)->data, &(v)->cap, (v)->len, (n), sizeof *(v)->data)
#define avec_push(a, v, x) (avec_reserve((a), (v), (v)->len + 1), (v)->data[(v)->len++] = (x))

#endif
