#include "semantic_internal.h"

static const StructFieldDecl *analyzer_lookup_struct_field(const SemanticStructInfo *info, StrSlice field_name) {
    size_t index = 0U;

    while (index < struct_field_decl_list_len(&info->decl->as.struct_decl.fields)) {
        const StructFieldDecl *field = struct_field_decl_list_get_const(&info->decl->as.struct_decl.fields, index);

        if (slice_equal(field->name, field_name)) {
            return field;
        }
        index += 1U;
    }

    return NULL;
}

static bool analyzer_builtin_expect_arg_count(Analyzer *analyzer, const Expr *call_expr, size_t expected_count) {
    if (expr_list_len(&call_expr->as.call.args) != expected_count) {
        return error_set_at(analyzer->error,
                            "Semantic",
                            call_expr->token.line,
                            call_expr->token.column,
                            "Builtin `%.*s` expects %zu argument(s)",
                            (int)call_expr->as.call.callee.len,
                            call_expr->as.call.callee.data,
                            expected_count);
    }
    return true;
}

static bool analyzer_builtin_require_arg_type(Analyzer *analyzer, const Expr *call_expr, size_t index, Type expected, Type *out_type) {
    Expr *arg = expr_list_get(&call_expr->as.call.args, index);

    if (!analyzer_analyze_expr(analyzer, arg, out_type)) {
        return false;
    }
    if (!type_equal(*out_type, expected)) {
        return error_set_at(analyzer->error,
                            "Semantic",
                            call_expr->token.line,
                            call_expr->token.column,
                            "Argument %zu to builtin `%.*s` has type %s, expected %s",
                            index + 1U,
                            (int)call_expr->as.call.callee.len,
                            call_expr->as.call.callee.data,
                            type_display_name(*out_type),
                            type_display_name(expected));
    }
    return true;
}

