#include "token.h"

const char *token_name(TokenType type) {
    switch (type) {
        case TOKEN_EOF:
            return "end of file";
        case TOKEN_END_STMT:
            return "statement separator";
        case TOKEN_IDENTIFIER:
            return "identifier";
        case TOKEN_INT_LITERAL:
            return "integer literal";
        case TOKEN_BOOL_LITERAL:
            return "boolean literal";
        case TOKEN_KW_CONST:
            return "`const`";
        case TOKEN_KW_MUT:
            return "`mut`";
        case TOKEN_KW_FN:
            return "`fn`";
        case TOKEN_KW_RETURN:
            return "`return`";
        case TOKEN_KW_IF:
            return "`if`";
        case TOKEN_KW_ELIF:
            return "`elif`";
        case TOKEN_KW_ELSE:
            return "`else`";
        case TOKEN_KW_WHILE:
            return "`while`";
        case TOKEN_KW_AND:
            return "`and`";
        case TOKEN_KW_OR:
            return "`or`";
        case TOKEN_KW_NOT:
            return "`not`";
        case TOKEN_KW_TYPE_INT:
            return "`Int`";
        case TOKEN_KW_TYPE_BOOL:
            return "`Bool`";
        case TOKEN_OPEN_PAREN:
            return "`(`";
        case TOKEN_CLOSE_PAREN:
            return "`)`";
        case TOKEN_OPEN_BRACE:
            return "`{`";
        case TOKEN_CLOSE_BRACE:
            return "`}`";
        case TOKEN_COMMA:
            return "`,`";
        case TOKEN_COLON:
            return "`:`";
        case TOKEN_ARROW:
            return "`->`";
        case TOKEN_ASSIGN:
            return "`=`";
        case TOKEN_PLUS:
            return "`+`";
        case TOKEN_MINUS:
            return "`-`";
        case TOKEN_STAR:
            return "`*`";
        case TOKEN_SLASH:
            return "`/`";
        case TOKEN_PERCENT:
            return "`%`";
        case TOKEN_EQ_EQ:
            return "`==`";
        case TOKEN_BANG_EQ:
            return "`!=`";
        case TOKEN_LESS:
            return "`<`";
        case TOKEN_LESS_EQ:
            return "`<=`";
        case TOKEN_GREATER:
            return "`>`";
        case TOKEN_GREATER_EQ:
            return "`>=`";
    }

    return "token";
}
