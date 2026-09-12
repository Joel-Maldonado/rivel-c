#include "base/arena.h"

#include <stdio.h>

struct ArenaBlock {
    ArenaBlock *next;
    size_t cap;
    size_t used;
    unsigned char *data;
};

enum { ARENA_ALIGN = 16 };

void arena_init(Arena *a, size_t block_size) {
    a->head = NULL;
    a->block_size = block_size ? block_size : 64 * 1024;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b != NULL) {
        ArenaBlock *next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    a->head = NULL;
}

static ArenaBlock *arena_add_block(Arena *a, size_t min_cap) {
    ArenaBlock *b = xmalloc(sizeof *b);
    b->cap = min_cap > a->block_size ? min_cap : a->block_size;
    b->used = 0;
    b->data = xmalloc(b->cap);
    b->next = a->head;
    a->head = b;
    return b;
}

void *arena_alloc(Arena *a, size_t size) {
    ArenaBlock *b = a->head;
    size_t aligned = (size + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);
    void *p;

    if (b == NULL || b->used + aligned > b->cap) {
        b = arena_add_block(a, aligned);
    }
    p = b->data + b->used;
    b->used += aligned;
    memset(p, 0, aligned);
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *copy = arena_alloc(a, n + 1);
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

char *arena_strdup(Arena *a, const char *s) {
    return arena_strndup(a, s, strlen(s));
}

char *arena_vprintf(Arena *a, const char *fmt, va_list args) {
    va_list copy;
    int n;
    char *buf;

    va_copy(copy, args);
    n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) {
        die("vsnprintf failed");
    }
    buf = arena_alloc(a, (size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, args);
    return buf;
}

char *arena_printf(Arena *a, const char *fmt, ...) {
    va_list args;
    char *s;

    va_start(args, fmt);
    s = arena_vprintf(a, fmt, args);
    va_end(args);
    return s;
}