static bool analyzer_analyze_binary_expr(Analyzer *analyzer, Token token, TokenType op, Type lhs, Type rhs, Type *out_type) {
    Type int_type = {.kind = TYPE_INT};
    Type bool_type = {.kind = TYPE_BOOL};
    Type string_type = {.kind = TYPE_STRING};

    switch (op) {
        case TOKEN_PLUS:
            if (type_is_numeric(lhs) && type_is_numeric(rhs)) {
                out_type->kind = lhs.kind == TYPE_INT && rhs.kind == TYPE_INT ? TYPE_INT : TYPE_DOUBLE;
                return true;
            }
            if (lhs.kind == TYPE_STRING && rhs.kind == TYPE_STRING) {
                *out_type = string_type;
                return true;
            }
            return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Operator %s expects numeric operands or String operands", token_name(op));
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
            if (!type_is_numeric(lhs) || !type_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Operator %s expects numeric operands", token_name(op));
            }
            out_type->kind = lhs.kind == TYPE_INT && rhs.kind == TYPE_INT ? TYPE_INT : TYPE_DOUBLE;
            return true;
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
            if (!type_is_numeric(lhs) || !type_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Comparison operators expect numeric operands");
            }
            *out_type = bool_type;
            return true;
        case TOKEN_EQ_EQ:
        case TOKEN_BANG_EQ:
            if (lhs.kind == TYPE_STRUCT || rhs.kind == TYPE_STRUCT) {
                return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Equality operators do not support struct operands");
            }
            if (!type_equal(lhs, rhs) && !(type_is_numeric(lhs) && type_is_numeric(rhs))) {
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

static bool analyzer_analyze_builtin_call(Analyzer *analyzer, const Expr *call_expr, SemanticBuiltinInfo builtin, bool allow_statement_only_builtins, Type *out_type) {
    Type int_type = {.kind = TYPE_INT};
    Type string_type = {.kind = TYPE_STRING};
    Type arg_type;

    if (builtin.kind == BUILTIN_PRINT) {
        if (!allow_statement_only_builtins) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` cannot be used as an expression");
        }
        if (!analyzer_builtin_expect_arg_count(analyzer, call_expr, 1U)) {
            return false;
        }
        if (!analyzer_analyze_expr(analyzer, expr_list_get(&call_expr->as.call.args, 0U), &arg_type)) {
            return false;
        }
        if (arg_type.kind != TYPE_INT && arg_type.kind != TYPE_DOUBLE && arg_type.kind != TYPE_BOOL && arg_type.kind != TYPE_STRING) {
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Builtin `print` does not support argument type %s", type_display_name(arg_type));
        }
        out_type->kind = TYPE_INT;
        return true;
    }
    if (builtin.kind == BUILTIN_LEN) {
        if (!analyzer_builtin_expect_arg_count(analyzer, call_expr, 1U)
            || !analyzer_builtin_require_arg_type(analyzer, call_expr, 0U, string_type, &arg_type)) {
            return false;
        }
        *out_type = int_type;
        return true;
    }
    if (builtin.kind == BUILTIN_SUBSTR) {
        Type start_type;
        Type len_type;

        if (!analyzer_builtin_expect_arg_count(analyzer, call_expr, 3U)
            || !analyzer_builtin_require_arg_type(analyzer, call_expr, 0U, string_type, &arg_type)
            || !analyzer_builtin_require_arg_type(analyzer, call_expr, 1U, int_type, &start_type)
            || !analyzer_builtin_require_arg_type(analyzer, call_expr, 2U, int_type, &len_type)) {
            return false;
        }
        *out_type = string_type;
        return true;
    }
    if (builtin.kind == BUILTIN_CONTAINS || builtin.kind == BUILTIN_STARTS_WITH || builtin.kind == BUILTIN_ENDS_WITH) {
        Type rhs_type;

        if (!analyzer_builtin_expect_arg_count(analyzer, call_expr, 2U)
            || !analyzer_builtin_require_arg_type(analyzer, call_expr, 0U, string_type, &arg_type)
            || !analyzer_builtin_require_arg_type(analyzer, call_expr, 1U, string_type, &rhs_type)) {
            return false;
        }
        out_type->kind = TYPE_BOOL;
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
    } else if (expr->kind == EXPR_STRING) {
        out_type->kind = TYPE_STRING;
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
    } else if (expr->kind == EXPR_STRUCT_LITERAL) {
        const SemanticStructInfo *struct_info = analyzer_lookup_struct(analyzer, expr->as.struct_literal.struct_name);
        size_t field_index = 0U;

        if (struct_info == NULL) {
            return error_set_at(analyzer->error,
                                "Semantic",
                                expr->token.line,
                                expr->token.column,
                                "Unknown struct `%.*s`",
                                (int)expr->as.struct_literal.struct_name.len,
                                expr->as.struct_literal.struct_name.data);
        }
        while (field_index < struct_literal_field_list_len(&expr->as.struct_literal.fields)) {
            const StructLiteralField *field = struct_literal_field_list_get_const(&expr->as.struct_literal.fields, field_index);
            const StructFieldDecl *field_decl = analyzer_lookup_struct_field(struct_info, field->name);
            size_t prior_index = 0U;
            Type value_type;

            if (field_decl == NULL) {
                return error_set_at(analyzer->error,
                                    "Semantic",
                                    field->token.line,
                                    field->token.column,
                                    "Struct `%.*s` has no field named `%.*s`",
                                    (int)expr->as.struct_literal.struct_name.len,
                                    expr->as.struct_literal.struct_name.data,
                                    (int)field->name.len,
                                    field->name.data);
            }
            while (prior_index < field_index) {
                const StructLiteralField *prior_field = struct_literal_field_list_get_const(&expr->as.struct_literal.fields, prior_index);

                if (slice_equal(prior_field->name, field->name)) {
                    return error_set_at(analyzer->error,
                                        "Semantic",
                                        field->token.line,
                                        field->token.column,
                                        "Struct literal for `%.*s` initializes field `%.*s` more than once",
                                        (int)expr->as.struct_literal.struct_name.len,
                                        expr->as.struct_literal.struct_name.data,
                                        (int)field->name.len,
                                        field->name.data);
                }
                prior_index += 1U;
            }
            if (!analyzer_analyze_expr(analyzer, field->value, &value_type)) {
                return false;
            }
            if (!type_can_widen_to(value_type, field_decl->type)) {
                return error_set_at(analyzer->error,
                                    "Semantic",
                                    field->token.line,
                                    field->token.column,
                                    "Field `%.*s` of `%.*s` expects %s but got %s",
                                    (int)field->name.len,
                                    field->name.data,
                                    (int)expr->as.struct_literal.struct_name.len,
                                    expr->as.struct_literal.struct_name.data,
                                    type_display_name(field_decl->type),
                                    type_display_name(value_type));
            }
            field_index += 1U;
        }
        if (field_index != struct_field_decl_list_len(&struct_info->decl->as.struct_decl.fields)) {
            size_t decl_index = 0U;

            while (decl_index < struct_field_decl_list_len(&struct_info->decl->as.struct_decl.fields)) {
                const StructFieldDecl *field_decl = struct_field_decl_list_get_const(&struct_info->decl->as.struct_decl.fields, decl_index);
                size_t literal_index = 0U;
                bool found = false;

                while (literal_index < struct_literal_field_list_len(&expr->as.struct_literal.fields)) {
                    const StructLiteralField *field = struct_literal_field_list_get_const(&expr->as.struct_literal.fields, literal_index);
                    if (slice_equal(field_decl->name, field->name)) {
                        found = true;
                        break;
                    }
                    literal_index += 1U;
                }
                if (!found) {
                    return error_set_at(analyzer->error,
                                        "Semantic",
                                        expr->token.line,
                                        expr->token.column,
                                        "Struct literal for `%.*s` is missing field `%.*s`",
                                        (int)expr->as.struct_literal.struct_name.len,
                                        expr->as.struct_literal.struct_name.data,
                                        (int)field_decl->name.len,
                                        field_decl->name.data);
                }
                decl_index += 1U;
            }
        }
        out_type->kind = TYPE_STRUCT;
        out_type->struct_name = expr->as.struct_literal.struct_name;
        out_type->struct_name_cstr = arena_copy_slice(analyzer->result->arena, expr->as.struct_literal.struct_name, analyzer->error);
        if (out_type->struct_name_cstr == NULL) {
            return false;
        }
    } else if (expr->kind == EXPR_FIELD) {
        Type base_type;
        const SemanticStructInfo *struct_info;
        const StructFieldDecl *field_decl;

        if (!analyzer_analyze_expr(analyzer, expr->as.field.base, &base_type)) {
            return false;
        }
        if (base_type.kind != TYPE_STRUCT) {
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Field access requires a struct value");
        }
        struct_info = analyzer_lookup_struct(analyzer, base_type.struct_name);
        if (struct_info == NULL) {
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unknown struct `%.*s`", (int)base_type.struct_name.len, base_type.struct_name.data);
        }
        field_decl = analyzer_lookup_struct_field(struct_info, expr->as.field.name);
        if (field_decl == NULL) {
            return error_set_at(analyzer->error,
                                "Semantic",
                                expr->token.line,
                                expr->token.column,
                                "Struct `%.*s` has no field named `%.*s`",
                                (int)base_type.struct_name.len,
                                base_type.struct_name.data,
                                (int)expr->as.field.name.len,
                                expr->as.field.name.data);
        }
        *out_type = field_decl->type;
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
