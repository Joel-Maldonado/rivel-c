#pragma once

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "parser.hpp"

struct ConstValue {
    Type type;
    int64_t int_value = 0;
    bool bool_value = false;
};

inline ConstValue make_int_value(const int64_t value) {
    return ConstValue{Type{TypeKind::Int}, value, false};
}

inline ConstValue make_bool_value(const bool value) {
    return ConstValue{Type{TypeKind::Bool}, 0, value};
}

struct GlobalConstInfo {
    const GlobalConstDecl* decl = nullptr;
    Type type{TypeKind::Int};
    ConstValue value = make_int_value(0);
};

struct FunctionInfo {
    const FunctionDecl* decl = nullptr;
};

enum class BuiltinKind {
    Print,
};

struct BuiltinInfo {
    BuiltinKind kind;
};

struct SemanticContext {
    std::unordered_map<std::string, GlobalConstInfo> globals;
    std::unordered_map<std::string, FunctionInfo> functions;
    std::unordered_map<std::string, BuiltinInfo> builtins;
};

class SemanticAnalyzer {
  public:
    explicit SemanticAnalyzer(Program& program)
        : m_program(program) {
    }

    SemanticContext analyze() {
        register_builtins();
        collect_top_level_declarations();
        analyze_global_constants();
        analyze_functions();
        validate_main_signature();
        return m_context;
    }

  private:
    struct BindingInfo {
        Type type;
        bool is_mutable = false;
    };

    enum class VisitState {
        Unvisited,
        Visiting,
        Done,
    };

    void register_builtins() {
        m_context.builtins.emplace("print", BuiltinInfo{BuiltinKind::Print});
    }

    void collect_top_level_declarations() {
        for (const auto& decl : m_program.decls) {
            if (const auto* global = dynamic_cast<const GlobalConstDecl*>(decl.get())) {
                ensure_unique_top_level_name(global->token, global->name);
                m_context.globals.emplace(global->name, GlobalConstInfo{.decl = global});
                m_global_state.emplace(global->name, VisitState::Unvisited);
            } else if (const auto* function = dynamic_cast<const FunctionDecl*>(decl.get())) {
                ensure_unique_top_level_name(function->token, function->name);
                m_context.functions.emplace(function->name, FunctionInfo{.decl = function});
            }
        }
    }

    void ensure_unique_top_level_name(const Token& token, const std::string& name) const {
        if (m_context.builtins.contains(name)) {
            throw error(token, "Top-level name `" + name + "` is reserved for a builtin");
        }
        if (m_context.globals.contains(name) || m_context.functions.contains(name)) {
            throw error(token, "Top-level name `" + name + "` is already declared");
        }
    }

    void analyze_global_constants() {
        for (const auto& [name, _] : m_context.globals) {
            evaluate_global_constant(name);
        }
    }

    ConstValue evaluate_global_constant(const std::string& name) {
        const VisitState state = m_global_state.at(name);
        if (state == VisitState::Done) {
            return m_context.globals.at(name).value;
        }
        if (state == VisitState::Visiting) {
            throw error(m_context.globals.at(name).decl->token, "Cyclic top-level constant definition involving `" + name + "`");
        }

        m_global_state.at(name) = VisitState::Visiting;
        auto& info = m_context.globals.at(name);
        ConstValue value = evaluate_const_expr(*info.decl->initializer);
        if (info.decl->annotation.has_value() && info.decl->annotation.value() != value.type) {
            throw error(info.decl->token, "Top-level constant `" + name + "` is declared as " + info.decl->annotation->display_name() + " but initializes to " + value.type.display_name());
        }
        info.type = info.decl->annotation.value_or(value.type);
        info.value = value;
        info.decl->initializer->inferred_type = info.type;
        m_global_state.at(name) = VisitState::Done;
        return value;
    }

