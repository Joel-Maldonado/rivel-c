#include "backend_c_internal.h"

static bool backend_emit_global_constants(Backend *backend) {
    size_t index = 0U;

    while (index < decl_list_len(&backend->program->decls)) {
        const Decl *decl = decl_list_get(&backend->program->decls, index);
        if (decl->kind == DECL_GLOBAL_CONST) {
            const SemanticGlobalInfo *info = semantic_lookup_global(backend->semantics, decl->name);
            if (info == NULL) {
                return error_set(backend->error, "Backend", "Internal error: missing semantic info for global `%.*s`", (int)decl->name.len, decl->name.data);
            }
            if (!backend_emit_line(backend, arena_printf(backend->arena, backend->error, "static const %s %s = %s;",
                                                         backend_c_type(info->type),
                                                         backend_global_name(backend, decl->name),
                                                         backend_literal(backend, info->value)))) {
                return false;
            }
        }
        index += 1U;
    }

    if (decl_list_len(&backend->program->decls) > 0U) {
        return backend_emit_line(backend, "");
    }
    return true;
}

static bool backend_emit_function_prototypes(Backend *backend) {
    size_t index = 0U;

    while (index < decl_list_len(&backend->program->decls)) {
        const Decl *decl = decl_list_get(&backend->program->decls, index);
        if (decl->kind == DECL_FUNCTION && !backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s;", backend_function_signature(backend, decl)))) {
            return false;
        }
        index += 1U;
    }
    return backend_emit_line(backend, "");
}

static bool backend_emit_functions(Backend *backend) {
    size_t index = 0U;

    while (index < decl_list_len(&backend->program->decls)) {
        const Decl *decl = decl_list_get(&backend->program->decls, index);
        if (decl->kind == DECL_FUNCTION) {
            if (!backend_emit_function(backend, decl) || !backend_emit_line(backend, "")) {
                return false;
            }
        }
        index += 1U;
    }
    return true;
}

bool c_backend_generate(const Program *program, const SemanticResult *semantics, Arena *arena, StrBuf *output, CompileError *error) {
    Backend backend;

    backend.program = program;
    backend.semantics = semantics;
    backend.arena = arena;
    backend.output = output;
    backend.error = error;
    backend.indent = 0;
    backend.next_local_id = 0U;
    backend_scope_stack_init(&backend.scopes, NULL);
    strbuf_clear(output);

    if (!backend_emit_prelude(&backend)
        || !backend_emit_global_constants(&backend)
        || !backend_emit_function_prototypes(&backend)
        || !backend_emit_functions(&backend)
        || !backend_emit_program_entry(&backend)) {
        backend_clear_scopes(&backend);
        backend_scope_stack_free(&backend.scopes);
        return false;
    }

    backend_clear_scopes(&backend);
    backend_scope_stack_free(&backend.scopes);
    return true;
}
