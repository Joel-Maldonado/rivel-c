#include "semantic_internal.h"

static bool analyzer_analyze_binary_expr(Analyzer *analyzer, Token token, TokenType op, Type lhs, Type rhs, Type *out_type) {
    switch (op) {
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
            if (!type_is_numeric(lhs) || !type_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Operator %s expects Int operands", token_name(op));
            }
            out_type->kind = lhs.kind == TYPE_INT && rhs.kind == TYPE_INT ? TYPE_INT : TYPE_DOUBLE;
            return true;
        case TOKEN_SLASH:
            if (!type_is_numeric(lhs) || !type_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Operator %s expects Int operands", token_name(op));
            }
            out_type->kind = lhs.kind == TYPE_INT && rhs.kind == TYPE_INT ? TYPE_INT : TYPE_DOUBLE;
            return true;
        case TOKEN_PERCENT:
            if (lhs.kind != TYPE_INT || rhs.kind != TYPE_INT) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Operator %s expects Int operands", token_name(op));
            }
            out_type->kind = TYPE_INT;
            return true;
        case TOKEN_LESS:
        case TOKEN_LESS_EQ:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQ:
            if (!type_is_numeric(lhs) || !type_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Comparison operators expect Int operands");
            }
            out_type->kind = TYPE_BOOL;
            return true;
        case TOKEN_EQ_EQ:
        case TOKEN_BANG_EQ:
            if (!type_equal(lhs, rhs) && !(type_is_numeric(lhs) && type_is_numeric(rhs))) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Equality operators require matching operand types");
            }
            out_type->kind = TYPE_BOOL;
            return true;
        case TOKEN_KW_AND:
        case TOKEN_KW_OR:
            if (lhs.kind != TYPE_BOOL || rhs.kind != TYPE_BOOL) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Logical operators expect Bool operands");
            }
            out_type->kind = TYPE_BOOL;
            return true;
        default:
            return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Unsupported operator");
    }
}

static bool analyzer_analyze_builtin_call(Analyzer *analyzer, const Expr *call_expr, SemanticBuiltinInfo builtin, bool allow_statement_only_builtins, Type *out_type) {
    if (builtin.kind == BUILTIN_PRINT) {
        Type arg_type;

        if (!allow_statement_only_builtins) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` cannot be used as an expression");
        }
        if (expr_list_len(&call_expr->as.call.args) != 1U) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` expects 1 argument(s)");
        }
        if (!analyzer_analyze_expr(analyzer, expr_list_get(&call_expr->as.call.args, 0U), &arg_type)) {
            return false;
        }
        if (arg_type.kind != TYPE_INT && arg_type.kind != TYPE_DOUBLE && arg_type.kind != TYPE_BOOL) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` does not support argument type %s", type_display_name(arg_type));
        }
        out_type->kind = TYPE_INT;
        return true;
    }

    return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Unknown builtin `%.*s`", (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data);
}

bool analyzer_analyze_call(Analyzer *analyzer, const Expr *call_expr, bool allow_statement_only_builtins, Type *out_type) {
    const SemanticBuiltinInfo *builtin = analyzer_lookup_builtin(analyzer, call_expr->as.call.callee);
    const SemanticFunctionInfo *function;
    size_t index;

    if (builtin != NULL) {
        return analyzer_analyze_builtin_call(analyzer, call_expr, *builtin, allow_statement_only_builtins, out_type);
    }

    function = analyzer_lookup_function(analyzer, call_expr->as.call.callee);
    if (function == NULL) {
        return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Unknown function `%.*s`", (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data);
    }
    if (expr_list_len(&call_expr->as.call.args) != param_list_len(&function->decl->as.function.params)) {
        return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Function `%.*s` expects %zu argument(s)", (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data, param_list_len(&function->decl->as.function.params));
    }

    index = 0U;
    while (index < expr_list_len(&call_expr->as.call.args)) {
        const Expr *arg = expr_list_get(&call_expr->as.call.args, index);
        const Param *param = param_list_get_const(&function->decl->as.function.params, index);
        Type arg_type;

        if (!analyzer_analyze_expr(analyzer, arg, &arg_type)) {
            return false;
        }
        if (!type_can_widen_to(arg_type, param->type)) {
            return error_set_at(
                analyzer->error,
                "Semantic",
                call_expr->token.line,
                call_expr->token.column,
                "Argument %zu to `%.*s` has type %s, expected %s",
                index + 1U,
                (int)call_expr->as.call.callee.len,
                call_expr->as.call.callee.data,
                type_display_name(arg_type),
                type_display_name(param->type));
        }
        index += 1U;
    }

    *out_type = function->decl->as.function.return_type;
    return true;
}

bool analyzer_analyze_expr(Analyzer *analyzer, const Expr *expr, Type *out_type) {
    if (semantic_lookup_recorded_expr_type(analyzer->result, expr, out_type)) {
        return true;
    }

    if (expr->kind == EXPR_INT) {
        out_type->kind = TYPE_INT;
    } else if (expr->kind == EXPR_DOUBLE) {
        out_type->kind = TYPE_DOUBLE;
    } else if (expr->kind == EXPR_BOOL) {
        out_type->kind = TYPE_BOOL;
    } else if (expr->kind == EXPR_NAME) {
        const BindingInfo *binding = analyzer_resolve_local(analyzer, expr->as.name);
        const SemanticGlobalInfo *global;

        if (binding != NULL) {
            *out_type = binding->type;
        } else {
            global = analyzer_lookup_global(analyzer, expr->as.name);
            if (global == NULL) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unknown binding `%.*s`", (int)expr->as.name.len, expr->as.name.data);
            }
            *out_type = global->type;
        }
    } else if (expr->kind == EXPR_CALL) {
        if (!analyzer_analyze_call(analyzer, expr, false, out_type)) {
            return false;
        }
    } else if (expr->kind == EXPR_UNARY) {
        Type operand_type;

        if (!analyzer_analyze_expr(analyzer, expr->as.unary.operand, &operand_type)) {
            return false;
        }
        if (expr->as.unary.op == TOKEN_MINUS) {
            if (!type_is_numeric(operand_type)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unary `-` expects Int or Double");
            }
            *out_type = operand_type;
        } else {
            if (operand_type.kind != TYPE_BOOL) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "`not` expects Bool");
            }
            out_type->kind = TYPE_BOOL;
        }
    } else if (expr->kind == EXPR_BINARY) {
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
    } else {
        return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unknown expression type");
    }

    return semantic_record_expr_type(analyzer->result, expr, *out_type, analyzer->error);
}
