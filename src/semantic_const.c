#include "semantic_internal.h"

#include <string.h>

static double const_value_as_double(ConstValue value) {
    if (value.type.kind == TYPE_DOUBLE) {
        return value.double_value;
    }
    return (double)value.int_value;
}

static bool const_value_is_numeric(ConstValue value) {
    return type_is_numeric(value.type);
}

static ConstValue const_value_coerce(ConstValue value, Type target) {
    if (type_equal(value.type, target)) {
        return value;
    }
    if (value.type.kind == TYPE_INT && target.kind == TYPE_DOUBLE) {
        return const_value_make_double((double)value.int_value);
    }
    return value;
}

static bool semantic_const_copy_bytes(Analyzer *analyzer, StrSlice value, StrSlice *out_copy) {
    char *copy;

    if (value.len == 0U) {
        copy = arena_copy_cstr(analyzer->result->arena, "", analyzer->error);
    } else {
        copy = (char *)arena_alloc(analyzer->result->arena, value.len, _Alignof(char), analyzer->error);
        if (copy != NULL) {
            memcpy(copy, value.data, value.len);
        }
    }
    if (copy == NULL) {
        return false;
    }

    *out_copy = slice_from_parts(copy, value.len);
    return true;
}

static bool semantic_const_concat_strings(Analyzer *analyzer, StrSlice lhs, StrSlice rhs, StrSlice *out_value) {
    char *buffer;

    if (lhs.len + rhs.len == 0U) {
        return semantic_const_copy_bytes(analyzer, slice_from_parts("", 0U), out_value);
    }

    buffer = (char *)arena_alloc(analyzer->result->arena, lhs.len + rhs.len, _Alignof(char), analyzer->error);
    if (buffer == NULL) {
        return false;
    }
    if (lhs.len > 0U) {
        memcpy(buffer, lhs.data, lhs.len);
    }
    if (rhs.len > 0U) {
        memcpy(buffer + lhs.len, rhs.data, rhs.len);
    }

    *out_value = slice_from_parts(buffer, lhs.len + rhs.len);
    return true;
}

static bool semantic_const_contains(StrSlice haystack, StrSlice needle) {
    size_t index = 0U;

    if (needle.len == 0U) {
        return true;
    }
    if (needle.len > haystack.len) {
        return false;
    }

    while (index + needle.len <= haystack.len) {
        if (memcmp(haystack.data + index, needle.data, needle.len) == 0) {
            return true;
        }
        index += 1U;
    }
    return false;
}

static bool semantic_const_starts_with(StrSlice value, StrSlice prefix) {
    return prefix.len <= value.len && memcmp(value.data, prefix.data, prefix.len) == 0;
}

static bool semantic_const_ends_with(StrSlice value, StrSlice suffix) {
    return suffix.len <= value.len && memcmp(value.data + value.len - suffix.len, suffix.data, suffix.len) == 0;
}

static bool semantic_const_substr(Analyzer *analyzer, Token token, StrSlice value, int64_t start_value, int64_t len_value, StrSlice *out_value) {
    size_t start_index;
    size_t length;

    if (start_value < 0 || len_value < 0) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Substring bounds must be non-negative in constant expression");
    }

    start_index = (size_t)start_value;
    length = (size_t)len_value;
    if (start_index > value.len || length > value.len - start_index) {
        return error_set_at(analyzer->error, "Semantic", token.line, token.column, "Substring out of range in constant expression");
    }

    *out_value = slice_from_parts(value.data + start_index, length);
    return true;
}

static bool semantic_const_stringify(Analyzer *analyzer, ConstValue value, StrSlice *out_value) {
    char *text;

    if (value.type.kind == TYPE_STRING) {
        *out_value = value.string_value;
        return true;
    }
    if (value.type.kind == TYPE_BOOL) {
        *out_value = slice_from_cstr(value.bool_value ? "true" : "false");
        return true;
    }
    if (value.type.kind == TYPE_DOUBLE) {
        text = arena_printf(analyzer->result->arena, analyzer->error, "%.17g", value.double_value);
    } else {
        text = arena_printf(analyzer->result->arena, analyzer->error, "%lld", (long long)value.int_value);
    }
    if (text == NULL) {
        return false;
    }

    *out_value = slice_from_cstr(text);
    return true;
}

