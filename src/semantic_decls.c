#include "semantic_internal.h"

static bool analyzer_ensure_unique_top_level_name(Analyzer *analyzer, Token token, StrSlice name) {
    if (semantic_symbol_table_contains(&analyzer->result->builtin_names, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Top-level name `%.*s` is reserved for a builtin", (int)name.len, name.data);
    }
    if (semantic_symbol_table_contains(&analyzer->result->global_names, name)
        || semantic_symbol_table_contains(&analyzer->result->function_names, name)
        || semantic_symbol_table_contains(&analyzer->result->struct_names, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Top-level name `%.*s` is already declared", (int)name.len, name.data);
    }
    return true;
}

static bool analyzer_validate_declared_type(Analyzer *analyzer, Token token, Type type) {
    if (type.kind != TYPE_STRUCT) {
        return true;
    }
    if (analyzer_lookup_struct(analyzer, type.struct_name) != NULL) {
        return true;
    }
    return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Unknown struct type `%.*s`", (int)type.struct_name.len, type.struct_name.data);
}

static bool analyzer_validate_struct_decl(Analyzer *analyzer, const Decl *decl) {
    size_t field_index = 0U;

    while (field_index < struct_field_decl_list_len(&decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&decl->as.struct_decl.fields, field_index);
        size_t prior_index = 0U;

        while (prior_index < field_index) {
            const StructFieldDecl *prior_field = struct_field_decl_list_get_const(&decl->as.struct_decl.fields, prior_index);

            if (slice_equal(prior_field->name, field->name)) {
                return error_set_at(analyzer->error,
                                    "Semantic",
                                    field->token.line,
                                    field->token.column,
                                    "Struct `%.*s` already has a field named `%.*s`",
                                    (int)decl->name.len,
                                    decl->name.data,
                                    (int)field->name.len,
                                    field->name.data);
            }
            prior_index += 1U;
        }

        if (field->type.kind == TYPE_STRUCT && slice_equal(field->type.struct_name, decl->name)) {
            return error_set_at(analyzer->error,
                                "Semantic",
                                field->token.line,
                                field->token.column,
                                "Recursive struct definitions are not supported");
        }
        if (!analyzer_validate_declared_type(analyzer, field->token, field->type)) {
            return false;
        }
        field_index += 1U;
    }

    return true;
}

static bool analyzer_struct_declared_before(const Analyzer *analyzer, const Decl *decl, StrSlice struct_name) {
    size_t index = 0U;

    while (index < decl_list_len(&analyzer->program->decls)) {
        const Decl *candidate = decl_list_get(&analyzer->program->decls, index);

        if (candidate == decl) {
            return false;
        }
        if (candidate->kind == DECL_STRUCT && slice_equal(candidate->name, struct_name)) {
            return true;
        }
        index += 1U;
    }

    return false;
}

static bool analyzer_validate_struct_field_types(Analyzer *analyzer, const Decl *decl) {
    size_t field_index = 0U;

    while (field_index < struct_field_decl_list_len(&decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&decl->as.struct_decl.fields, field_index);

        if (field->type.kind == TYPE_STRUCT) {
            if (slice_equal(field->type.struct_name, decl->name)) {
                return error_set_at(analyzer->error, "Semantic", field->token.line, field->token.column, "Recursive struct definitions are not supported");
            }
            if (analyzer_lookup_struct(analyzer, field->type.struct_name) == NULL) {
                return error_set_at(analyzer->error,
                                    "Semantic",
                                    field->token.line,
                                    field->token.column,
                                    "Unknown struct type `%.*s`",
                                    (int)field->type.struct_name.len,
                                    field->type.struct_name.data);
            }
            if (!analyzer_struct_declared_before(analyzer, decl, field->type.struct_name)) {
                return error_set_at(analyzer->error,
                                    "Semantic",
                                    field->token.line,
                                    field->token.column,
                                    "Struct fields may only reference previously declared structs");
            }
        }
        field_index += 1U;
    }

    return true;
}

bool analyzer_register_builtins(Analyzer *analyzer) {
    static const struct {
        const char *name;
        BuiltinKind kind;
    } builtin_specs[] = {
        {"print", BUILTIN_PRINT},
        {"len", BUILTIN_LEN},
        {"substr", BUILTIN_SUBSTR},
        {"contains", BUILTIN_CONTAINS},
        {"starts_with", BUILTIN_STARTS_WITH},
        {"ends_with", BUILTIN_ENDS_WITH},
    };
    size_t index = 0U;

    while (index < sizeof(builtin_specs) / sizeof(builtin_specs[0])) {
        SemanticBuiltinInfo *builtin = semantic_builtin_table_push(&analyzer->result->builtins, analyzer->error);

        if (builtin == NULL) {
            return false;
        }
        builtin->kind = builtin_specs[index].kind;
        if (!semantic_symbol_table_set(&analyzer->result->builtin_names,
                                       slice_from_cstr(builtin_specs[index].name),
                                       semantic_builtin_table_len(&analyzer->result->builtins) - 1U,
                                       analyzer->error)) {
            return false;
        }
        index += 1U;
    }

    return true;
}

bool analyzer_collect_top_level_declarations(Analyzer *analyzer) {
    size_t index = 0U;

    while (index < decl_list_len(&analyzer->program->decls)) {
        Decl *decl = decl_list_get(&analyzer->program->decls, index);

        if (decl->kind == DECL_GLOBAL_CONST) {
            SemanticGlobalRecord *record;

            if (!analyzer_ensure_unique_top_level_name(analyzer, decl->token, decl->name)) {
                return false;
            }

            record = semantic_global_table_push(&analyzer->result->globals, analyzer->error);
            if (record == NULL) {
                return false;
            }

            record->info.decl = decl;
            record->info.type = type_make_int();
            record->info.value = const_value_make_int(0);
            record->visit_state = GLOBAL_UNVISITED;
            if (!semantic_symbol_table_set(&analyzer->result->global_names, decl->name, semantic_global_table_len(&analyzer->result->globals) - 1U, analyzer->error)) {
                return false;
            }
        } else if (decl->kind == DECL_FUNCTION) {
            SemanticFunctionInfo *info;

            if (!analyzer_ensure_unique_top_level_name(analyzer, decl->token, decl->name)) {
                return false;
            }

            info = semantic_function_table_push(&analyzer->result->functions, analyzer->error);
            if (info == NULL) {
                return false;
            }

            info->decl = decl;
            if (!semantic_symbol_table_set(&analyzer->result->function_names, decl->name, semantic_function_table_len(&analyzer->result->functions) - 1U, analyzer->error)) {
                return false;
            }
        } else if (decl->kind == DECL_STRUCT) {
            SemanticStructInfo *info;

            if (!analyzer_ensure_unique_top_level_name(analyzer, decl->token, decl->name)) {
                return false;
            }

            info = semantic_struct_table_push(&analyzer->result->structs, analyzer->error);
            if (info == NULL) {
                return false;
            }

            info->decl = decl;
            if (!semantic_symbol_table_set(&analyzer->result->struct_names, decl->name, semantic_struct_table_len(&analyzer->result->structs) - 1U, analyzer->error)) {
                return false;
            }
        }

        index += 1U;
    }

    index = 0U;
    while (index < decl_list_len(&analyzer->program->decls)) {
        Decl *decl = decl_list_get(&analyzer->program->decls, index);

        if (decl->kind == DECL_GLOBAL_CONST) {
            if (decl->as.global_const.has_annotation
                && !analyzer_validate_declared_type(analyzer, decl->token, decl->as.global_const.annotation)) {
                return false;
            }
        } else if (decl->kind == DECL_FUNCTION) {
            size_t param_index = 0U;

            if (!analyzer_validate_declared_type(analyzer, decl->token, decl->as.function.return_type)) {
                return false;
            }
            while (param_index < param_list_len(&decl->as.function.params)) {
                const Param *param = param_list_get_const(&decl->as.function.params, param_index);

                if (!analyzer_validate_declared_type(analyzer, param->token, param->type)) {
                    return false;
                }
                param_index += 1U;
            }
        } else if (decl->kind == DECL_STRUCT) {
            if (!analyzer_validate_struct_decl(analyzer, decl) || !analyzer_validate_struct_field_types(analyzer, decl)) {
                return false;
            }
        }
        index += 1U;
    }

    return true;
}