    ConstValue evaluate_const_expr(Expr& expr) {
        if (auto* int_expr = dynamic_cast<IntExpr*>(&expr)) {
            expr.inferred_type = Type{TypeKind::Int};
            return make_int_value(int_expr->value);
        }
        if (auto* bool_expr = dynamic_cast<BoolExpr*>(&expr)) {
            expr.inferred_type = Type{TypeKind::Bool};
            return make_bool_value(bool_expr->value);
        }
        if (auto* name_expr = dynamic_cast<NameExpr*>(&expr)) {
            const auto global = m_context.globals.find(name_expr->name);
            if (global == m_context.globals.end()) {
                throw error(name_expr->token, "Top-level constants can only reference other top-level constants");
            }
            ConstValue value = evaluate_global_constant(name_expr->name);
            expr.inferred_type = value.type;
            return value;
        }
        if (auto* call_expr = dynamic_cast<CallExpr*>(&expr)) {
            throw error(call_expr->token, "Top-level constants cannot call functions");
        }
        if (auto* unary_expr = dynamic_cast<UnaryExpr*>(&expr)) {
            ConstValue operand = evaluate_const_expr(*unary_expr->operand);
            switch (unary_expr->op) {
            case TokenType::minus:
                require_type(unary_expr->token, operand.type, Type{TypeKind::Int}, "Unary `-` expects Int");
                expr.inferred_type = Type{TypeKind::Int};
                return make_int_value(-operand.int_value);
            case TokenType::kw_not:
                require_type(unary_expr->token, operand.type, Type{TypeKind::Bool}, "`not` expects Bool");
                expr.inferred_type = Type{TypeKind::Bool};
                return make_bool_value(!operand.bool_value);
            default:
                break;
            }
        }
        if (auto* binary_expr = dynamic_cast<BinaryExpr*>(&expr)) {
            ConstValue lhs = evaluate_const_expr(*binary_expr->lhs);
            ConstValue rhs = evaluate_const_expr(*binary_expr->rhs);
            if (binary_expr->op == TokenType::kw_and) {
                require_type(binary_expr->token, lhs.type, Type{TypeKind::Bool}, "`and` expects Bool operands");
                require_type(binary_expr->token, rhs.type, Type{TypeKind::Bool}, "`and` expects Bool operands");
                expr.inferred_type = Type{TypeKind::Bool};
                return make_bool_value(lhs.bool_value && rhs.bool_value);
            }
            if (binary_expr->op == TokenType::kw_or) {
                require_type(binary_expr->token, lhs.type, Type{TypeKind::Bool}, "`or` expects Bool operands");
                require_type(binary_expr->token, rhs.type, Type{TypeKind::Bool}, "`or` expects Bool operands");
                expr.inferred_type = Type{TypeKind::Bool};
                return make_bool_value(lhs.bool_value || rhs.bool_value);
            }
            return evaluate_binary_const_expr(*binary_expr, lhs, rhs);
        }
        throw error(expr.token, "Unsupported constant expression");
    }

    ConstValue evaluate_binary_const_expr(BinaryExpr& expr, const ConstValue& lhs, const ConstValue& rhs) {
        const Type int_type{TypeKind::Int};
        const Type bool_type{TypeKind::Bool};

        switch (expr.op) {
        case TokenType::plus:
        case TokenType::minus:
        case TokenType::star:
        case TokenType::slash:
        case TokenType::percent:
            require_type(expr.token, lhs.type, int_type, "Arithmetic operators expect Int operands");
            require_type(expr.token, rhs.type, int_type, "Arithmetic operators expect Int operands");
            expr.inferred_type = int_type;
            switch (expr.op) {
            case TokenType::plus:
                return make_int_value(lhs.int_value + rhs.int_value);
            case TokenType::minus:
                return make_int_value(lhs.int_value - rhs.int_value);
            case TokenType::star:
                return make_int_value(lhs.int_value * rhs.int_value);
            case TokenType::slash:
                if (rhs.int_value == 0) {
                    throw error(expr.token, "Division by zero in constant expression");
                }
                return make_int_value(lhs.int_value / rhs.int_value);
            case TokenType::percent:
                if (rhs.int_value == 0) {
                    throw error(expr.token, "Division by zero in constant expression");
                }
                return make_int_value(lhs.int_value % rhs.int_value);
            default:
                break;
            }
            break;
        case TokenType::less:
        case TokenType::less_eq:
        case TokenType::greater:
        case TokenType::greater_eq:
            require_type(expr.token, lhs.type, int_type, "Comparison operators expect Int operands");
            require_type(expr.token, rhs.type, int_type, "Comparison operators expect Int operands");
            expr.inferred_type = bool_type;
            switch (expr.op) {
            case TokenType::less:
                return make_bool_value(lhs.int_value < rhs.int_value);
            case TokenType::less_eq:
                return make_bool_value(lhs.int_value <= rhs.int_value);
            case TokenType::greater:
                return make_bool_value(lhs.int_value > rhs.int_value);
            case TokenType::greater_eq:
                return make_bool_value(lhs.int_value >= rhs.int_value);
            default:
                break;
            }
            break;
        case TokenType::eq_eq:
        case TokenType::bang_eq:
            if (lhs.type != rhs.type) {
                throw error(expr.token, "Equality operators require matching operand types");
            }
            expr.inferred_type = bool_type;
            if (lhs.type.kind == TypeKind::Int) {
                return make_bool_value(expr.op == TokenType::eq_eq ? lhs.int_value == rhs.int_value : lhs.int_value != rhs.int_value);
            }
            return make_bool_value(expr.op == TokenType::eq_eq ? lhs.bool_value == rhs.bool_value : lhs.bool_value != rhs.bool_value);
        default:
            break;
        }
        throw error(expr.token, "Unsupported constant expression");
    }

