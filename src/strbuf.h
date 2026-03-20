#ifndef RIVEL_STRBUF_H
#define RIVEL_STRBUF_H

#include <stdbool.h>
#include <stddef.h>

#include "error.h"
#include "slice.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void strbuf_init(StrBuf *buf);
void strbuf_free(StrBuf *buf);
void strbuf_clear(StrBuf *buf);
bool strbuf_append_char(StrBuf *buf, char ch, CompileError *error);
bool strbuf_append_cstr(StrBuf *buf, const char *text, CompileError *error);
bool strbuf_append_slice(StrBuf *buf, StrSlice slice, CompileError *error);
bool strbuf_append_fmt(StrBuf *buf, CompileError *error, const char *fmt, ...);
const char *strbuf_cstr(const StrBuf *buf);

#endif
