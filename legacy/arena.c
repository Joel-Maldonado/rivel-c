#include "arena.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t arena_align_up(size_t value, size_t alignment) {
    size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

static ArenaBlock *arena_new_block(size_t capacity) {
    ArenaBlock *block = (ArenaBlock *)malloc(sizeof(*block) + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->next = NULL;
    block->capacity = capacity;
    block->used = 0U;
    return block;
}

void arena_init(Arena *arena, size_t block_size) {
    arena->head = NULL;
    arena->block_size = block_size;
}

void arena_free(Arena *arena) {
    ArenaBlock *block = arena->head;

    while (block != NULL) {
        ArenaBlock *next = block->next;
        free(block);
        block = next;
    }

    arena->head = NULL;
}

void *arena_alloc(Arena *arena, size_t size, size_t alignment, CompileError *error) {
    ArenaBlock *block = arena->head;
    size_t aligned_used;
    void *ptr;

    if (alignment == 0U) {
        alignment = alignof(max_align_t);
    }

    if (block == NULL) {
        size_t capacity = arena->block_size;
        if (capacity < size + alignment) {
            capacity = size + alignment;
        }
        block = arena_new_block(capacity);
        if (block == NULL) {
            return error_set_oom(error, "Arena"), NULL;
        }
        block->next = arena->head;
        arena->head = block;
    }

    aligned_used = arena_align_up(block->used, alignment);
    if (aligned_used + size > block->capacity) {
        size_t capacity = arena->block_size;
        if (capacity < size + alignment) {
            capacity = size + alignment;
        }
        block = arena_new_block(capacity);
        if (block == NULL) {
            return error_set_oom(error, "Arena"), NULL;
        }
        block->next = arena->head;
        arena->head = block;
        aligned_used = arena_align_up(block->used, alignment);
    }

    ptr = block->data + aligned_used;
    block->used = aligned_used + size;
    return ptr;
}

void *arena_alloc_zero(Arena *arena, size_t size, size_t alignment, CompileError *error) {
    void *ptr = arena_alloc(arena, size, alignment, error);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

char *arena_copy_cstr(Arena *arena, const char *text, CompileError *error) {
    size_t len = strlen(text);
    char *copy = (char *)arena_alloc(arena, len + 1U, alignof(char), error);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1U);
    return copy;
}

char *arena_copy_slice(Arena *arena, StrSlice slice, CompileError *error) {
    char *copy = (char *)arena_alloc(arena, slice.len + 1U, alignof(char), error);
    if (copy == NULL) {
        return NULL;
    }
    if (slice.len > 0U) {
        memcpy(copy, slice.data, slice.len);
    }
    copy[slice.len] = '\0';
    return copy;
}

char *arena_vprintf(Arena *arena, CompileError *error, const char *fmt, va_list args) {
    va_list copy;
    int needed;
    char *buffer;

    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        error_set(error, "Arena", "Failed to format string");
        return NULL;
    }

    buffer = (char *)arena_alloc(arena, (size_t)needed + 1U, alignof(char), error);
    if (buffer == NULL) {
        return NULL;
    }

    (void)vsnprintf(buffer, (size_t)needed + 1U, fmt, args);
    return buffer;
}

char *arena_printf(Arena *arena, CompileError *error, const char *fmt, ...) {
    va_list args;
    char *buffer;

    va_start(args, fmt);
    buffer = arena_vprintf(arena, error, fmt, args);
    va_end(args);
    return buffer;
}
