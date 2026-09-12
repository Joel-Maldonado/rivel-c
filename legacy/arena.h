#ifndef RIVEL_ARENA_H
#define RIVEL_ARENA_H

#include <stddef.h>
#include <stdarg.h>

#include "error.h"
#include "slice.h"

typedef struct ArenaBlock ArenaBlock;

struct ArenaBlock {
    ArenaBlock *next;
    size_t capacity;
    size_t used;
    unsigned char data[];
};

typedef struct {
    ArenaBlock *head;
    size_t block_size;
} Arena;

void arena_init(Arena *arena, size_t block_size);
void arena_free(Arena *arena);
void *arena_alloc(Arena *arena, size_t size, size_t alignment, CompileError *error);
void *arena_alloc_zero(Arena *arena, size_t size, size_t alignment, CompileError *error);
char *arena_copy_cstr(Arena *arena, const char *text, CompileError *error);
char *arena_copy_slice(Arena *arena, StrSlice slice, CompileError *error);
char *arena_vprintf(Arena *arena, CompileError *error, const char *fmt, va_list args);
char *arena_printf(Arena *arena, CompileError *error, const char *fmt, ...);

#endif
