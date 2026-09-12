#include "driver_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool driver_read_entire_file(const char *path, char **out_contents, CompileError *error) {
    FILE *file = fopen(path, "rb");
    long size;
    size_t read_size;
    char *buffer;

    if (file == NULL) {
        return error_set(error, NULL, "Failed to open input file: %s", path);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }

    buffer = (char *)malloc((size_t)size + 1U);
    if (buffer == NULL) {
        fclose(file);
        return error_set_oom(error, NULL);
    }

    read_size = fread(buffer, 1U, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        free(buffer);
        return error_set(error, NULL, "Failed to read input file: %s", path);
    }

    buffer[size] = '\0';
    *out_contents = buffer;
    return true;
}

bool driver_write_entire_file(const char *path, const char *contents, CompileError *error) {
    FILE *file = fopen(path, "wb");
    size_t len = strlen(contents);

    if (file == NULL) {
        return error_set(error, NULL, "Failed to open output file: %s", path);
    }
    if (fwrite(contents, 1U, len, file) != len) {
        fclose(file);
        return error_set(error, NULL, "Failed to write output file: %s", path);
    }
    fclose(file);
    return true;
}

char *driver_join_suffix(const char *base, const char *suffix, CompileError *error) {
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    char *joined = (char *)malloc(base_len + suffix_len + 1U);

    if (joined == NULL) {
        error_set_oom(error, NULL);
        return NULL;
    }
    (void)snprintf(joined, base_len + suffix_len + 1U, "%s%s", base, suffix);
    return joined;
}
