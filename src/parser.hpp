#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tokenization.hpp"

enum class TypeKind {
    Int,
    Bool,
};

struct Type {
    TypeKind kind;

    [[nodiscard]] std::string display_name() const {
        switch (kind) {
        case TypeKind::Int:
            return "Int";
        case TypeKind::Bool:
            return "Bool";
        }
        return "<type>";
    }

    [[nodiscard]] bool operator==(const Type& other) const {
        return kind == other.kind;
    }

    [[nodiscard]] bool operator!=(const Type& other) const {
        return !(*this == other);
    }
};

struct Expr {
    virtual ~Expr() = default;
    Token token;
    std::optional<Type> inferred_type;
};

struct IntExpr final : Expr {
    int64_t value = 0;
};

struct BoolExpr final : Expr {
    bool value = false;
};

struct NameExpr final : Expr {
    std::string name;
};

struct UnaryExpr final : Expr {
    TokenType op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr final : Expr {
    TokenType op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

struct CallExpr final : Expr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
};

using ExprPtr = std::unique_ptr<Expr>;

struct Stmt {
    virtual ~Stmt() = default;
    Token token;
};

struct BindingStmt final : Stmt {
    bool is_mutable = false;
    std::string name;
    std::optional<Type> annotation;
    ExprPtr initializer;
};

struct AssignStmt final : Stmt {
    std::string name;
    ExprPtr value;
};

struct ReturnStmt final : Stmt {
    ExprPtr value;
};

struct CallStmt final : Stmt {
    std::unique_ptr<CallExpr> call;
};

struct Block;

struct IfBranch {
    Token token;
    ExprPtr condition;
    std::unique_ptr<Block> body;
};

struct IfStmt final : Stmt {
    ExprPtr condition;
    std::unique_ptr<Block> then_block;
    std::vector<IfBranch> elif_branches;
    std::unique_ptr<Block> else_block;
};

struct WhileStmt final : Stmt {
    ExprPtr condition;
    std::unique_ptr<Block> body;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct Block {
    Token token;
    std::vector<StmtPtr> statements;
};

struct Param {
    Token token;
    std::string name;
    Type type;
};

struct TopLevelDecl {
    virtual ~TopLevelDecl() = default;
    Token token;
    std::string name;
};

struct GlobalConstDecl final : TopLevelDecl {
    std::optional<Type> annotation;
    ExprPtr initializer;
};

struct FunctionDecl final : TopLevelDecl {
    std::vector<Param> params;
    Type return_type;
    std::unique_ptr<Block> body;
};

struct Program {
    std::vector<std::unique_ptr<TopLevelDecl>> decls;
};

class Parser {
  public:
    explicit Parser(std::vector<Token> tokens)
        : m_tokens(std::move(tokens)) {
    }

    Program parse_program() {
        Program program;
        skip_separators();
        while (!is(TokenType::eof)) {
            if (is(TokenType::kw_const)) {
                program.decls.push_back(parse_global_const());
            } else if (is(TokenType::kw_fn)) {
                program.decls.push_back(parse_function());
            } else if (is(TokenType::kw_mut)) {
                throw error(peek(), "Top-level `mut` declarations are not part of Rivel v1");
            } else {
                throw error(peek(), "Expected a top-level `const` or `fn` declaration");
            }
            consume_declaration_end();
            skip_separators();
        }
        return program;
    }

  private:
    [[nodiscard]] const Token& peek(const size_t offset = 0) const {
        const size_t index = m_index + offset;
        if (index >= m_tokens.size()) {
            return m_tokens.back();
        }
        return m_tokens.at(index);
    }

    [[nodiscard]] bool is(const TokenType type, const size_t offset = 0) const {
        return peek(offset).type == type;
    }

    const Token& advance() {
        return m_tokens.at(m_index++);
    }

    const Token& expect(const TokenType type, const std::string& message) {
        if (!is(type)) {
            throw error(peek(), message);
        }
        return advance();
    }

