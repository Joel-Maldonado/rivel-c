#include "semantic.h"

#include <stdint.h>

enum {
    VISIT_UNVISITED = 0,
    VISIT_VISITING = 1,
    VISIT_DONE = 2
};

typedef struct {
    Type type;
    bool is_mutable;
} BindingInfo;

typedef struct {
    StringMap names;
} Scope;

typedef struct {
    Program *program;
    SemanticContext *context;
    CompileError *error;
    Vec scopes;
} Analyzer;

static uintptr_t semantic_index_value(size_t index) {
    return (uintptr_t)(index + 1U);
}

static size_t semantic_map_index(uintptr_t value) {
    return (size_t)(value - 1U);
}

static ConstValue semantic_make_int(int64_t value) {
    ConstValue out;
    out.type.kind = TYPE_INT;
    out.int_value = value;
    out.bool_value = false;
    return out;
}

static ConstValue semantic_make_bool(bool value) {
    ConstValue out;
    out.type.kind = TYPE_BOOL;
    out.int_value = 0;
    out.bool_value = value;
    return out;
}

static bool analyzer_push_scope(Analyzer *analyzer) {
    Scope *scope = (Scope *)vec_push(&analyzer->scopes, analyzer->error);
    if (scope == NULL) {
        return false;
    }
    strmap_init(&scope->names);
    return true;
}

static void analyzer_pop_scope(Analyzer *analyzer) {
    Scope *scope;
    if (analyzer->scopes.len == 0U) {
        return;
    }
    scope = (Scope *)vec_get(&analyzer->scopes, analyzer->scopes.len - 1U);
    strmap_free(&scope->names);
    analyzer->scopes.len -= 1U;
}

static void analyzer_clear_scopes(Analyzer *analyzer) {
    while (analyzer->scopes.len > 0U) {
        analyzer_pop_scope(analyzer);
    }
}

static bool analyzer_require_type(Analyzer *analyzer, Token token, Type actual, Type expected, const char *message) {
    if (!type_equal(actual, expected)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "%s", message);
    }
    return true;
}

