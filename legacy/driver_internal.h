#ifndef RIVEL_DRIVER_INTERNAL_H
#define RIVEL_DRIVER_INTERNAL_H

#include <stdbool.h>
#include <sys/types.h>

#include "arena.h"
#include "backend_c.h"
#include "driver.h"
#include "parser.h"
#include "semantic.h"
#include "strbuf.h"
#include "tokenizer.h"

typedef struct {
    Arena arena;
    TokenList tokens;
    Program program;
    SemanticResult *semantics;
    StrBuf generated_c;
} DriverPipeline;

extern const char DRIVER_HOST_C_COMPILER[];

bool driver_read_entire_file(const char *path, char **out_contents, CompileError *error);
bool driver_write_entire_file(const char *path, const char *contents, CompileError *error);
char *driver_join_suffix(const char *base, const char *suffix, CompileError *error);

void driver_pipeline_init(DriverPipeline *pipeline);
void driver_pipeline_dispose(DriverPipeline *pipeline);
bool driver_run_pipeline(const char *source, DriverPipeline *pipeline, CompileError *error);

bool driver_wait_for_process(pid_t pid, CompileError *error);
bool driver_run_host_compiler(const char *generated_c_path, const char *output_name, CompileError *error);

#endif