    bool match(const TokenType type) {
        if (!is(type)) {
            return false;
        }
        advance();
        return true;
    }

    void skip_separators() {
        while (match(TokenType::end_stmt)) {
        }
    }

    void consume_declaration_end() {
        if (match(TokenType::end_stmt)) {
            skip_separators();
            return;
        }
        if (is(TokenType::eof)) {
            return;
        }
        throw error(peek(), "Expected a declaration separator after top-level declaration");
    }

    void require_stmt_end(const std::string& after_what) {
        if (match(TokenType::end_stmt)) {
            skip_separators();
            return;
        }
        if (is(TokenType::close_brace) || is(TokenType::eof)) {
            return;
        }
        throw error(peek(), "Expected a statement separator after " + after_what);
    }

    std::unique_ptr<TopLevelDecl> parse_global_const() {
        const Token token = expect(TokenType::kw_const, "Expected `const`");
        const Token& name_token = expect(TokenType::identifier, "Expected a constant name");
        auto decl = std::make_unique<GlobalConstDecl>();
        decl->token = token;
        decl->name = name_token.lexeme;
        if (match(TokenType::colon)) {
            decl->annotation = parse_type();
        }
        expect(TokenType::assign, "Expected `=` in constant declaration");
        decl->initializer = parse_expression();
        return decl;
    }

    std::unique_ptr<TopLevelDecl> parse_function() {
        const Token token = expect(TokenType::kw_fn, "Expected `fn`");
        const Token& name_token = expect(TokenType::identifier, "Expected a function name");
        auto decl = std::make_unique<FunctionDecl>();
        decl->token = token;
        decl->name = name_token.lexeme;
        expect(TokenType::open_paren, "Expected `(` after function name");
        if (!is(TokenType::close_paren)) {
            do {
                decl->params.push_back(parse_param());
            } while (match(TokenType::comma));
        }
        expect(TokenType::close_paren, "Expected `)` after parameter list");
        expect(TokenType::arrow, "Expected `->` before function return type");
        decl->return_type = parse_type();
        decl->body = parse_block();
        return decl;
    }

    Param parse_param() {
        const Token& name_token = expect(TokenType::identifier, "Expected a parameter name");
        expect(TokenType::colon, "Expected `:` after parameter name");
        Param param;
        param.token = name_token;
        param.name = name_token.lexeme;
        param.type = parse_type();
        return param;
    }

    std::unique_ptr<Block> parse_block() {
        const Token token = expect(TokenType::open_brace, "Expected `{` to start a block");
        auto block = std::make_unique<Block>();
        block->token = token;
        skip_separators();
        while (!is(TokenType::close_brace)) {
            if (is(TokenType::eof)) {
                throw error(peek(), "Expected `}` to close the block");
            }
            block->statements.push_back(parse_statement());
            skip_separators();
        }
        expect(TokenType::close_brace, "Expected `}` to close the block");
        return block;
    }

    StmtPtr parse_statement() {
        if (is(TokenType::kw_const) || is(TokenType::kw_mut)) {
            return parse_binding_statement();
        }
        if (is(TokenType::kw_return)) {
            return parse_return_statement();
        }
        if (is(TokenType::kw_if)) {
            return parse_if_statement();
        }
        if (is(TokenType::kw_while)) {
            return parse_while_statement();
        }
        if (is(TokenType::identifier) && is(TokenType::assign, 1)) {
            return parse_assign_statement();
        }
        if (is(TokenType::identifier) && is(TokenType::open_paren, 1)) {
            return parse_call_statement();
        }
        throw error(peek(), "Expected a statement");
    }

    StmtPtr parse_binding_statement() {
        const Token token = advance();
        const bool is_mutable = token.type == TokenType::kw_mut;
        const Token& name_token = expect(TokenType::identifier, "Expected a binding name");
        auto stmt = std::make_unique<BindingStmt>();
        stmt->token = token;
        stmt->is_mutable = is_mutable;
        stmt->name = name_token.lexeme;
        if (match(TokenType::colon)) {
            stmt->annotation = parse_type();
        }
        expect(TokenType::assign, "Expected `=` in binding declaration");
        stmt->initializer = parse_expression();
        require_stmt_end("binding declaration");
        return stmt;
    }

