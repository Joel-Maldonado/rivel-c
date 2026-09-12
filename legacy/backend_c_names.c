#include "backend_c_internal.h"

#include <math.h>
#include <string.h>

bool backend_emit_line(Backend *backend, const char *line) {
    int depth = 0;

    if (line == NULL) {
        return false;
    }
    if (line[0] == '\0') {
        return strbuf_append_char(backend->output, '\n', backend->error);
    }
    while (depth < backend->indent) {
        if (!strbuf_append_cstr(backend->output, "    ", backend->error)) {
            return false;
        }
        depth += 1;
    }
    if (!strbuf_append_cstr(backend->output, line, backend->error)) {
        return false;
    }
    return strbuf_append_char(backend->output, '\n', backend->error);
}

void backend_indent_push(Backend *backend) {
    backend->indent += 1;
}

void backend_indent_pop(Backend *backend) {
    if (backend->indent > 0) {
        backend->indent -= 1;
    }
}

char *backend_struct_type_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "RivelStruct_%.*s", (int)name.len, name.data);
}

char *backend_struct_retain_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_struct_%.*s_retain", (int)name.len, name.data);
}

char *backend_struct_release_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_struct_%.*s_release", (int)name.len, name.data);
}

char *backend_struct_take_field_name(Backend *backend, StrSlice struct_name, StrSlice field_name) {
    return arena_printf(backend->arena,
                        backend->error,
                        "rivel_struct_%.*s_take_%.*s",
                        (int)struct_name.len,
                        struct_name.data,
                        (int)field_name.len,
                        field_name.data);
}

const SemanticStructInfo *backend_lookup_struct_checked(Backend *backend, StrSlice name) {
    const SemanticStructInfo *info = semantic_lookup_struct(backend->semantics, name);

    if (info != NULL) {
        return info;
    }
    error_set(backend->error, "Backend", "Internal error: unknown struct `%.*s` during C emission", (int)name.len, name.data);
    return NULL;
}

const StructFieldDecl *backend_lookup_struct_field(Backend *backend, StrSlice struct_name, StrSlice field_name) {
    const SemanticStructInfo *struct_info = backend_lookup_struct_checked(backend, struct_name);
    size_t index = 0U;

    if (struct_info == NULL) {
        return NULL;
    }
    while (index < struct_field_decl_list_len(&struct_info->decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&struct_info->decl->as.struct_decl.fields, index);

        if (slice_equal(field->name, field_name)) {
            return field;
        }
        index += 1U;
    }

    error_set(backend->error,
              "Backend",
              "Internal error: struct `%.*s` has no field `%.*s` during C emission",
              (int)struct_name.len,
              struct_name.data,
              (int)field_name.len,
              field_name.data);
    return NULL;
}

bool backend_type_contains_owned_strings(Backend *backend, Type type) {
    size_t index = 0U;
    const SemanticStructInfo *struct_info;

    if (type.kind == TYPE_STRING) {
        return true;
    }
    if (type.kind != TYPE_STRUCT) {
        return false;
    }
    struct_info = backend_lookup_struct_checked(backend, type.struct_name);
    if (struct_info == NULL) {
        return false;
    }
    while (index < struct_field_decl_list_len(&struct_info->decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&struct_info->decl->as.struct_decl.fields, index);

        if (backend_type_contains_owned_strings(backend, field->type)) {
            return true;
        }
        index += 1U;
    }
    return false;
}

char *backend_c_type(Backend *backend, Type type) {
    switch (type.kind) {
        case TYPE_INT:
            return arena_copy_cstr(backend->arena, "int64_t", backend->error);
        case TYPE_DOUBLE:
            return arena_copy_cstr(backend->arena, "double", backend->error);
        case TYPE_BOOL:
            return arena_copy_cstr(backend->arena, "bool", backend->error);
        case TYPE_STRING:
            return arena_copy_cstr(backend->arena, "RivelString", backend->error);
        case TYPE_STRUCT:
            return backend_struct_type_name(backend, type.struct_name);
    }
    return arena_copy_cstr(backend->arena, "<type>", backend->error);
}

char *backend_double_literal(Backend *backend, double value) {
    char *text;

    if (isnan(value)) {
        return arena_copy_cstr(backend->arena, "NAN", backend->error);
    }
    if (isinf(value)) {
        return arena_copy_cstr(backend->arena, signbit(value) ? "-INFINITY" : "INFINITY", backend->error);
    }

    text = arena_printf(backend->arena, backend->error, "%.17g", value);
    if (text == NULL) {
        return NULL;
    }
    if (strchr(text, '.') != NULL || strchr(text, 'e') != NULL || strchr(text, 'E') != NULL) {
        return text;
    }
    return arena_printf(backend->arena, backend->error, "%s.0", text);
}

char *backend_function_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_fn_%.*s", (int)name.len, name.data);
}