static bool analyzer_declare_local(Analyzer *analyzer, Token token, StrSlice name, Type type, bool is_mutable) {
    Scope *scope;
    BindingInfo *binding;

    scope = (Scope *)vec_get(&analyzer->scopes, analyzer->scopes.len - 1U);
    if (strmap_contains(&scope->names, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Binding `%.*s` is already declared in this scope", (int)name.len, name.data);
    }

    binding = (BindingInfo *)arena_alloc_zero(analyzer->context->arena, sizeof(*binding), _Alignof(BindingInfo), analyzer->error);
    if (binding == NULL) {
        return false;
    }
    binding->type = type;
    binding->is_mutable = is_mutable;

    return strmap_set(&scope->names, name, (uintptr_t)binding, false, analyzer->error);
}

static const BindingInfo *analyzer_resolve_local(const Analyzer *analyzer, StrSlice name) {
    size_t index = analyzer->scopes.len;

    while (index > 0U) {
        Scope *scope = (Scope *)vec_get(&analyzer->scopes, index - 1U);
        uintptr_t value;
        if (strmap_get(&scope->names, name, &value)) {
            return (const BindingInfo *)value;
        }
        index -= 1U;
    }

    return NULL;
}

static GlobalConstInfo *analyzer_lookup_global_mut(Analyzer *analyzer, StrSlice name) {
    uintptr_t value;
    if (!strmap_get(&analyzer->context->global_names, name, &value)) {
        return NULL;
    }
    return (GlobalConstInfo *)vec_get(&analyzer->context->globals, semantic_map_index(value));
}

static const GlobalConstInfo *analyzer_lookup_global(const Analyzer *analyzer, StrSlice name) {
    uintptr_t value;
    if (!strmap_get(&analyzer->context->global_names, name, &value)) {
        return NULL;
    }
    return (const GlobalConstInfo *)vec_get(&analyzer->context->globals, semantic_map_index(value));
}

static const FunctionInfo *analyzer_lookup_function(const Analyzer *analyzer, StrSlice name) {
    uintptr_t value;
    if (!strmap_get(&analyzer->context->function_names, name, &value)) {
        return NULL;
    }
    return (const FunctionInfo *)vec_get(&analyzer->context->functions, semantic_map_index(value));
}

static const BuiltinInfo *analyzer_lookup_builtin(const Analyzer *analyzer, StrSlice name) {
    uintptr_t value;
    if (!strmap_get(&analyzer->context->builtin_names, name, &value)) {
        return NULL;
    }
    return (const BuiltinInfo *)vec_get(&analyzer->context->builtins, semantic_map_index(value));
}

static bool analyzer_register_builtins(Analyzer *analyzer) {
    BuiltinInfo *builtin = (BuiltinInfo *)vec_push(&analyzer->context->builtins, analyzer->error);
    if (builtin == NULL) {
        return false;
    }
    builtin->kind = BUILTIN_PRINT;
    return strmap_set(&analyzer->context->builtin_names, slice_from_cstr("print"), semantic_index_value(0U), false, analyzer->error);
}

static bool analyzer_ensure_unique_top_level_name(Analyzer *analyzer, Token token, StrSlice name) {
    if (strmap_contains(&analyzer->context->builtin_names, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Top-level name `%.*s` is reserved for a builtin", (int)name.len, name.data);
    }
    if (strmap_contains(&analyzer->context->global_names, name) || strmap_contains(&analyzer->context->function_names, name)) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Top-level name `%.*s` is already declared", (int)name.len, name.data);
    }
    return true;
}

static bool analyzer_collect_top_level_declarations(Analyzer *analyzer) {
    size_t index = 0U;

    while (index < analyzer->program->decls.len) {
        Decl *decl = *(Decl **)vec_get(&analyzer->program->decls, index);

        if (decl->kind == DECL_GLOBAL_CONST) {
            GlobalConstInfo *info;
            if (!analyzer_ensure_unique_top_level_name(analyzer, decl->token, decl->name)) {
                return false;
            }
            info = (GlobalConstInfo *)vec_push(&analyzer->context->globals, analyzer->error);
            if (info == NULL) {
                return false;
            }
            info->decl = decl;
            info->type.kind = TYPE_INT;
            info->value = semantic_make_int(0);
            info->visit_state = VISIT_UNVISITED;
            if (!strmap_set(&analyzer->context->global_names, decl->name, semantic_index_value(analyzer->context->globals.len - 1U), false, analyzer->error)) {
                return false;
            }
        } else if (decl->kind == DECL_FUNCTION) {
            FunctionInfo *info;
            if (!analyzer_ensure_unique_top_level_name(analyzer, decl->token, decl->name)) {
                return false;
            }
            info = (FunctionInfo *)vec_push(&analyzer->context->functions, analyzer->error);
            if (info == NULL) {
                return false;
            }
            info->decl = decl;
            if (!strmap_set(&analyzer->context->function_names, decl->name, semantic_index_value(analyzer->context->functions.len - 1U), false, analyzer->error)) {
                return false;
            }
        }

        index += 1U;
    }

    return true;
}

static bool analyzer_evaluate_const_expr(Analyzer *analyzer, Expr *expr, ConstValue *out_value);

static bool analyzer_evaluate_global_constant(Analyzer *analyzer, StrSlice name, ConstValue *out_value) {
    GlobalConstInfo *info = analyzer_lookup_global_mut(analyzer, name);
    ConstValue value;

    if (info->visit_state == VISIT_DONE) {
        *out_value = info->value;
        return true;
    }
    if (info->visit_state == VISIT_VISITING) {
        return error_set_at(analyzer->error, "Semantic", info->decl->token.line, info->decl->token.column, "Cyclic top-level constant definition involving `%.*s`", (int)name.len, name.data);
    }

    info->visit_state = VISIT_VISITING;
    if (!analyzer_evaluate_const_expr(analyzer, info->decl->as.global_const.initializer, &value)) {
        return false;
    }

    if (info->decl->as.global_const.has_annotation && !type_equal(info->decl->as.global_const.annotation, value.type)) {
        return error_set_at(analyzer->error, "Semantic", info->decl->token.line, info->decl->token.column,
                            "Top-level constant `%.*s` is declared as %s but initializes to %s",
                            (int)name.len, name.data,
                            type_display_name(info->decl->as.global_const.annotation),
                            type_display_name(value.type));
    }

    info->type = info->decl->as.global_const.has_annotation ? info->decl->as.global_const.annotation : value.type;
    info->value = value;
    info->decl->as.global_const.initializer->has_inferred_type = true;
    info->decl->as.global_const.initializer->inferred_type = info->type;
    info->visit_state = VISIT_DONE;
    *out_value = value;
    return true;
}

static bool analyzer_evaluate_binary_const_expr(Analyzer *analyzer, Expr *expr, ConstValue lhs, ConstValue rhs, ConstValue *out_value) {
    Type int_type;
    Type bool_type;

    int_type.kind = TYPE_INT;
    bool_type.kind = TYPE_BOOL;

    switch (expr->as.binary.op) {
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
            if (!analyzer_require_type(analyzer, expr->token, lhs.type, int_type, "Arithmetic operators expect Int operands")) {
                return false;
            }
            if (!analyzer_require_type(analyzer, expr->token, rhs.type, int_type, "Arithmetic operators expect Int operands")) {
                return false;
            }
            expr->has_inferred_type = true;
            expr->inferred_type = int_type;
            if (expr->as.binary.op == TOKEN_PLUS) {
                *out_value = semantic_make_int(lhs.int_value + rhs.int_value);
                return true;
            }
            if (expr->as.binary.op == TOKEN_MINUS) {
                *out_value = semantic_make_int(lhs.int_value - rhs.int_value);
                return true;
            }
            if (expr->as.binary.op == TOKEN_STAR) {
                *out_value = semantic_make_int(lhs.int_value * rhs.int_value);
                return true;
            }
            if (rhs.int_value == 0) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Division by zero in constant expression");
            }
            if (expr->as.binary.op == TOKEN_SLASH) {
                *out_value = semantic_make_int(lhs.int_value / rhs.int_value);
                return true;
            }
            *out_value = semantic_make_int(lhs.int_value % rhs.int_value);
            return true;
        case TOKEN_LESS:
        case TOKEN_LESS_EQ:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQ:
            if (!analyzer_require_type(analyzer, expr->token, lhs.type, int_type, "Comparison operators expect Int operands")) {
                return false;
            }
            if (!analyzer_require_type(analyzer, expr->token, rhs.type, int_type, "Comparison operators expect Int operands")) {
                return false;
            }
            expr->has_inferred_type = true;
            expr->inferred_type = bool_type;
            if (expr->as.binary.op == TOKEN_LESS) {
                *out_value = semantic_make_bool(lhs.int_value < rhs.int_value);
                return true;
            }
            if (expr->as.binary.op == TOKEN_LESS_EQ) {
                *out_value = semantic_make_bool(lhs.int_value <= rhs.int_value);
                return true;
            }
            if (expr->as.binary.op == TOKEN_GREATER) {
                *out_value = semantic_make_bool(lhs.int_value > rhs.int_value);
                return true;
            }
            *out_value = semantic_make_bool(lhs.int_value >= rhs.int_value);
            return true;
        case TOKEN_EQ_EQ:
        case TOKEN_BANG_EQ:
            if (!type_equal(lhs.type, rhs.type)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Equality operators require matching operand types");
            }
            expr->has_inferred_type = true;
            expr->inferred_type = bool_type;
            if (lhs.type.kind == TYPE_INT) {
                *out_value = semantic_make_bool(expr->as.binary.op == TOKEN_EQ_EQ ? lhs.int_value == rhs.int_value : lhs.int_value != rhs.int_value);
                return true;
            }
            *out_value = semantic_make_bool(expr->as.binary.op == TOKEN_EQ_EQ ? lhs.bool_value == rhs.bool_value : lhs.bool_value != rhs.bool_value);
            return true;
        default:
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unsupported constant expression");
    }
}

