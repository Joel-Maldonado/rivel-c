#ifndef RIVEL_DRIVER_H
#define RIVEL_DRIVER_H

#include <stdbool.h>

#include "error.h"

bool driver_compile_file(const char *input_file, const char *output_name, bool emit_c, CompileError *error);

#endif
