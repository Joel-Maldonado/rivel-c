#include "base/source.h"

#include <errno.h>
#include <stdio.h>

#include "base/util.h"

Source *source_new(const char *name, const char *text, size_t len) {
    Source *src = xcalloc(1, sizeof *src);

    src->name = xstrdup(name);
    src->text = xmalloc(len + 1);
    memcpy(src->text, text, len);
    src->text[len] = '\0';
    src->len = len;
    vec_push(&src->line_starts, 0u);
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            vec_push(&src->line_starts, (uint32_t)(i + 1));
        }
    }
    return src;
}

Source *source_from_file(const char *path, char **err) {
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    Source *src;

    if (f == NULL) {
        *err = xprintf("cannot open %s: %s", path, strerror(errno));
        return NULL;
    }
    for (;;) {
        size_t got;
        if (len == cap) {
            cap = cap ? cap * 2 : 4096;
            buf = xrealloc(buf, cap);
        }
        got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) {
            break;
        }
    }
    if (ferror(f)) {
        *err = xprintf("cannot read %s: %s", path, strerror(errno));
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    src = source_new(path, buf ? buf : "", len);
    free(buf);
    return src;
}

void source_free(Source *src) {
    if (src == NULL) {
        return;
    }
    free(src->name);
    free(src->text);
    vec_free(&src->line_starts);
    free(src);
}

void source_position(const Source *src, uint32_t offset, int *line, int *col) {
    size_t lo = 0;
    size_t hi = src->line_starts.len;

    if (offset > src->len) {
        offset = (uint32_t)src->len;
    }
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (src->line_starts.data[mid] <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    *line = (int)lo + 1;
    *col = (int)(offset - src->line_starts.data[lo]) + 1;
}

Str source_line(const Source *src, int line) {
    size_t idx = (size_t)line - 1;
    uint32_t start;
    uint32_t end;

    if (line < 1 || idx >= src->line_starts.len) {
        return (Str){"", 0};
    }
    start = src->line_starts.data[idx];
    end = idx + 1 < src->line_starts.len ? src->line_starts.data[idx + 1] : (uint32_t)src->len;
    while (end > start && (src->text[end - 1] == '\n' || src->text[end - 1] == '\r')) {
        end--;
    }
    return (Str){src->text + start, end - start};
}
