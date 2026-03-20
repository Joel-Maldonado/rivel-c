#ifndef RIVEL_SLICE_H
#define RIVEL_SLICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *data;
    size_t len;
} StrSlice;

StrSlice slice_from_parts(const char *data, size_t len);
StrSlice slice_from_cstr(const char *text);
bool slice_equal(StrSlice lhs, StrSlice rhs);
bool slice_equal_cstr(StrSlice lhs, const char *rhs);
uint64_t slice_hash(StrSlice slice);

#endif
