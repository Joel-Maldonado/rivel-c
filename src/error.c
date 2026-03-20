#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool error_set_va(CompileError *error, const char *phase, bool has_location, int line, int column, const char *fmt, va_list args) {
    va_list copy;
    int needed;
    char *message;

    error_free(error);
    error->phase = phase;
    error->has_location = has_location;
    error->line = line;
    error->column = column;

    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        error->message = NULL;
        return false;
    }

    message = (char *)malloc((size_t)needed + 1U);
    if (message == NULL) {
        error->message = NULL;
        return false;
    }

    (void)vsnprintf(message, (size_t)needed + 1U, fmt, args);
    error->message = message;
    return false;
}

void error_init(CompileError *error) {
    error->phase = NULL;
    error->line = 0;
    error->column = 0;
    error->has_location = false;
    error->message = NULL;
}

void error_free(CompileError *error) {
    free(error->message);
    error->message = NULL;
    error->phase = NULL;
    error->line = 0;
    error->column = 0;
    error->has_location = false;
}

bool error_set(CompileError *error, const char *phase, const char *fmt, ...) {
    va_list args;
    bool result;

    va_start(args, fmt);
    result = error_set_va(error, phase, false, 0, 0, fmt, args);
    va_end(args);
    return result;
}

bool error_set_at(CompileError *error, const char *phase, int line, int column, const char *fmt, ...) {
    va_list args;
    bool result;

    va_start(args, fmt);
    result = error_set_va(error, phase, true, line, column, fmt, args);
    va_end(args);
    return result;
}

bool error_set_oom(CompileError *error, const char *phase) {
    error_free(error);
    error->phase = phase;
    error->has_location = false;
    error->message = NULL;
    return false;
}

void error_print(FILE *stream, const CompileError *error) {
    const char *message = error->message;

    if (message == NULL) {
        message = "out of memory";
    }

    if (error->phase == NULL) {
        fprintf(stream, "%s\n", message);
        return;
    }

    if (error->has_location) {
        fprintf(stream, "[%s Error] line %d:%d %s\n", error->phase, error->line, error->column, message);
        return;
    }

    fprintf(stream, "[%s Error] %s\n", error->phase, message);
}
