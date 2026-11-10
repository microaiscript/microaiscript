/*
 * AiScript - Scanner (Lexer) interface
 */

#ifndef AISCRIPT_SCANNER_H
#define AISCRIPT_SCANNER_H

#include "token.h"

/* Scanner state */
typedef struct {
    const char* source;
    size_t pos;
    size_t line;
    size_t column;
    as_token_t* current_token;
    as_token_t* lookahead_token;
    /* Pre-tokenized stream (template expressions); does not own tokens */
    as_token_t** token_list;
    size_t token_list_count;
    size_t token_list_pos;
} as_scanner_t;

/* Create and destroy */
as_scanner_t* as_scanner_new(const char* source);
as_scanner_t* as_scanner_from_token_list(as_token_t** tokens, size_t count);
void as_scanner_free(as_scanner_t* scanner);

/* Token stream operations */
as_token_t* as_scanner_peek(as_scanner_t* scanner);
as_token_t* as_scanner_lookahead(as_scanner_t* scanner, int offset);
void as_scanner_next(as_scanner_t* scanner);
bool as_scanner_is(as_scanner_t* scanner, as_token_kind_t kind);
bool as_scanner_expect(as_scanner_t* scanner, as_token_kind_t kind, char** error);
as_pos_t as_scanner_pos(as_scanner_t* scanner);

#endif /* AISCRIPT_SCANNER_H */


