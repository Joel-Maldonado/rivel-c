#include "slice.h"

#include <string.h>

StrSlice slice_from_parts(const char *data, size_t len) {
    StrSlice slice;
    slice.data = data;
    slice.len = len;
    return slice;
}

StrSlice slice_from_cstr(const char *text) {
    return slice_from_parts(text, strlen(text));
}

bool slice_equal(StrSlice lhs, StrSlice rhs) {
    if (lhs.len != rhs.len) {
        return false;
    }
    if (lhs.len == 0U) {
        return true;
    }
    return memcmp(lhs.data, rhs.data, lhs.len) == 0;
}

bool slice_equal_cstr(StrSlice lhs, const char *rhs) {
    return slice_equal(lhs, slice_from_cstr(rhs));
}

uint64_t slice_hash(StrSlice slice) {
    uint64_t hash = 1469598103934665603ULL;
    size_t index = 0U;

    while (index < slice.len) {
        hash ^= (unsigned char)slice.data[index];
        hash *= 1099511628211ULL;
        ++index;
    }

    return hash;
}