static bool semantic_const_expect_builtin_arg_count(Analyzer *analyzer, const Expr *call_expr, size_t expected_count) {
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

static bool semantic_const_require_arg_type(Analyzer *analyzer, const Expr *call_expr, size_t index, Type expected, ConstValue *out_value) {
    Expr *arg = expr_list_get(&call_expr->as.call.args, index);

    if (!analyzer_evaluate_const_expr(analyzer, arg, out_value)) {
        return false;
    }
    if (!type_equal(out_value->type, expected)) {
        return error_set_at(analyzer->error,
                            "Semantic",
                            call_expr->token.line,
                            call_expr->token.column,
                            "Argument %zu to builtin `%.*s` has type %s, expected %s",
                            index + 1U,
                            (int)call_expr->as.call.callee.len,
                            call_expr->as.call.callee.data,
                            type_display_name(out_value->type),
                            type_display_name(expected));
    }
    return true;
}

static bool semantic_record_const_result(Analyzer *analyzer, const Expr *expr, ConstValue value, ConstValue *out_value) {
    *out_value = value;
    return semantic_record_expr_type(analyzer->result, expr, value.type, analyzer->error)
        && semantic_record_expr_const(analyzer->result, expr, value, analyzer->error);
}

static bool analyzer_evaluate_builtin_const_expr(Analyzer *analyzer, const Expr *call_expr, BuiltinKind builtin_kind, ConstValue *out_value) {
    Type int_type = type_make_int();
    Type string_type = type_make_string();
    ConstValue arg0;
    ConstValue arg1;
    ConstValue arg2;
    StrSlice substr_value;

    switch (builtin_kind) {
        case BUILTIN_PRINT:
        case BUILTIN_PRINTLN:
            return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Top-level constants cannot call builtin `print`");
        case BUILTIN_STRINGIFY:
            if (!semantic_const_expect_builtin_arg_count(analyzer, call_expr, 1U)
                || !analyzer_evaluate_const_expr(analyzer, expr_list_get(&call_expr->as.call.args, 0U), &arg0)
                || !semantic_const_stringify(analyzer, arg0, &substr_value)) {
                return false;
            }
            return semantic_record_const_result(analyzer, call_expr, const_value_make_string(substr_value), out_value);
        case BUILTIN_LEN:
            if (!semantic_const_expect_builtin_arg_count(analyzer, call_expr, 1U)
                || !semantic_const_require_arg_type(analyzer, call_expr, 0U, string_type, &arg0)) {
                return false;
            }
            return semantic_record_const_result(analyzer, call_expr, const_value_make_int((int64_t)arg0.string_value.len), out_value);
        case BUILTIN_SUBSTR:
            if (!semantic_const_expect_builtin_arg_count(analyzer, call_expr, 3U)
                || !semantic_const_require_arg_type(analyzer, call_expr, 0U, string_type, &arg0)
                || !semantic_const_require_arg_type(analyzer, call_expr, 1U, int_type, &arg1)
                || !semantic_const_require_arg_type(analyzer, call_expr, 2U, int_type, &arg2)
                || !semantic_const_substr(analyzer, call_expr->token, arg0.string_value, arg1.int_value, arg2.int_value, &substr_value)) {
                return false;
            }
            return semantic_record_const_result(analyzer, call_expr, const_value_make_string(substr_value), out_value);
        case BUILTIN_CONTAINS:
            if (!semantic_const_expect_builtin_arg_count(analyzer, call_expr, 2U)
                || !semantic_const_require_arg_type(analyzer, call_expr, 0U, string_type, &arg0)
                || !semantic_const_require_arg_type(analyzer, call_expr, 1U, string_type, &arg1)) {
                return false;
            }
            return semantic_record_const_result(analyzer, call_expr, const_value_make_bool(semantic_const_contains(arg0.string_value, arg1.string_value)), out_value);
        case BUILTIN_STARTS_WITH:
            if (!semantic_const_expect_builtin_arg_count(analyzer, call_expr, 2U)
                || !semantic_const_require_arg_type(analyzer, call_expr, 0U, string_type, &arg0)
                || !semantic_const_require_arg_type(analyzer, call_expr, 1U, string_type, &arg1)) {
                return false;
            }
            return semantic_record_const_result(analyzer, call_expr, const_value_make_bool(semantic_const_starts_with(arg0.string_value, arg1.string_value)), out_value);
        case BUILTIN_ENDS_WITH:
            if (!semantic_const_expect_builtin_arg_count(analyzer, call_expr, 2U)
                || !semantic_const_require_arg_type(analyzer, call_expr, 0U, string_type, &arg0)
                || !semantic_const_require_arg_type(analyzer, call_expr, 1U, string_type, &arg1)) {
                return false;
            }
            return semantic_record_const_result(analyzer, call_expr, const_value_make_bool(semantic_const_ends_with(arg0.string_value, arg1.string_value)), out_value);
    }

    return error_set_at(analyzer->error, "Semantic", call_expr->token.line, call_expr->token.column, "Unknown builtin `%.*s`", (int)call_expr->as.call.callee.len, call_expr->as.call.callee.data);
}

bool analyzer_evaluate_global_constant(Analyzer *analyzer, StrSlice name, ConstValue *out_value) {
    SemanticGlobalRecord *record = analyzer_lookup_global_record_mut(analyzer, name);
    ConstValue value;

    if (record == NULL) {
        return false;
    }
    if (record->visit_state == GLOBAL_DONE) {
        *out_value = record->info.value;
        return true;
    }
    if (record->visit_state == GLOBAL_VISITING) {
        return error_set_at(analyzer->error, "Semantic", record->info.decl->token.line, record->info.decl->token.column, "Cyclic top-level constant definition involving `%.*s`", (int)name.len, name.data);
    }

    record->visit_state = GLOBAL_VISITING;
    if (!analyzer_evaluate_const_expr(analyzer, record->info.decl->as.global_const.initializer, &value)) {
        return false;
    }

    if (record->info.decl->as.global_const.has_annotation
        && !type_can_widen_to(value.type, record->info.decl->as.global_const.annotation)) {
        return error_set_at(
            analyzer->error,
            "Semantic",
            record->info.decl->token.line,
            record->info.decl->token.column,
            "Top-level constant `%.*s` is declared as %s but initializes to %s",
            (int)name.len,
            name.data,
            type_display_name(record->info.decl->as.global_const.annotation),
            type_display_name(value.type));
    }

    if (record->info.decl->as.global_const.has_annotation) {
        value = const_value_coerce(value, record->info.decl->as.global_const.annotation);
        record->info.type = record->info.decl->as.global_const.annotation;
    } else {
        record->info.type = value.type;
    }
    record->info.value = value;
    record->visit_state = GLOBAL_DONE;
    *out_value = value;
    return true;
}

static bool analyzer_evaluate_binary_const_expr(Analyzer *analyzer, const Expr *expr, ConstValue lhs, ConstValue rhs, ConstValue *out_value) {
    StrSlice concat_value;
    bool bool_result;

    switch (expr->as.binary.op) {
        case TOKEN_PLUS:
            if (lhs.type.kind == TYPE_STRING && rhs.type.kind == TYPE_STRING) {
                if (!semantic_const_concat_strings(analyzer, lhs.string_value, rhs.string_value, &concat_value)) {
                    return false;
                }
                return semantic_record_const_result(analyzer, expr, const_value_make_string(concat_value), out_value);
            }
            if (!const_value_is_numeric(lhs) || !const_value_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Operator %s expects numeric operands or String operands", token_name(expr->as.binary.op));
            }
            if (lhs.type.kind == TYPE_INT && rhs.type.kind == TYPE_INT) {
                return semantic_record_const_result(analyzer, expr, const_value_make_int(lhs.int_value + rhs.int_value), out_value);
            }
            return semantic_record_const_result(analyzer, expr, const_value_make_double(const_value_as_double(lhs) + const_value_as_double(rhs)), out_value);
        case TOKEN_MINUS:
        case TOKEN_STAR:
            if (!const_value_is_numeric(lhs) || !const_value_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Arithmetic operators expect numeric operands");
            }
            if (lhs.type.kind == TYPE_INT && rhs.type.kind == TYPE_INT) {
                if (expr->as.binary.op == TOKEN_MINUS) {
                    return semantic_record_const_result(analyzer, expr, const_value_make_int(lhs.int_value - rhs.int_value), out_value);
                }
                return semantic_record_const_result(analyzer, expr, const_value_make_int(lhs.int_value * rhs.int_value), out_value);
            }
            if (expr->as.binary.op == TOKEN_MINUS) {
                return semantic_record_const_result(analyzer, expr, const_value_make_double(const_value_as_double(lhs) - const_value_as_double(rhs)), out_value);
            }
            return semantic_record_const_result(analyzer, expr, const_value_make_double(const_value_as_double(lhs) * const_value_as_double(rhs)), out_value);
        case TOKEN_SLASH:
            if (!const_value_is_numeric(lhs) || !const_value_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Arithmetic operators expect numeric operands");
            }
            if (lhs.type.kind == TYPE_INT && rhs.type.kind == TYPE_INT) {
                if (rhs.int_value == 0) {
                    return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Division by zero in constant expression");
                }
                return semantic_record_const_result(analyzer, expr, const_value_make_int(lhs.int_value / rhs.int_value), out_value);
            }
            return semantic_record_const_result(analyzer, expr, const_value_make_double(const_value_as_double(lhs) / const_value_as_double(rhs)), out_value);
        case TOKEN_PERCENT:
            if (lhs.type.kind != TYPE_INT || rhs.type.kind != TYPE_INT) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Arithmetic operators expect Int operands");
            }
            if (rhs.int_value == 0) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Division by zero in constant expression");
            }
            return semantic_record_const_result(analyzer, expr, const_value_make_int(lhs.int_value % rhs.int_value), out_value);
        case TOKEN_LESS:
        case TOKEN_LESS_EQ:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQ:
            if (!const_value_is_numeric(lhs) || !const_value_is_numeric(rhs)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Comparison operators expect numeric operands");
            }
            if (lhs.type.kind == TYPE_INT && rhs.type.kind == TYPE_INT) {
                if (expr->as.binary.op == TOKEN_LESS) {
                    bool_result = lhs.int_value < rhs.int_value;
                } else if (expr->as.binary.op == TOKEN_LESS_EQ) {
                    bool_result = lhs.int_value <= rhs.int_value;
                } else if (expr->as.binary.op == TOKEN_GREATER) {
                    bool_result = lhs.int_value > rhs.int_value;
                } else {
                    bool_result = lhs.int_value >= rhs.int_value;
                }
            } else {
                double lhs_value = const_value_as_double(lhs);
                double rhs_value = const_value_as_double(rhs);

                if (expr->as.binary.op == TOKEN_LESS) {
                    bool_result = lhs_value < rhs_value;
                } else if (expr->as.binary.op == TOKEN_LESS_EQ) {
                    bool_result = lhs_value <= rhs_value;
                } else if (expr->as.binary.op == TOKEN_GREATER) {
                    bool_result = lhs_value > rhs_value;
                } else {
                    bool_result = lhs_value >= rhs_value;
                }
            }
            return semantic_record_const_result(analyzer, expr, const_value_make_bool(bool_result), out_value);
        case TOKEN_EQ_EQ:
        case TOKEN_BANG_EQ:
            if (lhs.type.kind == TYPE_STRUCT || rhs.type.kind == TYPE_STRUCT) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Equality operators do not support struct operands");
            }
            if (!type_equal(lhs.type, rhs.type) && !(const_value_is_numeric(lhs) && const_value_is_numeric(rhs))) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Equality operators require matching operand types");
            }
            if (lhs.type.kind == TYPE_BOOL && rhs.type.kind == TYPE_BOOL) {
                bool_result = lhs.bool_value == rhs.bool_value;
            } else if (lhs.type.kind == TYPE_INT && rhs.type.kind == TYPE_INT) {
                bool_result = lhs.int_value == rhs.int_value;
            } else if (lhs.type.kind == TYPE_STRING && rhs.type.kind == TYPE_STRING) {
                bool_result = slice_equal(lhs.string_value, rhs.string_value);
            } else {
                bool_result = const_value_as_double(lhs) == const_value_as_double(rhs);
            }
            if (expr->as.binary.op == TOKEN_BANG_EQ) {
                bool_result = !bool_result;
            }
            return semantic_record_const_result(analyzer, expr, const_value_make_bool(bool_result), out_value);
        default:
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unsupported constant expression");
    }
}