    StmtPtr parse_assign_statement() {
        const Token& name_token = expect(TokenType::identifier, "Expected a binding name");
        auto stmt = std::make_unique<AssignStmt>();
        stmt->token = name_token;
        stmt->name = name_token.lexeme;
        expect(TokenType::assign, "Expected `=` in assignment");
        stmt->value = parse_expression();
        require_stmt_end("assignment");
        return stmt;
    }

    StmtPtr parse_return_statement() {
        const Token token = expect(TokenType::kw_return, "Expected `return`");
        auto stmt = std::make_unique<ReturnStmt>();
        stmt->token = token;
        stmt->value = parse_expression();
        require_stmt_end("return statement");
        return stmt;
    }

    StmtPtr parse_call_statement() {
        auto expr = parse_expression();
        auto* call_expr = dynamic_cast<CallExpr*>(expr.get());
        if (call_expr == nullptr) {
            throw error(expr->token, "Only function calls can be used as statements");
        }

        auto stmt = std::make_unique<CallStmt>();
        stmt->token = call_expr->token;
        stmt->call.reset(static_cast<CallExpr*>(expr.release()));
        require_stmt_end("call statement");
        return stmt;
    }

    StmtPtr parse_if_statement() {
        const Token token = expect(TokenType::kw_if, "Expected `if`");
        if (is(TokenType::open_paren)) {
            throw error(peek(), "Parenthesized conditions are not supported in Rivel v1");
        }
        auto stmt = std::make_unique<IfStmt>();
        stmt->token = token;
        stmt->condition = parse_expression();
        stmt->then_block = parse_block();
        while (match(TokenType::kw_elif)) {
            const Token elif_token = m_tokens.at(m_index - 1);
            if (is(TokenType::open_paren)) {
                throw error(peek(), "Parenthesized conditions are not supported in Rivel v1");
            }
            IfBranch branch;
            branch.token = elif_token;
            branch.condition = parse_expression();
            branch.body = parse_block();
            stmt->elif_branches.push_back(std::move(branch));
        }
        if (match(TokenType::kw_else)) {
            stmt->else_block = parse_block();
        }
        return stmt;
    }

    StmtPtr parse_while_statement() {
        const Token token = expect(TokenType::kw_while, "Expected `while`");
        if (is(TokenType::open_paren)) {
            throw error(peek(), "Parenthesized conditions are not supported in Rivel v1");
        }
        auto stmt = std::make_unique<WhileStmt>();
        stmt->token = token;
        stmt->condition = parse_expression();
        stmt->body = parse_block();
        return stmt;
    }

    Type parse_type() {
        if (match(TokenType::kw_type_int)) {
            return Type{TypeKind::Int};
        }
        if (match(TokenType::kw_type_bool)) {
            return Type{TypeKind::Bool};
        }
        if (is(TokenType::identifier)) {
            throw error(peek(), "Unknown type `" + peek().lexeme + "`");
        }
        throw error(peek(), "Expected a type name");
    }

    ExprPtr parse_expression() {
        return parse_or();
    }

    ExprPtr parse_or() {
        auto expr = parse_and();
        while (match(TokenType::kw_or)) {
            const Token op = m_tokens.at(m_index - 1);
            auto rhs = parse_and();
            expr = make_binary(op, std::move(expr), std::move(rhs));
        }
        return expr;
    }

    ExprPtr parse_and() {
        auto expr = parse_equality();
        while (match(TokenType::kw_and)) {
            const Token op = m_tokens.at(m_index - 1);
            auto rhs = parse_equality();
            expr = make_binary(op, std::move(expr), std::move(rhs));
        }
        return expr;
    }

    ExprPtr parse_equality() {
        auto expr = parse_comparison();
        while (match(TokenType::eq_eq) || match(TokenType::bang_eq)) {
            const Token op = m_tokens.at(m_index - 1);
            auto rhs = parse_comparison();
            expr = make_binary(op, std::move(expr), std::move(rhs));
        }
        return expr;
    }