    void analyze_functions() {
        for (const auto& [_, function] : m_context.functions) {
            analyze_function(*function.decl);
        }
    }

    void analyze_function(const FunctionDecl& function) {
        m_scopes.clear();
        push_scope();
        for (const Param& param : function.params) {
            declare_local(param.token, param.name, BindingInfo{param.type, false});
        }
        analyze_block(*function.body, function.return_type);
        pop_scope();
        if (!block_guarantees_return(*function.body)) {
            throw error(function.token, "Function `" + function.name + "` does not return on all paths");
        }
    }

    void analyze_block(const Block& block, const Type& function_return_type) {
        push_scope();
        for (const auto& stmt : block.statements) {
            analyze_stmt(*stmt, function_return_type);
        }
        pop_scope();
    }

    void analyze_stmt(Stmt& stmt, const Type& function_return_type) {
        if (auto* binding = dynamic_cast<BindingStmt*>(&stmt)) {
            const Type initializer_type = analyze_expr(*binding->initializer);
            const Type binding_type = binding->annotation.value_or(initializer_type);
            if (binding->annotation.has_value() && binding->annotation.value() != initializer_type) {
                throw error(binding->token, "Binding `" + binding->name + "` is declared as " + binding->annotation->display_name() + " but initializes to " + initializer_type.display_name());
            }
            declare_local(binding->token, binding->name, BindingInfo{binding_type, binding->is_mutable});
            return;
        }
        if (auto* assign = dynamic_cast<AssignStmt*>(&stmt)) {
            const BindingInfo* binding = resolve_local(assign->name);
            if (binding == nullptr) {
                if (m_context.globals.contains(assign->name)) {
                    throw error(assign->token, "Cannot assign to immutable binding `" + assign->name + "`");
                }
                throw error(assign->token, "Unknown binding `" + assign->name + "`");
            }
            if (!binding->is_mutable) {
                throw error(assign->token, "Cannot assign to immutable binding `" + assign->name + "`");
            }
            const Type value_type = analyze_expr(*assign->value);
            if (value_type != binding->type) {
                throw error(assign->token, "Cannot assign value of type " + value_type.display_name() + " to " + binding->type.display_name());
            }
            return;
        }
        if (auto* return_stmt = dynamic_cast<ReturnStmt*>(&stmt)) {
            const Type value_type = analyze_expr(*return_stmt->value);
            if (value_type != function_return_type) {
                throw error(return_stmt->token, "Return type mismatch: expected " + function_return_type.display_name() + " but got " + value_type.display_name());
            }
            return;
        }
        if (auto* call_stmt = dynamic_cast<CallStmt*>(&stmt)) {
            analyze_call(*call_stmt->call, true);
            return;
        }
        if (auto* if_stmt = dynamic_cast<IfStmt*>(&stmt)) {
            const Type condition_type = analyze_expr(*if_stmt->condition);
            if (condition_type.kind != TypeKind::Bool) {
                throw error(if_stmt->token, "If condition must be Bool");
            }
            analyze_block(*if_stmt->then_block, function_return_type);
            for (auto& branch : if_stmt->elif_branches) {
                const Type branch_type = analyze_expr(*branch.condition);
                if (branch_type.kind != TypeKind::Bool) {
                    throw error(branch.token, "Elif condition must be Bool");
                }
                analyze_block(*branch.body, function_return_type);
            }
            if (if_stmt->else_block != nullptr) {
                analyze_block(*if_stmt->else_block, function_return_type);
            }
            return;
        }
        if (auto* while_stmt = dynamic_cast<WhileStmt*>(&stmt)) {
            const Type condition_type = analyze_expr(*while_stmt->condition);
            if (condition_type.kind != TypeKind::Bool) {
                throw error(while_stmt->token, "While condition must be Bool");
            }
            analyze_block(*while_stmt->body, function_return_type);
            return;
        }
        throw error(stmt.token, "Unknown statement type");
    }