char *backend_global_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_global_%.*s", (int)name.len, name.data);
}

char *backend_param_name(Backend *backend, StrSlice name) {
    return arena_printf(backend->arena, backend->error, "rivel_param_%.*s", (int)name.len, name.data);
}

char *backend_local_name(Backend *backend, StrSlice name) {
    char *out = arena_printf(backend->arena, backend->error, "rivel_local_%.*s_%zu", (int)name.len, name.data, backend->next_local_id);

    backend->next_local_id += 1U;
    return out;
}

char *backend_temp_name(Backend *backend, const char *base) {
    char *out = arena_printf(backend->arena, backend->error, "rivel_%s_%zu", base, backend->next_local_id);

    backend->next_local_id += 1U;
    return out;
}

char *backend_resolve_name(Backend *backend, StrSlice name) {
    const LocalBinding *binding = backend_resolve_local(backend, name);

    if (binding != NULL) {
        return (char *)binding->c_name;
    }
    if (semantic_lookup_global(backend->semantics, name) != NULL) {
        return backend_global_name(backend, name);
    }
    error_set(backend->error, "Backend", "Internal error: unresolved name `%.*s` during C emission", (int)name.len, name.data);
    return NULL;
}

char *backend_retain_value_expr(Backend *backend, Type type, const char *value) {
    if (type.kind == TYPE_STRING) {
        return arena_printf(backend->arena, backend->error, "rivel_string_retain(%s)", value);
    }
    if (type.kind == TYPE_STRUCT && backend_type_contains_owned_strings(backend, type)) {
        return arena_printf(backend->arena,
                            backend->error,
                            "%s(%s)",
                            backend_struct_retain_name(backend, type.struct_name),
                            value);
    }
    return (char *)value;
}

bool backend_emit_release_value(Backend *backend, Type type, const char *value) {
    if (type.kind == TYPE_STRING) {
        return backend_emit_line(backend, arena_printf(backend->arena, backend->error, "rivel_string_release(%s);", value));
    }
    if (type.kind == TYPE_STRUCT && backend_type_contains_owned_strings(backend, type)) {
        return backend_emit_line(backend,
                                 arena_printf(backend->arena,
                                              backend->error,
                                              "%s(%s);",
                                              backend_struct_release_name(backend, type.struct_name),
                                              value));
    }
    return true;
}

bool backend_expr_type_checked(Backend *backend, const Expr *expr, Type *out_type) {
    if (!expr_resolved_type(expr, out_type)) {
        return error_set(backend->error, "Backend", "Internal error: missing semantic type for emitted expression");
    }
    return true;
}

char *backend_string_literal_value(Backend *backend, StrSlice value) {
    StrBuf buf;
    size_t index = 0U;
    char *copy;

    strbuf_init(&buf);
    if (!strbuf_append_char(&buf, '"', backend->error)) {
        strbuf_free(&buf);
        return NULL;
    }
    while (index < value.len) {
        unsigned char byte = (unsigned char)value.data[index];
        const char *escape = NULL;

        if (byte == '\\') escape = "\\\\";
        else if (byte == '"') escape = "\\\"";
        else if (byte == '\n') escape = "\\n";
        else if (byte == '\r') escape = "\\r";
        else if (byte == '\t') escape = "\\t";

        if (escape != NULL) {
            if (!strbuf_append_cstr(&buf, escape, backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (byte >= 32U && byte <= 126U) {
            if (!strbuf_append_char(&buf, (char)byte, backend->error)) {
                strbuf_free(&buf);
                return NULL;
            }
        } else if (!strbuf_append_fmt(&buf, backend->error, "\\x%02X", (unsigned int)byte)) {
            strbuf_free(&buf);
            return NULL;
        }
        index += 1U;
    }
    if (!strbuf_append_char(&buf, '"', backend->error)) {
        strbuf_free(&buf);
        return NULL;
    }

    copy = arena_copy_cstr(backend->arena, strbuf_cstr(&buf), backend->error);
    strbuf_free(&buf);
    return copy;
}

char *backend_literal(Backend *backend, ConstValue value) {
    if (value.type.kind == TYPE_BOOL) {
        return arena_copy_cstr(backend->arena, value.as.bool_value ? "true" : "false", backend->error);
    }
    if (value.type.kind == TYPE_DOUBLE) {
        return backend_double_literal(backend, value.as.double_value);
    }
    if (value.type.kind == TYPE_STRING) {
        char *literal = backend_string_literal_value(backend, value.as.string_value);

        if (literal == NULL) {
            return NULL;
        }
        return arena_printf(backend->arena, backend->error, "(RivelString){%s, %zu, NULL}", literal, value.as.string_value.len);
    }
    return arena_printf(backend->arena, backend->error, "INT64_C(%lld)", (long long)value.as.int_value);
}