static bool analyzer_evaluate_const_expr(Analyzer *analyzer, Expr *expr, ConstValue *out_value) {
    if (expr->kind == EXPR_INT) {
        expr->has_inferred_type = true;
        expr->inferred_type.kind = TYPE_INT;
        *out_value = semantic_make_int(expr->as.int_value);
        return true;
    }
    if (expr->kind == EXPR_BOOL) {
        expr->has_inferred_type = true;
        expr->inferred_type.kind = TYPE_BOOL;
        *out_value = semantic_make_bool(expr->as.bool_value);
        return true;
    }
    if (expr->kind == EXPR_NAME) {
        ConstValue value;
        if (analyzer_lookup_global(analyzer, expr->as.name) == NULL) {
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Top-level constants can only reference other top-level constants");
        }
        if (!analyzer_evaluate_global_constant(analyzer, expr->as.name, &value)) {
            return false;
        }
        expr->has_inferred_type = true;
        expr->inferred_type = value.type;
        *out_value = value;
        return true;
    }
    if (expr->kind == EXPR_CALL) {
        return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Top-level constants cannot call functions");
    }
    if (expr->kind == EXPR_UNARY) {
        ConstValue operand;
        if (!analyzer_evaluate_const_expr(analyzer, expr->as.unary.operand, &operand)) {
            return false;
        }
        if (expr->as.unary.op == TOKEN_MINUS) {
            Type int_type;
            int_type.kind = TYPE_INT;
            if (!analyzer_require_type(analyzer, expr->token, operand.type, int_type, "Unary `-` expects Int")) {
                return false;
            }
            expr->has_inferred_type = true;
            expr->inferred_type.kind = TYPE_INT;
            *out_value = semantic_make_int(-operand.int_value);
            return true;
        }
        if (!analyzer_require_type(analyzer, expr->token, operand.type, (Type){TYPE_BOOL}, "`not` expects Bool")) {
            return false;
        }
        expr->has_inferred_type = true;
        expr->inferred_type.kind = TYPE_BOOL;
        *out_value = semantic_make_bool(!operand.bool_value);
        return true;
    }
    if (expr->kind == EXPR_BINARY) {
        ConstValue lhs;
        ConstValue rhs;
        if (!analyzer_evaluate_const_expr(analyzer, expr->as.binary.lhs, &lhs)) {
            return false;
        }
        if (!analyzer_evaluate_const_expr(analyzer, expr->as.binary.rhs, &rhs)) {
            return false;
        }
        if (expr->as.binary.op == TOKEN_KW_AND) {
            if (!analyzer_require_type(analyzer, expr->token, lhs.type, (Type){TYPE_BOOL}, "`and` expects Bool operands")) {
                return false;
            }
            if (!analyzer_require_type(analyzer, expr->token, rhs.type, (Type){TYPE_BOOL}, "`and` expects Bool operands")) {
                return false;
            }
            expr->has_inferred_type = true;
            expr->inferred_type.kind = TYPE_BOOL;
            *out_value = semantic_make_bool(lhs.bool_value && rhs.bool_value);
            return true;
        }
        if (expr->as.binary.op == TOKEN_KW_OR) {
            if (!analyzer_require_type(analyzer, expr->token, lhs.type, (Type){TYPE_BOOL}, "`or` expects Bool operands")) {
                return false;
            }
            if (!analyzer_require_type(analyzer, expr->token, rhs.type, (Type){TYPE_BOOL}, "`or` expects Bool operands")) {
                return false;
            }
            expr->has_inferred_type = true;
            expr->inferred_type.kind = TYPE_BOOL;
            *out_value = semantic_make_bool(lhs.bool_value || rhs.bool_value);
            return true;
        }
        return analyzer_evaluate_binary_const_expr(analyzer, expr, lhs, rhs, out_value);
    }

    return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unsupported constant expression");
}