    ExprPtr parse_comparison() {
        auto expr = parse_additive();
        while (match(TokenType::less) || match(TokenType::less_eq) || match(TokenType::greater) || match(TokenType::greater_eq)) {
            const Token op = m_tokens.at(m_index - 1);
            auto rhs = parse_additive();
            expr = make_binary(op, std::move(expr), std::move(rhs));
        }
        return expr;
    }

    ExprPtr parse_additive() {
        auto expr = parse_multiplicative();
        while (match(TokenType::plus) || match(TokenType::minus)) {
            const Token op = m_tokens.at(m_index - 1);
            auto rhs = parse_multiplicative();
            expr = make_binary(op, std::move(expr), std::move(rhs));
        }
        return expr;
    }

    ExprPtr parse_multiplicative() {
        auto expr = parse_unary();
        while (match(TokenType::star) || match(TokenType::slash) || match(TokenType::percent)) {
            const Token op = m_tokens.at(m_index - 1);
            auto rhs = parse_unary();
            expr = make_binary(op, std::move(expr), std::move(rhs));
        }
        return expr;
    }

    ExprPtr parse_unary() {
        if (match(TokenType::kw_not) || match(TokenType::minus)) {
            const Token op = m_tokens.at(m_index - 1);
            auto expr = std::make_unique<UnaryExpr>();
            expr->token = op;
            expr->op = op.type;
            expr->operand = parse_unary();
            return expr;
        }
        return parse_call();
    }

    ExprPtr parse_call() {
        auto expr = parse_primary();
        while (match(TokenType::open_paren)) {
            const Token open_paren = m_tokens.at(m_index - 1);
            auto* callee = dynamic_cast<NameExpr*>(expr.get());
            if (callee == nullptr) {
                throw error(open_paren, "Only named functions can be called in Rivel v1");
            }

            auto call = std::make_unique<CallExpr>();
            call->token = callee->token;
            call->callee = callee->name;
            if (!is(TokenType::close_paren)) {
                do {
                    call->args.push_back(parse_expression());
                } while (match(TokenType::comma));
            }
            expect(TokenType::close_paren, "Expected `)` after call arguments");
            expr = std::move(call);
        }
        return expr;
    }

    ExprPtr parse_primary() {
        if (match(TokenType::int_literal)) {
            const Token token = m_tokens.at(m_index - 1);
            auto expr = std::make_unique<IntExpr>();
            expr->token = token;
            try {
                expr->value = std::stoll(token.lexeme);
            } catch (const std::out_of_range&) {
                throw error(token, "Integer literal out of range for Int");
            }
            return expr;
        }
        if (match(TokenType::bool_literal)) {
            const Token token = m_tokens.at(m_index - 1);
            auto expr = std::make_unique<BoolExpr>();
            expr->token = token;
            expr->value = token.lexeme == "true";
            return expr;
        }
        if (match(TokenType::identifier)) {
            const Token token = m_tokens.at(m_index - 1);
            auto expr = std::make_unique<NameExpr>();
            expr->token = token;
            expr->name = token.lexeme;
            return expr;
        }
        if (match(TokenType::open_paren)) {
            auto expr = parse_expression();
            expect(TokenType::close_paren, "Expected `)` after grouped expression");
            return expr;
        }
        throw error(peek(), "Expected an expression");
    }

    static ExprPtr make_binary(const Token& op, ExprPtr lhs, ExprPtr rhs) {
        auto expr = std::make_unique<BinaryExpr>();
        expr->token = op;
        expr->op = op.type;
        expr->lhs = std::move(lhs);
        expr->rhs = std::move(rhs);
        return expr;
    }

    [[nodiscard]] static CompileError error(const Token& token, const std::string& message) {
        return make_error("Parse", token, message);
    }

    std::vector<Token> m_tokens;
    size_t m_index = 0;
};
