/*
 * AiScript - Parser interface
 */

#ifndef AISCRIPT_PARSER_H
#define AISCRIPT_PARSER_H

#include "scanner.h"
#include "node.h"

/* Parser state */
typedef struct as_parser {
    as_scanner_t* scanner;
    char* error_message;
} as_parser_t;

/* Create and destroy */
as_parser_t* as_parser_new(const char* source);
void as_parser_free(as_parser_t* parser);

/* Parse operations */
as_node_t** as_parser_parse(as_parser_t* parser, size_t* count_out);

/* Helper functions for parsing different constructs */
as_node_t* as_parse_statement(as_parser_t* parser);
as_node_t* as_parse_expression(as_parser_t* parser);
as_node_t* as_parse_primary(as_parser_t* parser);

#endif /* AISCRIPT_PARSER_H */


