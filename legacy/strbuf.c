#include "strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool strbuf_reserve(StrBuf *buf, size_t wanted, CompileError *error) {
    size_t new_cap;
    char *data;

    if (wanted <= buf->cap) {
        return true;
    }

    new_cap = buf->cap == 0U ? 64U : buf->cap;
    while (new_cap < wanted) {
        new_cap *= 2U;
    }

    data = (char *)realloc(buf->data, new_cap);
    if (data == NULL) {
        return error_set_oom(error, "StrBuf");
    }

    buf->data = data;
    buf->cap = new_cap;
    return true;
}

static bool strbuf_append_n(StrBuf *buf, const char *text, size_t len, CompileError *error) {
    if (!strbuf_reserve(buf, buf->len + len + 1U, error)) {
        return false;
    }

    if (len > 0U) {
        memcpy(buf->data + buf->len, text, len);
    }
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

void strbuf_init(StrBuf *buf) {
    buf->data = NULL;
    buf->len = 0U;
    buf->cap = 0U;
}

void strbuf_free(StrBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0U;
    buf->cap = 0U;
}

void strbuf_clear(StrBuf *buf) {
    buf->len = 0U;
    if (buf->data != NULL) {
        buf->data[0] = '\0';
    }
}

bool strbuf_append_char(StrBuf *buf, char ch, CompileError *error) {
    return strbuf_append_n(buf, &ch, 1U, error);
}

bool strbuf_append_cstr(StrBuf *buf, const char *text, CompileError *error) {
    return strbuf_append_n(buf, text, strlen(text), error);
}

bool strbuf_append_slice(StrBuf *buf, StrSlice slice, CompileError *error) {
    return strbuf_append_n(buf, slice.data, slice.len, error);
}

bool strbuf_append_fmt(StrBuf *buf, CompileError *error, const char *fmt, ...) {
    va_list args;
    va_list copy;
    int needed;

    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return error_set(error, "StrBuf", "Failed to format string");
    }

    if (!strbuf_reserve(buf, buf->len + (size_t)needed + 1U, error)) {
        va_end(args);
        return false;
    }

    (void)vsnprintf(buf->data + buf->len, (size_t)needed + 1U, fmt, args);
    va_end(args);
    buf->len += (size_t)needed;
    return true;
}

const char *strbuf_cstr(const StrBuf *buf) {
    if (buf->data == NULL) {
        return "";
    }
    return buf->data;
}
