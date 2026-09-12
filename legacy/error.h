#ifndef RIVEL_ERROR_H
#define RIVEL_ERROR_H

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char *phase;
    int line;
    int column;
    bool has_location;
    char *message;
} CompileError;

void error_init(CompileError *error);
void error_free(CompileError *error);
bool error_set(CompileError *error, const char *phase, const char *fmt, ...);
bool error_set_at(CompileError *error, const char *phase, int line, int column, const char *fmt, ...);
bool error_set_oom(CompileError *error, const char *phase);
void error_print(FILE *stream, const CompileError *error);

#endif
