#pragma once

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class CompileError : public std::runtime_error {
  public:
    explicit CompileError(const std::string& message)
        : std::runtime_error(message) {
    }
};

enum class TokenType {
    eof,
    end_stmt,
    identifier,
    int_literal,
    bool_literal,
    kw_const,
    kw_mut,
    kw_fn,
    kw_return,
    kw_if,
    kw_elif,
    kw_else,
    kw_while,
    kw_and,
    kw_or,
    kw_not,
    kw_type_int,
    kw_type_bool,
    open_paren,
    close_paren,
    open_brace,
    close_brace,
    comma,
    colon,
    arrow,
    assign,
    plus,
    minus,
    star,
    slash,
    percent,
    eq_eq,
    bang_eq,
    less,
    less_eq,
    greater,
    greater_eq,
};

inline std::string_view token_name(const TokenType type) {
    switch (type) {
    case TokenType::eof:
        return "end of file";
    case TokenType::end_stmt:
        return "statement separator";
    case TokenType::identifier:
        return "identifier";
    case TokenType::int_literal:
        return "integer literal";
    case TokenType::bool_literal:
        return "boolean literal";
    case TokenType::kw_const:
        return "`const`";
    case TokenType::kw_mut:
        return "`mut`";
    case TokenType::kw_fn:
        return "`fn`";
    case TokenType::kw_return:
        return "`return`";
    case TokenType::kw_if:
        return "`if`";
    case TokenType::kw_elif:
        return "`elif`";
    case TokenType::kw_else:
        return "`else`";
    case TokenType::kw_while:
        return "`while`";
    case TokenType::kw_and:
        return "`and`";
    case TokenType::kw_or:
        return "`or`";
    case TokenType::kw_not:
        return "`not`";
    case TokenType::kw_type_int:
        return "`Int`";
    case TokenType::kw_type_bool:
        return "`Bool`";
    case TokenType::open_paren:
        return "`(`";
    case TokenType::close_paren:
        return "`)`";
    case TokenType::open_brace:
        return "`{`";
    case TokenType::close_brace:
        return "`}`";
    case TokenType::comma:
        return "`,`";
    case TokenType::colon:
        return "`:`";
    case TokenType::arrow:
        return "`->`";
    case TokenType::assign:
        return "`=`";
    case TokenType::plus:
        return "`+`";
    case TokenType::minus:
        return "`-`";
    case TokenType::star:
        return "`*`";
    case TokenType::slash:
        return "`/`";
    case TokenType::percent:
        return "`%`";
    case TokenType::eq_eq:
        return "`==`";
    case TokenType::bang_eq:
        return "`!=`";
    case TokenType::less:
        return "`<`";
    case TokenType::less_eq:
        return "`<=`";
    case TokenType::greater:
        return "`>`";
    case TokenType::greater_eq:
        return "`>=`";
    }
    return "token";
}

struct Token {
    TokenType type;
    std::string lexeme;
    int line = 1;
    int column = 1;
};

inline CompileError make_error(std::string_view phase, int line, int column, const std::string& message) {
    return CompileError("[" + std::string(phase) + " Error] line " + std::to_string(line) + ":" + std::to_string(column) + " " + message);
}

inline CompileError make_error(std::string_view phase, const Token& token, const std::string& message) {
    return make_error(phase, token.line, token.column, message);
}

class Tokenizer {
  public:
    explicit Tokenizer(std::string source)
        : m_source(std::move(source)) {
    }