    Type analyze_expr(Expr& expr) {
        if (expr.inferred_type.has_value()) {
            return expr.inferred_type.value();
        }

        if (dynamic_cast<IntExpr*>(&expr) != nullptr) {
            expr.inferred_type = Type{TypeKind::Int};
            return expr.inferred_type.value();
        }
        if (dynamic_cast<BoolExpr*>(&expr) != nullptr) {
            expr.inferred_type = Type{TypeKind::Bool};
            return expr.inferred_type.value();
        }
        if (auto* name_expr = dynamic_cast<NameExpr*>(&expr)) {
            if (const BindingInfo* binding = resolve_local(name_expr->name)) {
                expr.inferred_type = binding->type;
                return binding->type;
            }
            if (const auto global = m_context.globals.find(name_expr->name); global != m_context.globals.end()) {
                expr.inferred_type = global->second.type;
                return global->second.type;
            }
            throw error(name_expr->token, "Unknown binding `" + name_expr->name + "`");
        }
        if (auto* call_expr = dynamic_cast<CallExpr*>(&expr)) {
            const Type call_type = analyze_call(*call_expr, false);
            expr.inferred_type = call_type;
            return call_type;
        }
        if (auto* unary_expr = dynamic_cast<UnaryExpr*>(&expr)) {
            const Type operand_type = analyze_expr(*unary_expr->operand);
            switch (unary_expr->op) {
            case TokenType::minus:
                if (operand_type.kind != TypeKind::Int) {
                    throw error(unary_expr->token, "Unary `-` expects Int");
                }
                expr.inferred_type = Type{TypeKind::Int};
                return expr.inferred_type.value();
            case TokenType::kw_not:
                if (operand_type.kind != TypeKind::Bool) {
                    throw error(unary_expr->token, "`not` expects Bool");
                }
                expr.inferred_type = Type{TypeKind::Bool};
                return expr.inferred_type.value();
            default:
                break;
            }
        }
        if (auto* binary_expr = dynamic_cast<BinaryExpr*>(&expr)) {
            const Type lhs_type = analyze_expr(*binary_expr->lhs);
            const Type rhs_type = analyze_expr(*binary_expr->rhs);
            expr.inferred_type = analyze_binary_expr(binary_expr->token, binary_expr->op, lhs_type, rhs_type);
            return expr.inferred_type.value();
        }

        throw error(expr.token, "Unknown expression type");
    }

    Type analyze_binary_expr(const Token& token, const TokenType op, const Type& lhs_type, const Type& rhs_type) {
        const Type int_type{TypeKind::Int};
        const Type bool_type{TypeKind::Bool};
        switch (op) {
        case TokenType::plus:
        case TokenType::minus:
        case TokenType::star:
        case TokenType::slash:
        case TokenType::percent:
            if (lhs_type.kind != TypeKind::Int || rhs_type.kind != TypeKind::Int) {
                throw error(token, "Operator " + std::string(token_name(op)) + " expects Int operands");
            }
            return int_type;
        case TokenType::less:
        case TokenType::less_eq:
        case TokenType::greater:
        case TokenType::greater_eq:
            if (lhs_type.kind != TypeKind::Int || rhs_type.kind != TypeKind::Int) {
                throw error(token, "Comparison operators expect Int operands");
            }
            return bool_type;
        case TokenType::eq_eq:
        case TokenType::bang_eq:
            if (lhs_type != rhs_type) {
                throw error(token, "Equality operators require matching operand types");
            }
            return bool_type;
        case TokenType::kw_and:
        case TokenType::kw_or:
            if (lhs_type.kind != TypeKind::Bool || rhs_type.kind != TypeKind::Bool) {
                throw error(token, "Logical operators expect Bool operands");
            }
            return bool_type;
        default:
            break;
        }
        throw error(token, "Unsupported operator");
    }

