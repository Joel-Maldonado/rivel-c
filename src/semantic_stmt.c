#include "semantic_internal.h"

bool analyzer_analyze_block(Analyzer *analyzer, const Block *block, Type function_return_type) {
    size_t index = 0U;

    if (!analyzer_push_scope(analyzer)) {
        return false;
    }
    while (index < stmt_list_len(&block->statements)) {
        const Stmt *stmt = stmt_list_get(&block->statements, index);
        if (!analyzer_analyze_stmt(analyzer, stmt, function_return_type)) {
            return false;
        }
        index += 1U;
    }
    analyzer_pop_scope(analyzer);
    return true;
}

bool analyzer_analyze_stmt(Analyzer *analyzer, const Stmt *stmt, Type function_return_type) {
    if (stmt->kind == STMT_BINDING) {
        Type initializer_type;
        Type binding_type;

        if (!analyzer_analyze_expr(analyzer, stmt->as.binding.initializer, &initializer_type)) {
            return false;
        }
        binding_type = stmt->as.binding.has_annotation ? stmt->as.binding.annotation : initializer_type;
        if (stmt->as.binding.has_annotation && !type_can_widen_to(initializer_type, stmt->as.binding.annotation)) {
            return error_set_at(
                analyzer->error,
                "Semantic",
                stmt->token.line,
                stmt->token.column,
                "Binding `%.*s` is declared as %s but initializes to %s",
                (int)stmt->as.binding.name.len,
                stmt->as.binding.name.data,
                type_display_name(stmt->as.binding.annotation),
                type_display_name(initializer_type));
        }
        return analyzer_declare_local(analyzer, stmt->token, stmt->as.binding.name, binding_type, stmt->as.binding.is_mutable);
    }
    if (stmt->kind == STMT_ASSIGN) {
        const BindingInfo *binding = analyzer_resolve_local(analyzer, stmt->as.assign.name);
        Type value_type;

        if (binding == NULL) {
            if (analyzer_lookup_global(analyzer, stmt->as.assign.name) != NULL) {
                return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "Cannot assign to immutable binding `%.*s`", (int)stmt->as.assign.name.len, stmt->as.assign.name.data);
            }
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "Unknown binding `%.*s`", (int)stmt->as.assign.name.len, stmt->as.assign.name.data);
        }
        if (!binding->is_mutable) {
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "Cannot assign to immutable binding `%.*s`", (int)stmt->as.assign.name.len, stmt->as.assign.name.data);
        }
        if (!analyzer_analyze_expr(analyzer, stmt->as.assign.value, &value_type)) {
            return false;
        }
        if (!type_can_widen_to(value_type, binding->type)) {
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "Cannot assign value of type %s to %s", type_display_name(value_type), type_display_name(binding->type));
        }
        return true;
    }
    if (stmt->kind == STMT_RETURN) {
        Type value_type;

        if (!analyzer_analyze_expr(analyzer, stmt->as.ret.value, &value_type)) {
            return false;
        }
        if (!type_can_widen_to(value_type, function_return_type)) {
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "Return type mismatch: expected %s but got %s", type_display_name(function_return_type), type_display_name(value_type));
        }
        return true;
    }
    if (stmt->kind == STMT_CALL) {
        Type call_type;

        return analyzer_analyze_call(analyzer, stmt->as.call.call, true, &call_type);
    }
    if (stmt->kind == STMT_IF) {
        Type condition_type;
        size_t index = 0U;

        if (!analyzer_analyze_expr(analyzer, stmt->as.if_stmt.condition, &condition_type)) {
            return false;
        }
        if (condition_type.kind != TYPE_BOOL) {
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "If condition must be Bool");
        }
        if (!analyzer_analyze_block(analyzer, stmt->as.if_stmt.then_block, function_return_type)) {
            return false;
        }
        while (index < if_branch_list_len(&stmt->as.if_stmt.elif_branches)) {
            const IfBranch *branch = if_branch_list_get_const(&stmt->as.if_stmt.elif_branches, index);
            if (!analyzer_analyze_expr(analyzer, branch->condition, &condition_type)) {
                return false;
            }
            if (condition_type.kind != TYPE_BOOL) {
                return error_set_at(analyzer->error, "Semantic", branch->token.line, branch->token.column, "Elif condition must be Bool");
            }
            if (!analyzer_analyze_block(analyzer, branch->body, function_return_type)) {
                return false;
            }
            index += 1U;
        }
        if (stmt->as.if_stmt.else_block != NULL && !analyzer_analyze_block(analyzer, stmt->as.if_stmt.else_block, function_return_type)) {
            return false;
        }
        return true;
    }
    if (stmt->kind == STMT_WHILE) {
        Type condition_type;

        if (!analyzer_analyze_expr(analyzer, stmt->as.while_stmt.condition, &condition_type)) {
            return false;
        }
        if (condition_type.kind != TYPE_BOOL) {
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "While condition must be Bool");
        }
        return analyzer_analyze_block(analyzer, stmt->as.while_stmt.body, function_return_type);
    }

    return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "Unknown statement type");
}

