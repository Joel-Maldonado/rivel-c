#include "driver_internal.h"

#include <errno.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

const char DRIVER_HOST_C_COMPILER[] = "gcc";

bool driver_wait_for_process(pid_t pid, CompileError *error) {
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return error_set(error, "Driver", "Failed while waiting for host C compiler: %s", strerror(errno));
        }
    }

    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0) {
            return true;
        }
        return error_set(error, "Driver", "Host C compiler `%s` exited with status %d", DRIVER_HOST_C_COMPILER, WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return error_set(error, "Driver", "Host C compiler `%s` terminated by signal %d", DRIVER_HOST_C_COMPILER, WTERMSIG(status));
    }
    return error_set(error, "Driver", "Host C compiler `%s` ended unexpectedly", DRIVER_HOST_C_COMPILER);
}

bool driver_run_host_compiler(const char *generated_c_path, const char *output_name, CompileError *error) {
    char *argv[] = {
        (char *)DRIVER_HOST_C_COMPILER,
        "-std=c11",
        (char *)generated_c_path,
        "-o",
        (char *)output_name,
        NULL
    };
    pid_t pid;
    int spawn_error = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);

    if (spawn_error != 0) {
        return error_set(error, "Driver", "Failed to launch host C compiler `%s`: %s", DRIVER_HOST_C_COMPILER, strerror(spawn_error));
    }
    return driver_wait_for_process(pid, error);
}