    Type analyze_call(CallExpr& call_expr, const bool allow_statement_only_builtins) {
        if (const auto builtin = m_context.builtins.find(call_expr.callee); builtin != m_context.builtins.end()) {
            return analyze_builtin_call(call_expr, builtin->second, allow_statement_only_builtins);
        }

        const auto function = m_context.functions.find(call_expr.callee);
        if (function == m_context.functions.end()) {
            throw error(call_expr.token, "Unknown function `" + call_expr.callee + "`");
        }
        const FunctionDecl* decl = function->second.decl;
        if (call_expr.args.size() != decl->params.size()) {
            throw error(call_expr.token, "Function `" + call_expr.callee + "` expects " + std::to_string(decl->params.size()) + " argument(s)");
        }
        for (size_t i = 0; i < call_expr.args.size(); ++i) {
            const Type arg_type = analyze_expr(*call_expr.args.at(i));
            if (arg_type != decl->params.at(i).type) {
                throw error(call_expr.token, "Argument " + std::to_string(i + 1) + " to `" + call_expr.callee + "` has type " + arg_type.display_name() + ", expected " + decl->params.at(i).type.display_name());
            }
        }
        return decl->return_type;
    }

    Type analyze_builtin_call(CallExpr& call_expr, const BuiltinInfo& builtin, const bool allow_statement_only_builtins) {
        switch (builtin.kind) {
        case BuiltinKind::Print:
            if (!allow_statement_only_builtins) {
                throw error(call_expr.token, "Builtin `print` cannot be used as an expression");
            }
            if (call_expr.args.size() != 1) {
                throw error(call_expr.token, "Builtin `print` expects 1 argument(s)");
            }
            {
                const Type arg_type = analyze_expr(*call_expr.args.front());
                if (arg_type.kind != TypeKind::Int && arg_type.kind != TypeKind::Bool) {
                    throw error(call_expr.token, "Builtin `print` does not support argument type " + arg_type.display_name());
                }
            }
            return Type{TypeKind::Int};
        }

        throw error(call_expr.token, "Unknown builtin `" + call_expr.callee + "`");
    }

    void declare_local(const Token& token, const std::string& name, const BindingInfo& binding) {
        auto& scope = m_scopes.back();
        if (scope.contains(name)) {
            throw error(token, "Binding `" + name + "` is already declared in this scope");
        }
        scope.emplace(name, binding);
    }

