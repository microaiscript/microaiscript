/*
 * AiScript - Token implementation
 */

#include "token.h"
#include "memory.h"
#include <string.h>

as_token_t* as_token_new(as_token_kind_t kind, as_pos_t pos, const char* value) {
    as_token_t* token = as_malloc(sizeof(as_token_t));
    token->kind = kind;
    token->pos = pos;
    token->has_left_spacing = false;
    token->value = value ? as_strdup(value) : NULL;
    token->children = NULL;
    token->children_count = 0;
    return token;
}

void as_token_free(as_token_t* token) {
    if (!token) return;
    if (token->value) as_free(token->value);
    /* Free children recursively */
    for (size_t i = 0; i < token->children_count; i++) {
        as_token_free(token->children[i]);
    }
    if (token->children) as_free(token->children);
    as_free(token);
}

const char* as_token_kind_to_string(as_token_kind_t kind) {
    switch (kind) {
        case TOK_EOF: return "EOF";
        case TOK_NEWLINE: return "NEWLINE";
        case TOK_IDENTIFIER: return "IDENTIFIER";
        case TOK_NUMBER: return "NUMBER";
        case TOK_STRING: return "STRING";
        case TOK_TEMPLATE: return "TEMPLATE";
        case TOK_TEMPLATE_STRING_ELEMENT: return "TEMPLATE_STRING_ELEMENT";
        case TOK_TEMPLATE_EXPR_ELEMENT: return "TEMPLATE_EXPR_ELEMENT";
        case TOK_NULL: return "null";
        case TOK_TRUE: return "true";
        case TOK_FALSE: return "false";
        case TOK_EACH: return "each";
        case TOK_FOR: return "for";
        case TOK_LOOP: return "loop";
        case TOK_DO: return "do";
        case TOK_WHILE: return "while";
        case TOK_BREAK: return "break";
        case TOK_CONTINUE: return "continue";
        case TOK_MATCH: return "match";
        case TOK_CASE: return "case";
        case TOK_DEFAULT: return "default";
        case TOK_IF: return "if";
        case TOK_ELIF: return "elif";
        case TOK_ELSE: return "else";
        case TOK_RETURN: return "return";
        case TOK_EVAL: return "eval";
        case TOK_VAR: return "var";
        case TOK_LET: return "let";
        case TOK_EXISTS: return "exists";
        case TOK_FROM: return "from";
        case TOK_IMPORT: return "import";
        case TOK_AS: return "as";
        case TOK_NOT: return "!";
        case TOK_NOT_EQ: return "!=";
        case TOK_SHARP: return "#";
        case TOK_PERCENT: return "%";
        case TOK_AND: return "&&";
        case TOK_LPAREN: return "(";
        case TOK_RPAREN: return ")";
        case TOK_ASTERISK: return "*";
        case TOK_PLUS: return "+";
        case TOK_PLUS_EQ: return "+=";
        case TOK_COMMA: return ",";
        case TOK_MINUS: return "-";
        case TOK_MINUS_EQ: return "-=";
        case TOK_DOT: return ".";
        case TOK_SLASH: return "/";
        case TOK_COLON: return ":";
        case TOK_COLON2: return "::";
        case TOK_SEMICOLON: return ";";
        case TOK_LT: return "<";
        case TOK_LT_EQ: return "<=";
        case TOK_OUT: return "<:";
        case TOK_EQ: return "=";
        case TOK_EQ2: return "==";
        case TOK_ARROW: return "=>";
        case TOK_GT: return ">";
        case TOK_GT_EQ: return ">=";
        case TOK_QUESTION: return "?";
        case TOK_AT: return "@";
        case TOK_LBRACKET: return "[";
        case TOK_BACKSLASH: return "\\";
        case TOK_RBRACKET: return "]";
        case TOK_HAT: return "^";
        case TOK_LBRACE: return "{";
        case TOK_OR: return "|";
        case TOK_OR2: return "||";
        case TOK_RBRACE: return "}";
        default: return "UNKNOWN";
    }
}

bool as_token_is_keyword(as_token_kind_t kind) {
    return kind >= TOK_NULL && kind <= TOK_EXISTS;
}


