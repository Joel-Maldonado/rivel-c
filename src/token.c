#include "token.h"

void token_list_init(TokenList *list, Arena *arena) {
    vec_init(&list->storage, sizeof(Token), arena);
}

size_t token_list_len(const TokenList *list) {
    return list->storage.len;
}

Token *token_list_push(TokenList *list, CompileError *error) {
    return (Token *)vec_push(&list->storage, error);
}

Token *token_list_get(TokenList *list, size_t index) {
    return (Token *)vec_get(&list->storage, index);
}

const Token *token_list_get_const(const TokenList *list, size_t index) {
    return (const Token *)vec_get(&list->storage, index);
}

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
        case TOKEN_DOUBLE_LITERAL:
            return "double literal";
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
        case TOKEN_KW_TYPE_DOUBLE:
            return "`Double`";
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