    [[nodiscard]] const BindingInfo* resolve_local(const std::string& name) const {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (const auto found = it->find(name); found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    void push_scope() {
        m_scopes.emplace_back();
    }

    void pop_scope() {
        m_scopes.pop_back();
    }

    bool block_guarantees_return(const Block& block) const {
        for (const auto& stmt : block.statements) {
            if (stmt_guarantees_return(*stmt)) {
                return true;
            }
        }
        return false;
    }

    bool stmt_guarantees_return(const Stmt& stmt) const {
        if (dynamic_cast<const ReturnStmt*>(&stmt) != nullptr) {
            return true;
        }
        if (const auto* if_stmt = dynamic_cast<const IfStmt*>(&stmt)) {
            if (if_stmt->else_block == nullptr) {
                return false;
            }
            if (!block_guarantees_return(*if_stmt->then_block)) {
                return false;
            }
            for (const auto& branch : if_stmt->elif_branches) {
                if (!block_guarantees_return(*branch.body)) {
                    return false;
                }
            }
            return block_guarantees_return(*if_stmt->else_block);
        }
        return false;
    }

    void validate_main_signature() const {
        const auto main_it = m_context.functions.find("main");
        if (main_it == m_context.functions.end()) {
            throw CompileError("[Semantic Error] missing entrypoint `main`");
        }
        const FunctionDecl* main_fn = main_it->second.decl;
        if (!main_fn->params.empty()) {
            throw error(main_fn->token, "`main` must not take parameters");
        }
        if (main_fn->return_type.kind != TypeKind::Int) {
            throw error(main_fn->token, "`main` must return Int");
        }
    }

    static void require_type(const Token& token, const Type& actual, const Type& expected, const std::string& message) {
        if (actual != expected) {
            throw error(token, message);
        }
    }

    [[nodiscard]] static CompileError error(const Token& token, const std::string& message) {
        return make_error("Semantic", token, message);
    }

    Program& m_program;
    SemanticContext m_context;
    std::unordered_map<std::string, VisitState> m_global_state;
    std::vector<std::unordered_map<std::string, BindingInfo>> m_scopes;
};

struct BackendOutput {
    std::string filename;
    std::string source;
};

class CBackend {
  public:
    CBackend(const Program& program, const SemanticContext& semantics)
        : m_program(program), m_semantics(semantics) {
    }

    [[nodiscard]] BackendOutput generate(const std::string& c_filename) {
        reset();
        classify_declarations();
        emit_prelude();
        emit_global_consts();
        emit_function_prototypes();
        emit_function_definitions();
        emit_line("int main(void) {");
        ++m_indent;
        emit_line("return rivel_exit_code(rivel_fn_main());");
        --m_indent;
        emit_line("}");
        return {.filename = c_filename, .source = m_output.str()};
    }

  private:
    struct LocalBinding {
        std::string c_name;
        Type type;
    };

    void reset() {
        m_output.str("");
        m_output.clear();
        m_indent = 0;
        m_globals.clear();
        m_functions.clear();
    }

    void classify_declarations() {
        for (const auto& decl : m_program.decls) {
            if (const auto* global = dynamic_cast<const GlobalConstDecl*>(decl.get())) {
                m_globals.push_back(global);
            } else if (const auto* function = dynamic_cast<const FunctionDecl*>(decl.get())) {
                m_functions.push_back(function);
            }
        }
    }

    void emit_prelude() {
        emit_line("#include <stdbool.h>");
        emit_line("#include <stdint.h>");
        emit_line("#include <stdio.h>");
        emit_line("#include <stdlib.h>");
        emit_line();
        emit_line("static int rivel_exit_code(int64_t value) {");
        ++m_indent;
        emit_line("return (int)value;");
        --m_indent;
        emit_line("}");
        emit_line();
        emit_line("static void rivel_check_divisor(int64_t rhs) {");
        ++m_indent;
        emit_line("if (rhs == INT64_C(0)) {");
        ++m_indent;
        emit_line("fputs(\"division by zero\\n\", stderr);");
        emit_line("exit(1);");
        --m_indent;
        emit_line("}");
        --m_indent;
        emit_line("}");
        emit_line();
        emit_line("static int64_t rivel_div(int64_t lhs, int64_t rhs) {");
        ++m_indent;
        emit_line("rivel_check_divisor(rhs);");
        emit_line("return lhs / rhs;");
        --m_indent;
        emit_line("}");
        emit_line();
        emit_line("static int64_t rivel_mod(int64_t lhs, int64_t rhs) {");
        ++m_indent;
        emit_line("rivel_check_divisor(rhs);");
        emit_line("return lhs % rhs;");
        --m_indent;
        emit_line("}");
        emit_line();
        emit_line("static void rivel_print_int(int64_t value) {");
        ++m_indent;
        emit_line("printf(\"%lld\\n\", (long long)value);");
        --m_indent;
        emit_line("}");
        emit_line();
        emit_line("static void rivel_print_bool(bool value) {");
        ++m_indent;
        emit_line("puts(value ? \"true\" : \"false\");");
        --m_indent;
        emit_line("}");
        emit_line();
    }

    void emit_global_consts() {
        for (const auto* global : m_globals) {
            const auto& info = m_semantics.globals.at(global->name);
            emit_line("static const " + c_type(info.type) + " " + c_global_name(global->name) + " = " + literal(info.value) + ";");
        }
        if (!m_globals.empty()) {
            emit_line();
        }
    }

    void emit_function_prototypes() {
        for (const auto* function : m_functions) {
            emit_line(function_signature(*function) + ";");
        }
        emit_line();
    }

    void emit_function_definitions() {
        for (const auto* function : m_functions) {
            emit_function(*function);
            emit_line();
        }
    }

    void emit_function(const FunctionDecl& function) {
        emit_line(function_signature(function) + " {");
        ++m_indent;
        m_next_local_id = 0;
        m_scopes.clear();
        push_scope();
        for (const Param& param : function.params) {
            m_scopes.back().emplace(param.name, LocalBinding{param_c_name(param.name), param.type});
        }
        emit_block(*function.body);
        pop_scope();
        --m_indent;
        emit_line("}");
    }

    void emit_block(const Block& block) {
        push_scope();
        for (const auto& stmt : block.statements) {
            emit_stmt(*stmt);
        }
        pop_scope();
    }

    void emit_nested_block(const Block& block) {
        emit_line("{");
        ++m_indent;
        emit_block(block);
        --m_indent;
        emit_line("}");
    }

    void emit_stmt(const Stmt& stmt) {
        if (const auto* binding = dynamic_cast<const BindingStmt*>(&stmt)) {
            const std::string c_name = make_local_name(binding->name);
            const Type type = binding->annotation.value_or(binding->initializer->inferred_type.value());
            emit_line(c_type(type) + " " + c_name + " = " + emit_expr(*binding->initializer) + ";");
            m_scopes.back().emplace(binding->name, LocalBinding{c_name, type});
            return;
        }
        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            emit_line(resolve_name(assign->name) + " = " + emit_expr(*assign->value) + ";");
            return;
        }
        if (const auto* return_stmt = dynamic_cast<const ReturnStmt*>(&stmt)) {
            emit_line("return " + emit_expr(*return_stmt->value) + ";");
            return;
        }
        if (const auto* call_stmt = dynamic_cast<const CallStmt*>(&stmt)) {
            emit_call_stmt(*call_stmt->call);
            return;
        }
        if (const auto* if_stmt = dynamic_cast<const IfStmt*>(&stmt)) {
            emit_line("if " + condition_expr(*if_stmt->condition) + " {");
            ++m_indent;
            emit_block(*if_stmt->then_block);
            --m_indent;
            emit_line("}");
            for (const auto& branch : if_stmt->elif_branches) {
                emit_line("else if " + condition_expr(*branch.condition) + " {");
                ++m_indent;
                emit_block(*branch.body);
                --m_indent;
                emit_line("}");
            }
            if (if_stmt->else_block != nullptr) {
                emit_line("else {");
                ++m_indent;
                emit_block(*if_stmt->else_block);
                --m_indent;
                emit_line("}");
            }
            return;
        }
        if (const auto* while_stmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            emit_line("while " + condition_expr(*while_stmt->condition) + " {");
            ++m_indent;
            emit_block(*while_stmt->body);
            --m_indent;
            emit_line("}");
            return;
        }
    }

    void emit_call_stmt(const CallExpr& call) {
        if (call.callee == "print") {
            const Expr& arg = *call.args.front();
            const Type arg_type = arg.inferred_type.value();
            if (arg_type.kind == TypeKind::Bool) {
                emit_line("rivel_print_bool(" + emit_expr(arg) + ");");
            } else {
                emit_line("rivel_print_int(" + emit_expr(arg) + ");");
            }
            return;
        }

        emit_line(emit_expr(call) + ";");
    }

    [[nodiscard]] std::string emit_expr(const Expr& expr) {
        if (const auto* int_expr = dynamic_cast<const IntExpr*>(&expr)) {
            return "INT64_C(" + std::to_string(int_expr->value) + ")";
        }
        if (const auto* bool_expr = dynamic_cast<const BoolExpr*>(&expr)) {
            return bool_expr->value ? "true" : "false";
        }
        if (const auto* name_expr = dynamic_cast<const NameExpr*>(&expr)) {
            return resolve_name(name_expr->name);
        }
        if (const auto* call_expr = dynamic_cast<const CallExpr*>(&expr)) {
            if (call_expr->callee == "print") {
                return "<builtin-print>";
            }
            std::vector<std::string> args;
            args.reserve(call_expr->args.size());
            for (const auto& arg : call_expr->args) {
                args.push_back(emit_expr(*arg));
            }
            return c_function_name(call_expr->callee) + "(" + join(args) + ")";
        }
        if (const auto* unary_expr = dynamic_cast<const UnaryExpr*>(&expr)) {
            if (unary_expr->op == TokenType::minus) {
                return "(-" + emit_expr(*unary_expr->operand) + ")";
            }
            return "(!" + emit_expr(*unary_expr->operand) + ")";
        }
        if (const auto* binary_expr = dynamic_cast<const BinaryExpr*>(&expr)) {
            const std::string lhs = emit_expr(*binary_expr->lhs);
            if (binary_expr->op == TokenType::kw_and) {
                return "(" + lhs + " && " + emit_expr(*binary_expr->rhs) + ")";
            }
            if (binary_expr->op == TokenType::kw_or) {
                return "(" + lhs + " || " + emit_expr(*binary_expr->rhs) + ")";
            }
            const std::string rhs = emit_expr(*binary_expr->rhs);
            switch (binary_expr->op) {
            case TokenType::plus:
                return "(" + lhs + " + " + rhs + ")";
            case TokenType::minus:
                return "(" + lhs + " - " + rhs + ")";
            case TokenType::star:
                return "(" + lhs + " * " + rhs + ")";
            case TokenType::slash:
                return "rivel_div(" + lhs + ", " + rhs + ")";
            case TokenType::percent:
                return "rivel_mod(" + lhs + ", " + rhs + ")";
            case TokenType::eq_eq:
                return "(" + lhs + " == " + rhs + ")";
            case TokenType::bang_eq:
                return "(" + lhs + " != " + rhs + ")";
            case TokenType::less:
                return "(" + lhs + " < " + rhs + ")";
            case TokenType::less_eq:
                return "(" + lhs + " <= " + rhs + ")";
            case TokenType::greater:
                return "(" + lhs + " > " + rhs + ")";
            case TokenType::greater_eq:
                return "(" + lhs + " >= " + rhs + ")";
            default:
                break;
            }
        }
        return "<expr>";
    }

    [[nodiscard]] std::string condition_expr(const Expr& expr) {
        const std::string emitted = emit_expr(expr);
        if (is_wrapped_once(emitted)) {
            return emitted;
        }
        return "(" + emitted + ")";
    }

    [[nodiscard]] static bool is_wrapped_once(const std::string& text) {
        if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
            return false;
        }

        int depth = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '(') {
                ++depth;
            } else if (text[i] == ')') {
                --depth;
                if (depth == 0 && i + 1 != text.size()) {
                    return false;
                }
            }
        }
        return depth == 0;
    }

    [[nodiscard]] std::string resolve_name(const std::string& name) const {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (const auto found = it->find(name); found != it->end()) {
                return found->second.c_name;
            }
        }
        if (m_semantics.globals.contains(name)) {
            return c_global_name(name);
        }
        return "<unknown>";
    }

    void push_scope() {
        m_scopes.emplace_back();
    }

    void pop_scope() {
        m_scopes.pop_back();
    }

    [[nodiscard]] std::string function_signature(const FunctionDecl& function) const {
        std::vector<std::string> params;
        params.reserve(function.params.size());
        for (const Param& param : function.params) {
            params.push_back(c_type(param.type) + " " + param_c_name(param.name));
        }
        return "static " + c_type(function.return_type) + " " + c_function_name(function.name) + "(" + (params.empty() ? "void" : join(params)) + ")";
    }

    [[nodiscard]] static std::string c_type(const Type& type) {
        return type.kind == TypeKind::Int ? "int64_t" : "bool";
    }

    [[nodiscard]] static std::string c_function_name(const std::string& name) {
        return "rivel_fn_" + name;
    }

    [[nodiscard]] static std::string c_global_name(const std::string& name) {
        return "rivel_global_" + name;
    }

    [[nodiscard]] static std::string param_c_name(const std::string& name) {
        return "rivel_param_" + name;
    }

    [[nodiscard]] std::string make_local_name(const std::string& name) {
        return "rivel_local_" + name + "_" + std::to_string(m_next_local_id++);
    }

    [[nodiscard]] static std::string literal(const ConstValue& value) {
        if (value.type.kind == TypeKind::Bool) {
            return value.bool_value ? "true" : "false";
        }
        return "INT64_C(" + std::to_string(value.int_value) + ")";
    }

    [[nodiscard]] static std::string join(const std::vector<std::string>& parts) {
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += parts.at(i);
        }
        return out;
    }

    void emit_line(std::string_view line = {}) {
        if (line.empty()) {
            m_output << '\n';
            return;
        }
        for (int i = 0; i < m_indent; ++i) {
            m_output << "    ";
        }
        m_output << line << '\n';
    }

    const Program& m_program;
    const SemanticContext& m_semantics;
    std::vector<const GlobalConstDecl*> m_globals;
    std::vector<const FunctionDecl*> m_functions;
    std::stringstream m_output;
    int m_indent = 0;
    size_t m_next_local_id = 0;
    std::vector<std::unordered_map<std::string, LocalBinding>> m_scopes;
};