bool analyzer_evaluate_const_expr(Analyzer *analyzer, const Expr *expr, ConstValue *out_value) {
    if (semantic_lookup_recorded_expr_const(analyzer->result, expr, out_value)) {
        return true;
    }

    if (expr->kind == EXPR_INT) {
        return semantic_record_const_result(analyzer, expr, const_value_make_int(expr->as.int_value), out_value);
    }
    if (expr->kind == EXPR_DOUBLE) {
        return semantic_record_const_result(analyzer, expr, const_value_make_double(expr->as.double_value), out_value);
    }
    if (expr->kind == EXPR_BOOL) {
        return semantic_record_const_result(analyzer, expr, const_value_make_bool(expr->as.bool_value), out_value);
    }
    if (expr->kind == EXPR_STRING) {
        return semantic_record_const_result(analyzer, expr, const_value_make_string(expr->as.string_value), out_value);
    }
    if (expr->kind == EXPR_NAME) {
        ConstValue value;

        if (analyzer_lookup_global(analyzer, expr->as.name) == NULL) {
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Top-level constants can only reference other top-level constants");
        }
        if (!analyzer_evaluate_global_constant(analyzer, expr->as.name, &value)) {
            return false;
        }
        return semantic_record_const_result(analyzer, expr, value, out_value);
    }
    if (expr->kind == EXPR_CALL) {
        const SemanticBuiltinInfo *builtin = analyzer_lookup_builtin(analyzer, expr->as.call.callee);

        if (builtin == NULL) {
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Top-level constants cannot call functions");
        }
        return analyzer_evaluate_builtin_const_expr(analyzer, expr, builtin->kind, out_value);
    }
    if (expr->kind == EXPR_STRUCT_LITERAL) {
        return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Struct literals are not allowed in top-level constants");
    }
    if (expr->kind == EXPR_FIELD) {
        return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Field access is not allowed in top-level constants");
    }
    if (expr->kind == EXPR_UNARY) {
        ConstValue operand;

        if (!analyzer_evaluate_const_expr(analyzer, expr->as.unary.operand, &operand)) {
            return false;
        }
        if (expr->as.unary.op == TOKEN_MINUS) {
            if (!type_is_numeric(operand.type)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unary `-` expects Int or Double");
            }
            if (operand.type.kind == TYPE_INT) {
                return semantic_record_const_result(analyzer, expr, const_value_make_int(-operand.int_value), out_value);
            }
            return semantic_record_const_result(analyzer, expr, const_value_make_double(-operand.double_value), out_value);
        }
        if (!analyzer_require_type(analyzer, expr->token, operand.type, type_make_bool(), "`not` expects Bool")) {
            return false;
        }
        return semantic_record_const_result(analyzer, expr, const_value_make_bool(!operand.bool_value), out_value);
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
        if (expr->as.binary.op == TOKEN_KW_AND || expr->as.binary.op == TOKEN_KW_OR) {
            if (!analyzer_require_type(analyzer, expr->token, lhs.type, type_make_bool(), expr->as.binary.op == TOKEN_KW_AND ? "`and` expects Bool operands" : "`or` expects Bool operands")) {
                return false;
            }
            if (!analyzer_require_type(analyzer, expr->token, rhs.type, type_make_bool(), expr->as.binary.op == TOKEN_KW_AND ? "`and` expects Bool operands" : "`or` expects Bool operands")) {
                return false;
            }
            return semantic_record_const_result(
                analyzer,
                expr,
                const_value_make_bool(expr->as.binary.op == TOKEN_KW_AND ? (lhs.bool_value && rhs.bool_value) : (lhs.bool_value || rhs.bool_value)),
                out_value);
        }
        return analyzer_evaluate_binary_const_expr(analyzer, expr, lhs, rhs, out_value);
    }

    return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unsupported constant expression");
}
