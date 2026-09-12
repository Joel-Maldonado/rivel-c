#include "driver/driver.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/arena.h"
#include "base/diag.h"
#include "base/source.h"
#include "base/util.h"

#include "ast/ast.h"
#include "backend/qbe.h"
#include "ir/ir.h"
#include "lex/lexer.h"
#include "lower/lower.h"
#include "parse/parser.h"
#include "sema/sema.h"

extern char **environ;

#ifndef RIVEL_HOME
#define RIVEL_HOME "."
#endif
#ifndef RIVEL_BIN
#define RIVEL_BIN "bin"
#endif
#ifndef RIVEL_LIB
#define RIVEL_LIB "lib"
#endif

typedef struct Options {
    const char *input;
    const char *output;
    const char *target;
    bool run;
    bool dump_tokens;
    bool dump_ast;
    bool dump_ir;
    bool emit_il;
    bool emit_asm;
    bool keep;
    int run_argc;
    char **run_argv;
} Options;

static void usage(FILE *out) {
    fputs("usage: rivelc [options] <file.rivel>\n"
          "       rivelc run <file.rivel> [args...]\n"
          "\n"
          "  -o <path>        output executable (default: the input name without .rivel)\n"
          "  -t <target>      QBE target: amd64_sysv, amd64_apple, arm64, arm64_apple, rv64\n"
          "  --emit-il        write the QBE IL next to the output and stop\n"
          "  --emit-asm       write the assembly next to the output and stop\n"
          "  --keep           keep the intermediate .il and .s files\n"
          "  --dump-tokens    print the token stream and stop\n"
          "  --dump-ast       print the syntax tree and stop\n"
          "  --dump-ir        print the IR and stop\n"
          "  -h, --help       show this help\n"
          "  --version        print the version\n"
          "\n"
          "environment: RIVEL_HOME overrides where bin/qbe and lib/rivel_rt.o are found;\n"
          "             CC overrides the C compiler used to assemble and link (default cc)\n",
          out);
}

static bool parse_args(int argc, char **argv, Options *opts) {
    int i = 1;

    memset(opts, 0, sizeof *opts);
    if (argc > 1 && strcmp(argv[1], "run") == 0) {
        opts->run = true;
        i = 2;
    }
    for (; i < argc; i++) {
        const char *arg = argv[i];
        if (opts->run && opts->input != NULL) {
            opts->run_argc = argc - i;
            opts->run_argv = argv + i;
            break;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        } else if (strcmp(arg, "--version") == 0) {
            puts("rivelc 0.2.0-dev");
            exit(0);
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "-t") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "rivelc: %s needs an argument\n", arg);
                return false;
            }
            if (arg[1] == 'o') {
                opts->output = argv[++i];
            } else {
                opts->target = argv[++i];
            }
        } else if (strcmp(arg, "--dump-tokens") == 0) {
            opts->dump_tokens = true;
        } else if (strcmp(arg, "--dump-ast") == 0) {
            opts->dump_ast = true;
        } else if (strcmp(arg, "--dump-ir") == 0) {
            opts->dump_ir = true;
        } else if (strcmp(arg, "--emit-il") == 0) {
            opts->emit_il = true;
        } else if (strcmp(arg, "--emit-asm") == 0) {
            opts->emit_asm = true;
        } else if (strcmp(arg, "--keep") == 0) {
            opts->keep = true;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "rivelc: unknown option `%s`\n", arg);
            return false;
        } else if (opts->input != NULL) {
            fputs("rivelc: only one input file is supported\n", stderr);
            return false;
        } else {
            opts->input = arg;
        }
    }
    if (opts->input == NULL) {
        usage(stderr);
        return false;
    }
    return true;
}

/* Strips a trailing .rivel; "dir/prog.rivel" becomes "dir/prog". */
static char *default_output(const char *input) {
    size_t n = strlen(input);
    const char *ext = ".rivel";
    size_t en = strlen(ext);
    char *out;
    if (n > en && strcmp(input + n - en, ext) == 0) {
        out = xmalloc(n - en + 1);
        memcpy(out, input, n - en);
        out[n - en] = '\0';
        if (strchr(out, '/') == NULL) {
            char *local = xprintf("./%s", out);
            free(out);
            return local;
        }
        return out;
    }
    return xprintf("%s.out", input);
}

static const char *home_dir(void) {
    const char *env = getenv("RIVEL_HOME");
    return env != NULL && env[0] != '\0' ? env : RIVEL_HOME;
}

