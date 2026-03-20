#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "error.h"

int main(int argc, char **argv) {
    const char *input_file = NULL;
    const char *output_name = "out";
    bool emit_c = false;
    int index = 1;
    CompileError error;

    error_init(&error);

    while (index < argc) {
        const char *arg = argv[index];
        if (strcmp(arg, "-o") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "Missing argument for -o\n");
                error_free(&error);
                return EXIT_FAILURE;
            }
            output_name = argv[index + 1];
            index += 2;
            continue;
        }
        if (strcmp(arg, "--emit-c") == 0) {
            emit_c = true;
            index += 1;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            error_free(&error);
            return EXIT_FAILURE;
        }
        if (input_file != NULL) {
            fprintf(stderr, "Multiple input files are not supported\n");
            error_free(&error);
            return EXIT_FAILURE;
        }
        input_file = arg;
        index += 1;
    }

    if (input_file == NULL) {
        fprintf(stderr, "Incorrect usage. Correct usage is...\n");
        fprintf(stderr, "rivel <input.rivel> [-o <output>] [--emit-c]\n");
        error_free(&error);
        return EXIT_FAILURE;
    }

    if (!driver_compile_file(input_file, output_name, emit_c, &error)) {
        error_print(stderr, &error);
        error_free(&error);
        return EXIT_FAILURE;
    }

    error_free(&error);
    return EXIT_SUCCESS;
}
