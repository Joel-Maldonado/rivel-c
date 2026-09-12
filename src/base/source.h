#ifndef RV_BASE_SOURCE_H
#define RV_BASE_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "base/str.h"
#include "base/vec.h"

/* Half-open byte range [lo, hi) into one Source. */
typedef struct Span {
    uint32_t lo;
    uint32_t hi;
} Span;

static inline Span span_make(uint32_t lo, uint32_t hi) {
    return (Span){lo, hi};
}

static inline Span span_join(Span a, Span b) {
    return (Span){a.lo < b.lo ? a.lo : b.lo, a.hi > b.hi ? a.hi : b.hi};
}

typedef Vec(uint32_t) U32Vec;

typedef struct Source {
    char *name;
    char *text; /* NUL-terminated, owned */
    size_t len;
    U32Vec line_starts; /* byte offset of each line's first character */
} Source;

Source *source_new(const char *name, const char *text, size_t len);
/* Returns NULL and sets *err (malloc'd) on failure. */
Source *source_from_file(const char *path, char **err);
void source_free(Source *src);

/* 1-based line and byte column for an offset. */
void source_position(const Source *src, uint32_t offset, int *line, int *col);
/* The text of a 1-based line without its newline. */
Str source_line(const Source *src, int line);

#endif
