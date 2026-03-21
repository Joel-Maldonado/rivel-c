#include "backend_c_internal.h"

static bool backend_emit_struct_release_body(Backend *backend, const Decl *decl, const char *value_name) {
    size_t field_index = 0U;

    while (field_index < struct_field_decl_list_len(&decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&decl->as.struct_decl.fields, field_index);
        char *field_value = arena_printf(backend->arena,
                                         backend->error,
                                         "%s.%.*s",
                                         value_name,
                                         (int)field->name.len,
                                         field->name.data);

        if (field_value == NULL || !backend_emit_release_value(backend, field->type, field_value)) {
            return false;
        }
        field_index += 1U;
    }
    return true;
}

static bool backend_emit_struct_retain_body(Backend *backend, const Decl *decl, const char *value_name) {
    size_t field_index = 0U;

    while (field_index < struct_field_decl_list_len(&decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&decl->as.struct_decl.fields, field_index);
        char *field_value = arena_printf(backend->arena,
                                         backend->error,
                                         "%s.%.*s",
                                         value_name,
                                         (int)field->name.len,
                                         field->name.data);
        char *retained_value = backend_retain_value_expr(backend, field->type, field_value);

        if (field_value == NULL || retained_value == NULL) {
            return false;
        }
        if (retained_value != field_value
            && !backend_emit_line(backend, arena_printf(backend->arena, backend->error, "%s = %s;", field_value, retained_value))) {
            return false;
        }
        field_index += 1U;
    }
    return true;
}

static bool backend_emit_struct_take_field_helper(Backend *backend, const Decl *decl, const StructFieldDecl *field) {
    Type struct_type = {.kind = TYPE_STRUCT, .struct_name = decl->name};

    if (!backend_emit_line(backend,
                           arena_printf(backend->arena,
                                        backend->error,
                                        "static %s %s(%s value) {",
                                        backend_c_type(backend, field->type),
                                        backend_struct_take_field_name(backend, decl->name, field->name),
                                        backend_c_type(backend, struct_type)))) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_line(backend,
                           arena_printf(backend->arena,
                                        backend->error,
                                        "%s field = %s;",
                                        backend_c_type(backend, field->type),
                                        backend_retain_value_expr(backend,
                                                                  field->type,
                                                                  arena_printf(backend->arena,
                                                                               backend->error,
                                                                               "value.%.*s",
                                                                               (int)field->name.len,
                                                                               field->name.data))))) {
        return false;
    }
    if (!backend_emit_struct_release_body(backend, decl, "value")
        || !backend_emit_line(backend, "return field;")) {
        return false;
    }
    backend_indent_pop(backend);
    return backend_emit_line(backend, "}");
}

static bool backend_emit_struct_helpers(Backend *backend, const Decl *decl) {
    Type struct_type = {.kind = TYPE_STRUCT, .struct_name = decl->name};
    size_t field_index = 0U;

    if (!backend_type_contains_owned_strings(backend, struct_type)) {
        return true;
    }
    if (!backend_emit_line(backend,
                           arena_printf(backend->arena,
                                        backend->error,
                                        "static %s %s(%s value) {",
                                        backend_c_type(backend, struct_type),
                                        backend_struct_retain_name(backend, decl->name),
                                        backend_c_type(backend, struct_type)))) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_struct_retain_body(backend, decl, "value")
        || !backend_emit_line(backend, "return value;")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")
        || !backend_emit_line(backend, "")
        || !backend_emit_line(backend,
                              arena_printf(backend->arena,
                                           backend->error,
                                           "static void %s(%s value) {",
                                           backend_struct_release_name(backend, decl->name),
                                           backend_c_type(backend, struct_type)))) {
        return false;
    }
    backend_indent_push(backend);
    if (!backend_emit_struct_release_body(backend, decl, "value")) {
        return false;
    }
    backend_indent_pop(backend);
    if (!backend_emit_line(backend, "}")) {
        return false;
    }

    while (field_index < struct_field_decl_list_len(&decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&decl->as.struct_decl.fields, field_index);

        if (!backend_emit_line(backend, "") || !backend_emit_struct_take_field_helper(backend, decl, field)) {
            return false;
        }
        field_index += 1U;
    }
    return backend_emit_line(backend, "");
}

bool backend_emit_struct_definitions(Backend *backend) {
    size_t decl_index = 0U;
    bool emitted_any = false;

    while (decl_index < decl_list_len(&backend->program->decls)) {
        const Decl *decl = decl_list_get(&backend->program->decls, decl_index);

        if (decl->kind == DECL_STRUCT) {
            size_t field_index = 0U;

            emitted_any = true;
            if (!backend_emit_line(backend,
                                   arena_printf(backend->arena,
                                                backend->error,
                                                "typedef struct %s {",
                                                backend_struct_type_name(backend, decl->name)))) {
                return false;
            }
            backend_indent_push(backend);
            while (field_index < struct_field_decl_list_len(&decl->as.struct_decl.fields)) {
                const StructFieldDecl *field = struct_field_decl_list_get_const(&decl->as.struct_decl.fields, field_index);

                if (!backend_emit_line(backend,
                                       arena_printf(backend->arena,
                                                    backend->error,
                                                    "%s %.*s;",
                                                    backend_c_type(backend, field->type),
                                                    (int)field->name.len,
                                                    field->name.data))) {
                    return false;
                }
                field_index += 1U;
            }
            backend_indent_pop(backend);
            if (!backend_emit_line(backend,
                                   arena_printf(backend->arena,
                                                backend->error,
                                                "} %s;",
                                                backend_struct_type_name(backend, decl->name)))
                || !backend_emit_line(backend, "")
                || !backend_emit_struct_helpers(backend, decl)) {
                return false;
            }
        }
        decl_index += 1U;
    }

    if (emitted_any) {
        return backend_emit_line(backend, "");
    }
    return true;
}

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
                                                         backend_c_type(backend, info->type),
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
        || !backend_emit_struct_definitions(&backend)
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
