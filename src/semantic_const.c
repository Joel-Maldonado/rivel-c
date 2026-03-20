#include "semantic_internal.h"

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

    if (record->info.decl->as.global_const.has_annotation && !type_equal(record->info.decl->as.global_const.annotation, value.type)) {
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

    record->info.type = record->info.decl->as.global_const.has_annotation ? record->info.decl->as.global_const.annotation : value.type;
    record->info.value = value;
    record->visit_state = GLOBAL_DONE;
    *out_value = value;
    return true;
}

static bool analyzer_evaluate_binary_const_expr(Analyzer *analyzer, const Expr *expr, ConstValue lhs, ConstValue rhs, ConstValue *out_value) {
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
            if (!semantic_record_expr_type(analyzer->result, expr, int_type, analyzer->error)) {
                return false;
            }
            if (expr->as.binary.op == TOKEN_PLUS) {
                *out_value = semantic_make_int(lhs.int_value + rhs.int_value);
            } else if (expr->as.binary.op == TOKEN_MINUS) {
                *out_value = semantic_make_int(lhs.int_value - rhs.int_value);
            } else if (expr->as.binary.op == TOKEN_STAR) {
                *out_value = semantic_make_int(lhs.int_value * rhs.int_value);
            } else {
                if (rhs.int_value == 0) {
                    return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Division by zero in constant expression");
                }
                if (expr->as.binary.op == TOKEN_SLASH) {
                    *out_value = semantic_make_int(lhs.int_value / rhs.int_value);
                } else {
                    *out_value = semantic_make_int(lhs.int_value % rhs.int_value);
                }
            }
            return semantic_record_expr_const(analyzer->result, expr, *out_value, analyzer->error);
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
            if (!semantic_record_expr_type(analyzer->result, expr, bool_type, analyzer->error)) {
                return false;
            }
            if (expr->as.binary.op == TOKEN_LESS) {
                *out_value = semantic_make_bool(lhs.int_value < rhs.int_value);
            } else if (expr->as.binary.op == TOKEN_LESS_EQ) {
                *out_value = semantic_make_bool(lhs.int_value <= rhs.int_value);
            } else if (expr->as.binary.op == TOKEN_GREATER) {
                *out_value = semantic_make_bool(lhs.int_value > rhs.int_value);
            } else {
                *out_value = semantic_make_bool(lhs.int_value >= rhs.int_value);
            }
            return semantic_record_expr_const(analyzer->result, expr, *out_value, analyzer->error);
        case TOKEN_EQ_EQ:
        case TOKEN_BANG_EQ:
            if (!type_equal(lhs.type, rhs.type)) {
                return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Equality operators require matching operand types");
            }
            if (!semantic_record_expr_type(analyzer->result, expr, bool_type, analyzer->error)) {
                return false;
            }
            if (lhs.type.kind == TYPE_INT) {
                *out_value = semantic_make_bool(expr->as.binary.op == TOKEN_EQ_EQ ? lhs.int_value == rhs.int_value : lhs.int_value != rhs.int_value);
            } else {
                *out_value = semantic_make_bool(expr->as.binary.op == TOKEN_EQ_EQ ? lhs.bool_value == rhs.bool_value : lhs.bool_value != rhs.bool_value);
            }
            return semantic_record_expr_const(analyzer->result, expr, *out_value, analyzer->error);
        default:
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unsupported constant expression");
    }
}

bool analyzer_evaluate_const_expr(Analyzer *analyzer, const Expr *expr, ConstValue *out_value) {
    if (semantic_lookup_recorded_expr_const(analyzer->result, expr, out_value)) {
        return true;
    }

    if (expr->kind == EXPR_INT) {
        *out_value = semantic_make_int(expr->as.int_value);
        return semantic_record_expr_type(analyzer->result, expr, out_value->type, analyzer->error)
            && semantic_record_expr_const(analyzer->result, expr, *out_value, analyzer->error);
    }
    if (expr->kind == EXPR_BOOL) {
        *out_value = semantic_make_bool(expr->as.bool_value);
        return semantic_record_expr_type(analyzer->result, expr, out_value->type, analyzer->error)
            && semantic_record_expr_const(analyzer->result, expr, *out_value, analyzer->error);
    }
    if (expr->kind == EXPR_NAME) {
        ConstValue value;

        if (analyzer_lookup_global(analyzer, expr->as.name) == NULL) {
            return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Top-level constants can only reference other top-level constants");
        }
        if (!analyzer_evaluate_global_constant(analyzer, expr->as.name, &value)) {
            return false;
        }
        *out_value = value;
        return semantic_record_expr_type(analyzer->result, expr, value.type, analyzer->error)
            && semantic_record_expr_const(analyzer->result, expr, value, analyzer->error);
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
            *out_value = semantic_make_int(-operand.int_value);
        } else {
            if (!analyzer_require_type(analyzer, expr->token, operand.type, (Type){TYPE_BOOL}, "`not` expects Bool")) {
                return false;
            }
            *out_value = semantic_make_bool(!operand.bool_value);
        }
        return semantic_record_expr_type(analyzer->result, expr, out_value->type, analyzer->error)
            && semantic_record_expr_const(analyzer->result, expr, *out_value, analyzer->error);
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
            if (!analyzer_require_type(analyzer, expr->token, lhs.type, (Type){TYPE_BOOL}, expr->as.binary.op == TOKEN_KW_AND ? "`and` expects Bool operands" : "`or` expects Bool operands")) {
                return false;
            }
            if (!analyzer_require_type(analyzer, expr->token, rhs.type, (Type){TYPE_BOOL}, expr->as.binary.op == TOKEN_KW_AND ? "`and` expects Bool operands" : "`or` expects Bool operands")) {
                return false;
            }
            *out_value = semantic_make_bool(expr->as.binary.op == TOKEN_KW_AND ? (lhs.bool_value && rhs.bool_value) : (lhs.bool_value || rhs.bool_value));
            return semantic_record_expr_type(analyzer->result, expr, out_value->type, analyzer->error)
                && semantic_record_expr_const(analyzer->result, expr, *out_value, analyzer->error);
        }
        return analyzer_evaluate_binary_const_expr(analyzer, expr, lhs, rhs, out_value);
    }

    return error_set_at(analyzer->error, "Semantic", expr->token.line, expr->token.column, "Unsupported constant expression");
}
