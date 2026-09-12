#ifndef RV_BASE_ARENA_H
#define RV_BASE_ARENA_H

#include <stdarg.h>
#include <stddef.h>

#include "base/util.h"

/*
 * Bump allocator. Everything allocated from an arena lives until arena_free.
 * The AST, types, symbols, and IR all live in arenas; nothing in the compiler
 * frees individual nodes.
 */
typedef struct ArenaBlock ArenaBlock;

typedef struct Arena {
    ArenaBlock *head;
    size_t block_size;
} Arena;

void arena_init(Arena *a, size_t block_size);
void arena_free(Arena *a);
void *arena_alloc(Arena *a, size_t size); /* 16-byte aligned, zeroed */
char *arena_strndup(Arena *a, const char *s, size_t n);
char *arena_strdup(Arena *a, const char *s);
char *arena_vprintf(Arena *a, const char *fmt, va_list args) VPRINTF_LIKE(2);
char *arena_printf(Arena *a, const char *fmt, ...) PRINTF_LIKE(2, 3);

#define arena_new(a, T) ((T *)arena_alloc((a), sizeof(T)))

#endif