    std::vector<Token> tokenize() {
        while (!is_at_end()) {
            const char current = peek();
            if (current == ' ' || current == '\t' || current == '\r') {
                advance();
                continue;
            }
            if (current == '\n') {
                emit_newline_separator();
                continue;
            }
            if (current == '#') {
                skip_comment();
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
                tokenize_identifier();
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(current))) {
                tokenize_int_literal();
                continue;
            }

            const int token_line = m_line;
            const int token_column = m_column;
            switch (current) {
            case ';':
                advance();
                emit_separator(token_line, token_column);
                break;
            case '(':
                advance();
                ++m_paren_depth;
                add_token(TokenType::open_paren, "(", token_line, token_column);
                break;
            case ')':
                advance();
                if (m_paren_depth > 0) {
                    --m_paren_depth;
                }
                add_token(TokenType::close_paren, ")", token_line, token_column);
                break;
            case '{':
                advance();
                add_token(TokenType::open_brace, "{", token_line, token_column);
                break;
            case '}':
                advance();
                add_token(TokenType::close_brace, "}", token_line, token_column);
                break;
            case ',':
                advance();
                add_token(TokenType::comma, ",", token_line, token_column);
                break;
            case ':':
                advance();
                add_token(TokenType::colon, ":", token_line, token_column);
                break;
            case '+':
                advance();
                add_token(TokenType::plus, "+", token_line, token_column);
                break;
            case '-':
                advance();
                if (!is_at_end() && peek() == '>') {
                    advance();
                    add_token(TokenType::arrow, "->", token_line, token_column);
                } else {
                    add_token(TokenType::minus, "-", token_line, token_column);
                }
                break;
            case '*':
                advance();
                add_token(TokenType::star, "*", token_line, token_column);
                break;
            case '/':
                advance();
                if (!is_at_end() && (peek() == '/' || peek() == '*')) {
                    throw error(token_line, token_column, "Rivel v1 only supports `#` comments");
                }
                add_token(TokenType::slash, "/", token_line, token_column);
                break;
            case '%':
                advance();
                add_token(TokenType::percent, "%", token_line, token_column);
                break;
            case '=':
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    add_token(TokenType::eq_eq, "==", token_line, token_column);
                } else {
                    add_token(TokenType::assign, "=", token_line, token_column);
                }
                break;
            case '!':
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    add_token(TokenType::bang_eq, "!=", token_line, token_column);
                } else {
                    throw error(token_line, token_column, "Unexpected `!`; use `not` for logical negation");
                }
                break;
            case '<':
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    add_token(TokenType::less_eq, "<=", token_line, token_column);
                } else {
                    add_token(TokenType::less, "<", token_line, token_column);
                }
                break;
            case '>':
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    add_token(TokenType::greater_eq, ">=", token_line, token_column);
                } else {
                    add_token(TokenType::greater, ">", token_line, token_column);
                }
                break;
            case '"':
                throw error(token_line, token_column, "String literals are not part of Rivel v1");
            case '\'':
                throw error(token_line, token_column, "Character literals are not part of Rivel v1");
            case '[':
            case ']':
                throw error(token_line, token_column, "List syntax is not part of Rivel v1");
            case '.':
                throw error(token_line, token_column, "Member access is not part of Rivel v1");
            default:
                throw error(token_line, token_column, "Unexpected character `" + std::string(1, current) + "`");
            }
        }

        add_token(TokenType::eof, "", m_line, m_column);
        return m_tokens;
    }

  private:
    [[nodiscard]] bool is_at_end() const {
        return m_index >= m_source.size();
    }

    [[nodiscard]] char peek(const size_t offset = 0) const {
        return m_source.at(m_index + offset);
    }

    char advance() {
        const char ch = m_source.at(m_index++);
        if (ch == '\n') {
            ++m_line;
            m_column = 1;
        } else {
            ++m_column;
        }
        return ch;
    }

    void add_token(const TokenType type, std::string lexeme, const int line, const int column) {
        m_tokens.push_back(Token{type, std::move(lexeme), line, column});
    }

    void emit_separator(const int line, const int column) {
        if (!m_tokens.empty() && m_tokens.back().type == TokenType::end_stmt) {
            return;
        }
        add_token(TokenType::end_stmt, ";", line, column);
    }

    void emit_newline_separator() {
        const int line = m_line;
        const int column = m_column;
        advance();
        if (m_paren_depth == 0) {
            emit_separator(line, column);
        }
    }

    void skip_comment() {
        while (!is_at_end() && peek() != '\n') {
            advance();
        }
    }

    void tokenize_identifier() {
        const int token_line = m_line;
        const int token_column = m_column;
        std::string lexeme;
        lexeme.push_back(advance());
        while (!is_at_end()) {
            const char ch = peek();
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                break;
            }
            lexeme.push_back(advance());
        }

        static const std::unordered_map<std::string, TokenType> keywords = {
            {"const", TokenType::kw_const},
            {"mut", TokenType::kw_mut},
            {"fn", TokenType::kw_fn},
            {"return", TokenType::kw_return},
            {"if", TokenType::kw_if},
            {"elif", TokenType::kw_elif},
            {"else", TokenType::kw_else},
            {"while", TokenType::kw_while},
            {"and", TokenType::kw_and},
            {"or", TokenType::kw_or},
            {"not", TokenType::kw_not},
            {"Int", TokenType::kw_type_int},
            {"Bool", TokenType::kw_type_bool},
        };

        static const std::unordered_map<std::string, std::string> unsupported_keywords = {
            {"let", "Rivel v1 does not support legacy keyword `let`"},
            {"exit", "Rivel v1 does not support legacy keyword `exit`"},
            {"import", "Rivel v1 does not support `import`"},
            {"from", "Rivel v1 does not support `from` imports"},
            {"for", "Rivel v1 does not support `for ... in ...`"},
        };

        if (lexeme == "true" || lexeme == "false") {
            add_token(TokenType::bool_literal, lexeme, token_line, token_column);
            return;
        }
        if (const auto unsupported = unsupported_keywords.find(lexeme); unsupported != unsupported_keywords.end()) {
            throw error(token_line, token_column, unsupported->second);
        }
        if (const auto keyword = keywords.find(lexeme); keyword != keywords.end()) {
            add_token(keyword->second, lexeme, token_line, token_column);
            return;
        }
        add_token(TokenType::identifier, lexeme, token_line, token_column);
    }

    void tokenize_int_literal() {
        const int token_line = m_line;
        const int token_column = m_column;
        std::string lexeme;
        lexeme.push_back(advance());
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            lexeme.push_back(advance());
        }
        add_token(TokenType::int_literal, std::move(lexeme), token_line, token_column);
    }

    [[nodiscard]] static CompileError error(const int line, const int column, const std::string& message) {
        return make_error("Lexer", line, column, message);
    }

    std::string m_source;
    size_t m_index = 0;
    int m_line = 1;
    int m_column = 1;
    int m_paren_depth = 0;
    std::vector<Token> m_tokens;
};