static bool analyzer_analyze_expr(Analyzer *analyzer, Expr *expr, Type *out_type);

static bool analyzer_analyze_binary_expr(Analyzer *analyzer, Token token, TokenType op, Type lhs, Type rhs, Type *out_type) {
    Type int_type;
    Type bool_type;

    int_type.kind = TYPE_INT;
    bool_type.kind = TYPE_BOOL;

    switch (op) {
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
            if (lhs.kind != TYPE_INT || rhs.kind != TYPE_INT) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Operator %s expects Int operands", token_name(op));
            }
            *out_type = int_type;
            return true;
        case TOKEN_LESS:
        case TOKEN_LESS_EQ:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQ:
            if (lhs.kind != TYPE_INT || rhs.kind != TYPE_INT) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Comparison operators expect Int operands");
            }
            *out_type = bool_type;
            return true;
        case TOKEN_EQ_EQ:
        case TOKEN_BANG_EQ:
            if (!type_equal(lhs, rhs)) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Equality operators require matching operand types");
            }
            *out_type = bool_type;
            return true;
        case TOKEN_KW_AND:
        case TOKEN_KW_OR:
            if (lhs.kind != TYPE_BOOL || rhs.kind != TYPE_BOOL) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Logical operators expect Bool operands");
            }
            *out_type = bool_type;
            return true;
        default:
            return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Unsupported operator");
    }
}