bool analyzer_block_guarantees_return(const Block *block) {
    size_t index = 0U;

    while (index < stmt_list_len(&block->statements)) {
        const Stmt *stmt = stmt_list_get(&block->statements, index);
        if (analyzer_stmt_guarantees_return(stmt)) {
            return true;
        }
        index += 1U;
    }
    return false;
}

bool analyzer_stmt_guarantees_return(const Stmt *stmt) {
    size_t index = 0U;

    if (stmt->kind == STMT_RETURN) {
        return true;
    }
    if (stmt->kind != STMT_IF) {
        return false;
    }
    if (stmt->as.if_stmt.else_block == NULL) {
        return false;
    }
    if (!analyzer_block_guarantees_return(stmt->as.if_stmt.then_block)) {
        return false;
    }
    while (index < if_branch_list_len(&stmt->as.if_stmt.elif_branches)) {
        const IfBranch *branch = if_branch_list_get_const(&stmt->as.if_stmt.elif_branches, index);
        if (!analyzer_block_guarantees_return(branch->body)) {
            return false;
        }
        index += 1U;
    }
    return analyzer_block_guarantees_return(stmt->as.if_stmt.else_block);
}

bool analyzer_analyze_function(Analyzer *analyzer, const Decl *function_decl) {
    size_t index = 0U;

    analyzer_clear_scopes(analyzer);
    if (!analyzer_push_scope(analyzer)) {
        return false;
    }
    while (index < param_list_len(&function_decl->as.function.params)) {
        const Param *param = param_list_get_const(&function_decl->as.function.params, index);
        if (!analyzer_declare_local(analyzer, param->token, param->name, param->type, false)) {
            analyzer_clear_scopes(analyzer);
            return false;
        }
        index += 1U;
    }
    if (!analyzer_analyze_block(analyzer, function_decl->as.function.body, function_decl->as.function.return_type)) {
        analyzer_clear_scopes(analyzer);
        return false;
    }
    analyzer_clear_scopes(analyzer);
    if (!analyzer_block_guarantees_return(function_decl->as.function.body)) {
        return error_set_at(analyzer->error, "Semantic", function_decl->token.line, function_decl->token.column, "Function `%.*s` does not return on all paths", (int)function_decl->name.len, function_decl->name.data);
    }
    return true;
}

bool analyzer_validate_main_signature(Analyzer *analyzer) {
    const SemanticFunctionInfo *main_fn = semantic_lookup_function(analyzer->result, slice_from_cstr("main"));

    if (main_fn == NULL) {
        return error_set(analyzer->error, "Semantic", "missing entrypoint `main`");
    }
    if (param_list_len(&main_fn->decl->as.function.params) != 0U) {
        return error_set_at(analyzer->error, "Semantic", main_fn->decl->token.line, main_fn->decl->token.column, "`main` must not take parameters");
    }
    if (main_fn->decl->as.function.return_type.kind != TYPE_INT) {
        return error_set_at(analyzer->error, "Semantic", main_fn->decl->token.line, main_fn->decl->token.column, "`main` must return Int");
    }
    return true;
}