static char *tool_path(const char *dir_env, const char *dir_default, const char *name) {
    const char *dir = getenv(dir_env);
    if (dir == NULL || dir[0] == '\0') {
        dir = dir_default;
    }
    if (dir[0] == '/') {
        return xprintf("%s/%s", dir, name);
    }
    return xprintf("%s/%s/%s", home_dir(), dir, name);
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Runs a command and returns its exit status, or -1 if it could not start. */
static int run_command(char **argv) {
    pid_t pid;
    int status;
    int err = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    if (err != 0) {
        fprintf(stderr, "rivelc: cannot run `%s`: %s\n", argv[0], strerror(err));
        return -1;
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static bool write_il(const IrModule *ir, const char *path) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "rivelc: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    qbe_emit(ir, f);
    if (fclose(f) != 0) {
        fprintf(stderr, "rivelc: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

/* Assembles and links; returns the process exit status to report. */
static int build_native(const Options *opts, const IrModule *ir, const char *output) {
    char *il_path = xprintf("%s.il", output);
    char *asm_path = xprintf("%s.s", output);
    char *qbe = tool_path("RIVEL_BIN", RIVEL_BIN, "qbe");
    char *runtime = tool_path("RIVEL_LIB", RIVEL_LIB, "rivel_rt.o");
    const char *cc = getenv("CC");
    int status = 1;

    if (cc == NULL || cc[0] == '\0') {
        cc = "cc";
    }
    if (!write_il(ir, il_path)) {
        goto done;
    }
    if (opts->emit_il) {
        status = 0;
        goto done;
    }
    if (!file_exists(qbe)) {
        fprintf(stderr,
                "rivelc: cannot find the QBE backend at %s; run `make` in the Rivel checkout or set RIVEL_HOME\n", qbe);
        goto done;
    }
    {
        char *argv[8];
        int n = 0;
        argv[n++] = qbe;
        if (opts->target != NULL) {
            argv[n++] = "-t";
            argv[n++] = (char *)opts->target;
        }
        argv[n++] = "-o";
        argv[n++] = asm_path;
        argv[n++] = il_path;
        argv[n] = NULL;
        if (run_command(argv) != 0) {
            fprintf(stderr, "rivelc: the backend failed on %s (this is a compiler bug; please report it)\n", il_path);
            goto done;
        }
    }
    if (opts->emit_asm) {
        status = 0;
        goto done;
    }
    if (!file_exists(runtime)) {
        fprintf(stderr, "rivelc: cannot find the runtime at %s; run `make` in the Rivel checkout or set RIVEL_HOME\n",
                runtime);
        goto done;
    }
    {
        char *argv[] = {(char *)cc, asm_path, runtime, "-o", (char *)output, "-lm", NULL};
        if (run_command(argv) != 0) {
            fprintf(stderr, "rivelc: linking failed\n");
            goto done;
        }
    }
    status = 0;

done:
    if (!opts->keep && !opts->emit_il) {
        remove(il_path);
    }
    if (!opts->keep && !opts->emit_asm && !opts->emit_il) {
        remove(asm_path);
    }
    free(il_path);
    free(asm_path);
    free(qbe);
    free(runtime);
    return status;
}

static int run_program(const Options *opts, const char *output) {
    char **argv = xcalloc((size_t)opts->run_argc + 2, sizeof *argv);
    int status;
    argv[0] = (char *)output;
    for (int i = 0; i < opts->run_argc; i++) {
        argv[i + 1] = opts->run_argv[i];
    }
    status = run_command(argv);
    free(argv);
    return status < 0 ? 1 : status;
}

int driver_main(int argc, char **argv) {
    Options opts;
    Source *src;
    char *err = NULL;
    char *output = NULL;
    Arena arena;
    Diags diags;
    TokenVec tokens = {0};
    Module *module;
    Program prog;
    IrModule ir;
    bool have_prog = false;
    bool have_ir = false;
    bool color = isatty(STDERR_FILENO);
    int status = 1;

    if (!parse_args(argc, argv, &opts)) {
        return 2;
    }
    src = source_from_file(opts.input, &err);
    if (src == NULL) {
        fprintf(stderr, "rivelc: %s\n", err);
        free(err);
        return 2;
    }
    if (opts.run) {
        output = xprintf("/tmp/rivel-run-%ld", (long)getpid());
    } else {
        output = opts.output != NULL ? xstrdup(opts.output) : default_output(opts.input);
    }

    arena_init(&arena, 0);
    diags_init(&diags);

    lex_source(src, &arena, &diags, &tokens);
    if (opts.dump_tokens) {
        tokens_dump(&tokens, src, stdout);
        status = diags_ok(&diags) ? 0 : 1;
        goto done;
    }
    module = parse_module(src, &tokens, &arena, &diags);
    if (opts.dump_ast) {
        ast_dump(module, stdout);
        status = diags_ok(&diags) ? 0 : 1;
        goto done;
    }
    if (!diags_ok(&diags)) {
        goto done;
    }
    have_prog = true;
    if (!sema_check(module, &arena, &diags, &prog)) {
        goto done;
    }
    ir_module_init(&ir, &arena, opts.input);
    have_ir = true;
    lower_program(&prog, &arena, &ir);
    if (opts.dump_ir) {
        ir_dump(&ir, stdout);
        status = 0;
        goto done;
    }
    diags_print(&diags, stderr, color);
    diags_free(&diags);
    diags_init(&diags);
    status = build_native(&opts, &ir, output);
    if (status == 0 && opts.run) {
        status = run_program(&opts, output);
        remove(output);
    }

done:
    diags_print(&diags, stderr, color);
    if (have_ir) {
        ir_module_free(&ir);
    }
    if (have_prog) {
        program_free(&prog);
    }
    free(output);
    vec_free(&tokens);
    diags_free(&diags);
    arena_free(&arena);
    source_free(src);
    return status;
}
