/*
 * AiScript - Token definitions
 */

#ifndef AISCRIPT_TOKEN_H
#define AISCRIPT_TOKEN_H

#include "types.h"

/* Token kinds (matching TypeScript TokenKind enum) */
typedef enum {
    /* Special */
    TOK_EOF,
    TOK_NEWLINE,
    TOK_IDENTIFIER,
    
    /* Literals */
    TOK_NUMBER,
    TOK_STRING,
    TOK_TEMPLATE,
    TOK_TEMPLATE_STRING_ELEMENT,
    TOK_TEMPLATE_EXPR_ELEMENT,
    
    /* Keywords */
    TOK_NULL,
    TOK_TRUE,
    TOK_FALSE,
    TOK_EACH,
    TOK_FOR,
    TOK_LOOP,
    TOK_DO,
    TOK_WHILE,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_MATCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_IF,
    TOK_ELIF,
    TOK_ELSE,
    TOK_RETURN,
    TOK_EVAL,
    TOK_VAR,
    TOK_LET,
    TOK_EXISTS,
    TOK_FROM,
    TOK_IMPORT,
    TOK_AS,
    
    /* Operators and punctuation */
    TOK_NOT,           /* ! */
    TOK_NOT_EQ,        /* != */
    TOK_SHARP,         /* # */
    TOK_SHARP_BRACKET, /* #[ */
    TOK_SHARP3,        /* ### */
    TOK_PERCENT,       /* % */
    TOK_AND,           /* && */
    TOK_LPAREN,        /* ( */
    TOK_RPAREN,        /* ) */
    TOK_ASTERISK,      /* * */
    TOK_PLUS,          /* + */
    TOK_PLUS_EQ,       /* += */
    TOK_COMMA,         /* , */
    TOK_MINUS,         /* - */
    TOK_MINUS_EQ,      /* -= */
    TOK_DOT,           /* . */
    TOK_SLASH,         /* / */
    TOK_COLON,         /* : */
    TOK_COLON2,        /* :: */
    TOK_SEMICOLON,     /* ; */
    TOK_LT,            /* < */
    TOK_LT_EQ,         /* <= */
    TOK_OUT,           /* <: */
    TOK_EQ,            /* = */
    TOK_EQ2,           /* == */
    TOK_ARROW,         /* => */
    TOK_GT,            /* > */
    TOK_GT_EQ,         /* >= */
    TOK_QUESTION,      /* ? */
    TOK_AT,            /* @ */
    TOK_LBRACKET,      /* [ */
    TOK_BACKSLASH,     /* \ */
    TOK_RBRACKET,      /* ] */
    TOK_HAT,           /* ^ */
    TOK_LBRACE,        /* { */
    TOK_OR,            /* | */
    TOK_OR2,           /* || */
    TOK_RBRACE         /* } */
} as_token_kind_t;

/* Forward declaration for template children */
typedef struct as_token as_token_t;

/* Token structure */
struct as_token {
    as_token_kind_t kind;
    as_pos_t pos;
    bool has_left_spacing;
    char* value;  /* For literals and identifiers */
    as_token_t** children;  /* For template tokens */
    size_t children_count;
};

/* Token operations */
as_token_t* as_token_new(as_token_kind_t kind, as_pos_t pos, const char* value);
void as_token_free(as_token_t* token);
const char* as_token_kind_to_string(as_token_kind_t kind);
bool as_token_is_keyword(as_token_kind_t kind);

#endif /* AISCRIPT_TOKEN_H */