static bool analyzer_analyze_builtin_call(Analyzer *analyzer, Expr *call_expr, BuiltinInfo builtin, bool allow_statement_only_builtins, Type *out_type) {
    if (builtin.kind == BUILTIN_PRINT) {
        Type arg_type;
        if (!allow_statement_only_builtins) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` cannot be used as an expression");
        }
        if (call_expr->as.call.args.len != 1U) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` expects 1 argument(s)");
        }
        if (!analyzer_analyze_expr(analyzer, *(Expr **)vec_get(&call_expr->as.call.args, 0U), &arg_type)) {
            return false;
        }
        if (arg_type.kind != TYPE_INT && arg_type.kind != TYPE_BOOL) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` does not support argument type %s", type_display_name(arg_type));
        }
        out_type->kind = TYPE_INT;
        return true;
    }

    return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Unknown builtin `%.*s`", (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data);
}

static bool analyzer_analyze_call(Analyzer *analyzer, Expr *call_expr, bool allow_statement_only_builtins, Type *out_type) {
    const BuiltinInfo *builtin = analyzer_lookup_builtin(analyzer, call_expr->as.call.callee);
    const FunctionInfo *function;
    size_t index;

    if (builtin != NULL) {
        return analyzer_analyze_builtin_call(analyzer, call_expr, *builtin, allow_statement_only_builtins, out_type);
    }

    function = analyzer_lookup_function(analyzer, call_expr->as.call.callee);
    if (function == NULL) {
        return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Unknown function `%.*s`", (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data);
    }
    if (call_expr->as.call.args.len != function->decl->as.function.params.len) {
        return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Function `%.*s` expects %zu argument(s)", (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data, function->decl->as.function.params.len);
    }

    index = 0U;
    while (index < call_expr->as.call.args.len) {
        Expr *arg = *(Expr **)vec_get(&call_expr->as.call.args, index);
        Param *param = (Param *)vec_get(&function->decl->as.function.params, index);
        Type arg_type;
        if (!analyzer_analyze_expr(analyzer, arg, &arg_type)) {
            return false;
        }
        if (!type_equal(arg_type, param->type)) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column,
                                "Argument %zu to `%.*s` has type %s, expected %s",
                                index + 1U, (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data,
                                type_display_name(arg_type), type_display_name(param->type));
        }
        index += 1U;
    }

    *out_type = function->decl->as.function.return_type;
    return true;
}

static bool analyzer_analyze_expr(Analyzer *analyzer, Expr *expr, Type *out_type) {
    if (expr->has_inferred_type) {
        *out_type = expr->inferred_type;
        return true;
    }
    if (expr->kind == EXPR_INT) {
        expr->has_inferred_type = true;
        expr->inferred_type.kind = TYPE_INT;
        *out_type = expr->inferred_type;
        return true;
    }
    if (expr->kind == EXPR_BOOL) {
        expr->has_inferred_type = true;
        expr->inferred_type.kind = TYPE_BOOL;
        *out_type = expr->inferred_type;
        return true;
    }
    if (expr->kind == EXPR_NAME) {
        const BindingInfo *binding = analyzer_resolve_local(analyzer, expr->as.name);
        const GlobalConstInfo *global;

        if (binding != NULL) {
            expr->has_inferred_type = true;
            expr->inferred_type = binding->type;
            *out_type = binding->type;
            return true;
        }
        global = analyzer_lookup_global(analyzer, expr->as.name);
        if (global != NULL) {
            expr->has_inferred_type = true;
            expr->inferred_type = global->type;
            *out_type = global->type;
            return true;
        }
        return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unknown binding `%.*s`", (int)expr->as.name.len, expr->as.name.data);
    }
    if (expr->kind == EXPR_CALL) {
        if (!analyzer_analyze_call(analyzer, expr, false, out_type)) {
            return false;
        }
        expr->has_inferred_type = true;
        expr->inferred_type = *out_type;
        return true;
    }
    if (expr->kind == EXPR_UNARY) {
        Type operand_type;
        if (!analyzer_analyze_expr(analyzer, expr->as.unary.operand, &operand_type)) {
            return false;
        }
        if (expr->as.unary.op == TOKEN_MINUS) {
            if (operand_type.kind != TYPE_INT) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unary `-` expects Int");
            }
            expr->has_inferred_type = true;
            expr->inferred_type.kind = TYPE_INT;
            *out_type = expr->inferred_type;
            return true;
        }
        if (operand_type.kind != TYPE_BOOL) {
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "`not` expects Bool");
        }
        expr->has_inferred_type = true;
        expr->inferred_type.kind = TYPE_BOOL;
        *out_type = expr->inferred_type;
        return true;
    }
    if (expr->kind == EXPR_BINARY) {
        Type lhs;
        Type rhs;
        if (!analyzer_analyze_expr(analyzer, expr->as.binary.lhs, &lhs)) {
            return false;
        }
        if (!analyzer_analyze_expr(analyzer, expr->as.binary.rhs, &rhs)) {
            return false;
        }
        if (!analyzer_analyze_binary_expr(analyzer, expr->token, expr->as.binary.op, lhs, rhs, out_type)) {
            return false;
        }
        expr->has_inferred_type = true;
        expr->inferred_type = *out_type;
        return true;
    }

    return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unknown expression type");
}

static bool analyzer_analyze_stmt(Analyzer *analyzer, Stmt *stmt, Type function_return_type);

static bool analyzer_analyze_block(Analyzer *analyzer, Block *block, Type function_return_type) {
    size_t index = 0U;
    if (!analyzer_push_scope(analyzer)) {
        return false;
    }
    while (index < block->statements.len) {
        Stmt *stmt = *(Stmt **)vec_get(&block->statements, index);
        if (!analyzer_analyze_stmt(analyzer, stmt, function_return_type)) {
            return false;
        }
        index += 1U;
    }
    analyzer_pop_scope(analyzer);
    return true;
}

static bool analyzer_analyze_stmt(Analyzer *analyzer, Stmt *stmt, Type function_return_type) {
    if (stmt->kind == STMT_BINDING) {
        Type initializer_type;
        Type binding_type;
        if (!analyzer_analyze_expr(analyzer, stmt->as.binding.initializer, &initializer_type)) {
            return false;
        }
        binding_type = stmt->as.binding.has_annotation ? stmt->as.binding.annotation : initializer_type;
        if (stmt->as.binding.has_annotation && !type_equal(stmt->as.binding.annotation, initializer_type)) {
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column,
                                "Binding `%.*s` is declared as %s but initializes to %s",
                                (int)stmt->as.binding.name.len, stmt->as.binding.name.data,
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
        if (!type_equal(value_type, binding->type)) {
            return error_set_at(analyzer->error, "Semantic", stmt->token.line, stmt->token.column, "Cannot assign value of type %s to %s", type_display_name(value_type), type_display_name(binding->type));
        }
        return true;
    }
    if (stmt->kind == STMT_RETURN) {
        Type value_type;
        if (!analyzer_analyze_expr(analyzer, stmt->as.ret.value, &value_type)) {
            return false;
        }
        if (!type_equal(value_type, function_return_type)) {
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
        while (index < stmt->as.if_stmt.elif_branches.len) {
            IfBranch *branch = (IfBranch *)vec_get(&stmt->as.if_stmt.elif_branches, index);
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

static bool analyzer_stmt_guarantees_return(const Stmt *stmt);

static bool analyzer_block_guarantees_return(const Block *block) {
    size_t index = 0U;
    while (index < block->statements.len) {
        const Stmt *stmt = *(Stmt *const *)vec_get(&block->statements, index);
        if (analyzer_stmt_guarantees_return(stmt)) {
            return true;
        }
        index += 1U;
    }
    return false;
}

static bool analyzer_stmt_guarantees_return(const Stmt *stmt) {
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
    while (index < stmt->as.if_stmt.elif_branches.len) {
        const IfBranch *branch = (const IfBranch *)vec_get(&stmt->as.if_stmt.elif_branches, index);
        if (!analyzer_block_guarantees_return(branch->body)) {
            return false;
        }
        index += 1U;
    }
    return analyzer_block_guarantees_return(stmt->as.if_stmt.else_block);
}

static bool analyzer_analyze_function(Analyzer *analyzer, Decl *function_decl) {
    size_t index = 0U;

    analyzer_clear_scopes(analyzer);
    if (!analyzer_push_scope(analyzer)) {
        return false;
    }
    while (index < function_decl->as.function.params.len) {
        Param *param = (Param *)vec_get(&function_decl->as.function.params, index);
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

static bool analyzer_validate_main_signature(Analyzer *analyzer) {
    const FunctionInfo *main_fn = analyzer_lookup_function(analyzer, slice_from_cstr("main"));
    if (main_fn == NULL) {
        return error_set(analyzer->error, "Semantic", "missing entrypoint `main`");
    }
    if (main_fn->decl->as.function.params.len != 0U) {
        return error_set_at(analyzer->error, "Semantic", main_fn->decl->token.line, main_fn->decl->token.column, "`main` must not take parameters");
    }
    if (main_fn->decl->as.function.return_type.kind != TYPE_INT) {
        return error_set_at(analyzer->error, "Semantic", main_fn->decl->token.line, main_fn->decl->token.column, "`main` must return Int");
    }
    return true;
}

void semantic_context_init(SemanticContext *context, Arena *arena) {
    context->arena = arena;
    vec_init(&context->globals, sizeof(GlobalConstInfo), arena);
    vec_init(&context->functions, sizeof(FunctionInfo), arena);
    vec_init(&context->builtins, sizeof(BuiltinInfo), arena);
    strmap_init(&context->global_names);
    strmap_init(&context->function_names);
    strmap_init(&context->builtin_names);
}

void semantic_context_free(SemanticContext *context) {
    strmap_free(&context->global_names);
    strmap_free(&context->function_names);
    strmap_free(&context->builtin_names);
}

bool semantic_analyze(Program *program, SemanticContext *context, CompileError *error) {
    Analyzer analyzer;
    size_t index = 0U;

    analyzer.program = program;
    analyzer.context = context;
    analyzer.error = error;
    vec_init(&analyzer.scopes, sizeof(Scope), NULL);

    if (!analyzer_register_builtins(&analyzer)
        || !analyzer_collect_top_level_declarations(&analyzer)) {
        vec_free(&analyzer.scopes);
        return false;
    }

    while (index < context->globals.len) {
        GlobalConstInfo *info = (GlobalConstInfo *)vec_get(&context->globals, index);
        ConstValue value;
        if (!analyzer_evaluate_global_constant(&analyzer, info->decl->name, &value)) {
            analyzer_clear_scopes(&analyzer);
            vec_free(&analyzer.scopes);
            return false;
        }
        index += 1U;
    }

    index = 0U;
    while (index < context->functions.len) {
        FunctionInfo *info = (FunctionInfo *)vec_get(&context->functions, index);
        if (!analyzer_analyze_function(&analyzer, info->decl)) {
            analyzer_clear_scopes(&analyzer);
            vec_free(&analyzer.scopes);
            return false;
        }
        index += 1U;
    }

    if (!analyzer_validate_main_signature(&analyzer)) {
        analyzer_clear_scopes(&analyzer);
        vec_free(&analyzer.scopes);
        return false;
    }

    analyzer_clear_scopes(&analyzer);
    vec_free(&analyzer.scopes);
    return true;
}

const GlobalConstInfo *semantic_lookup_global(const SemanticContext *context, StrSlice name) {
    uintptr_t value;
    if (!strmap_get(&context->global_names, name, &value)) {
        return NULL;
    }
    return (const GlobalConstInfo *)vec_get(&context->globals, semantic_map_index(value));
}

const FunctionInfo *semantic_lookup_function(const SemanticContext *context, StrSlice name) {
    uintptr_t value;
    if (!strmap_get(&context->function_names, name, &value)) {
        return NULL;
    }
    return (const FunctionInfo *)vec_get(&context->functions, semantic_map_index(value));
}

const BuiltinInfo *semantic_lookup_builtin(const SemanticContext *context, StrSlice name) {
    uintptr_t value;
    if (!strmap_get(&context->builtin_names, name, &value)) {
        return NULL;
    }
    return (const BuiltinInfo *)vec_get(&context->builtins, semantic_map_index(value));
}
