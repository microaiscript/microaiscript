/*
 * AiScript - Parser implementation (simplified)
 */

#include "parser.h"
#include "memory.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* Forward declarations */
static as_node_t* parse_statement(as_parser_t* parser);
static as_node_t* parse_expression(as_parser_t* parser);
static void apply_label_to_node(as_node_t* node, char* label);
static as_node_t* parse_block_or_statement(as_parser_t* parser);
static as_node_t* parse_assignment_expr(as_parser_t* parser);
static as_node_t* parse_or_expr(as_parser_t* parser);
static as_node_t* parse_and_expr(as_parser_t* parser);
static as_node_t* parse_equality_expr(as_parser_t* parser);
static as_node_t* parse_relational_expr(as_parser_t* parser);
static as_node_t* parse_additive_expr(as_parser_t* parser);
static as_node_t* parse_multiplicative_expr(as_parser_t* parser);
static as_node_t* parse_power_expr(as_parser_t* parser);
static as_node_t* parse_unary_expr(as_parser_t* parser);
static as_node_t* parse_postfix_expr(as_parser_t* parser);
static as_node_t* parse_primary_expr(as_parser_t* parser);
static as_node_t* parse_fn_expr(as_parser_t* parser);
static as_fn_param_t* parse_params(as_parser_t* parser, size_t* count_out);
static as_node_t** parse_block(as_parser_t* parser, size_t* count_out);
static char* parse_type_annotation(as_parser_t* parser);
static as_node_attr_t* parse_attr(as_parser_t* parser);
static as_node_namespace_t* parse_namespace(as_parser_t* parser);
static as_node_import_t* parse_import(as_parser_t* parser);
static as_node_t* parse_array_pattern(as_parser_t* parser);
static as_node_t* parse_obj_pattern(as_parser_t* parser);
static void skip_type_params(as_parser_t* parser);

/* Record a syntax error. Only the first error is kept (later ones are usually
 * fallout from it). The message is reported by as_eval()/as_parse() through
 * parser->error_message. */
static void parser_error(as_parser_t* parser, const char* format, ...) {
    char buffer[512];
    char full[640];
    va_list args;
    as_token_t* tok;
    as_pos_t pos = {0, 0};

    if (parser->error_message) return;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    tok = as_scanner_peek(parser->scanner);
    if (tok) pos = tok->pos;
    snprintf(full, sizeof(full), "Syntax error at line %d, column %d: %s",
             pos.line, pos.column, buffer);
    parser->error_message = as_strdup(full);
}

/* Helper to skip newlines */
static void skip_newlines(as_parser_t* parser) {
    while (as_scanner_is(parser->scanner, TOK_NEWLINE)) {
        as_scanner_next(parser->scanner);
    }
}

/* Skip a `<T, U>` type-parameter list (types are not checked at runtime). */
static void skip_type_params(as_parser_t* parser) {
    if (!as_scanner_is(parser->scanner, TOK_LT)) {
        return;
    }
    as_scanner_next(parser->scanner);
    int depth = 1;
    while (depth > 0 && !as_scanner_is(parser->scanner, TOK_EOF)) {
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_LT)) {
            depth++;
            as_scanner_next(parser->scanner);
        } else if (as_scanner_is(parser->scanner, TOK_GT)) {
            depth--;
            as_scanner_next(parser->scanner);
        } else {
            as_scanner_next(parser->scanner);
        }
    }
}

/* Parse an object key: an identifier, a string literal or a keyword
 * (e.g. { loop: 1 }). *is_identifier tells whether the key was a plain
 * identifier, the only form allowed in the `{ a }` shorthand. */
static char* parse_obj_key(as_parser_t* parser, bool* is_identifier)
{
    as_token_t* tok = as_scanner_peek(parser->scanner);
    if (is_identifier) {
        *is_identifier = false;
    }
    if (!tok) {
        return NULL;
    }
    if (tok->kind == TOK_IDENTIFIER || tok->kind == TOK_STRING) {
        char* key;
        if (!tok->value) {
            return NULL;
        }
        key = as_strdup(tok->value);
        if (is_identifier) {
            *is_identifier = (tok->kind == TOK_IDENTIFIER);
        }
        as_scanner_next(parser->scanner);
        return key;
    }
    if (tok->kind >= TOK_NULL && tok->kind <= TOK_AS) {
        char* key = as_strdup(as_token_kind_to_string(tok->kind));
        as_scanner_next(parser->scanner);
        return key;
    }
    return NULL;
}

/* True if an object literal/pattern under construction already has `key`. */
static bool obj_has_key(const as_node_obj_t* node, const char* key)
{
    for (size_t i = 0; i < node->entry_count; i++) {
        if (strcmp(node->entries[i].key, key) == 0) {
            return true;
        }
    }
    return false;
}

/* Parse type annotation (supports simple types, union types |, and generics <>) */
static char* parse_type_annotation(as_parser_t* parser) {
    /* Build type string by concatenating tokens */
    char* type_str = NULL;
    size_t type_len = 0;
    size_t type_cap = 0;
    
    /* Helper macro to append to type string */
    #define APPEND_TYPE(str) do { \
        size_t len = strlen(str); \
        while (type_len + len + 1 > type_cap) { \
            type_cap = type_cap == 0 ? 64 : type_cap * 2; \
            type_str = (char*)as_realloc(type_str, type_cap); \
        } \
        strcpy(type_str + type_len, str); \
        type_len += len; \
    } while(0)
    
    /* Parse function type: @ [TypeParams] ( types ) => type */
    if (as_scanner_is(parser->scanner, TOK_AT)) {
        APPEND_TYPE("@");
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        skip_type_params(parser);
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_LPAREN)) {
            APPEND_TYPE("(");
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            int first_param = 1;
            while (!as_scanner_is(parser->scanner, TOK_RPAREN) &&
                   !as_scanner_is(parser->scanner, TOK_EOF)) {
                if (!first_param) {
                    APPEND_TYPE(", ");
                }
                first_param = 0;
                char* param_type = parse_type_annotation(parser);
                if (param_type) {
                    APPEND_TYPE(param_type);
                    as_free(param_type);
                }
                skip_newlines(parser);
                if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                    as_scanner_next(parser->scanner);
                    skip_newlines(parser);
                } else {
                    break;
                }
            }
            if (as_scanner_is(parser->scanner, TOK_RPAREN)) {
                APPEND_TYPE(")");
                as_scanner_next(parser->scanner);
            }
        }
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_ARROW)) {
            APPEND_TYPE(" => ");
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            char* ret_type = parse_type_annotation(parser);
            if (ret_type) {
                APPEND_TYPE(ret_type);
                as_free(ret_type);
            }
        }
    }
    /* Parse primary type: identifier or null */
    else if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
        APPEND_TYPE(as_scanner_peek(parser->scanner)->value);
        as_scanner_next(parser->scanner);
    } else if (as_scanner_is(parser->scanner, TOK_NULL)) {
        APPEND_TYPE("null");
        as_scanner_next(parser->scanner);
    } else {
        /* Not a valid type */
        if (type_str) as_free(type_str);
        return NULL;
    }
    
    /* Parse optional generic type parameter: <inner> */
    if (as_scanner_is(parser->scanner, TOK_LT)) {
        APPEND_TYPE("<");
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        char* inner_type = parse_type_annotation(parser);
        if (inner_type) {
            APPEND_TYPE(inner_type);
            as_free(inner_type);
        }
        
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_GT)) {
            APPEND_TYPE(">");
            as_scanner_next(parser->scanner);
        }
    }
    
    /* Parse union types: | type2 | type3 ... */
    while (as_scanner_is(parser->scanner, TOK_OR)) {
        APPEND_TYPE(" | ");
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        char* union_type = parse_type_annotation(parser);
        if (union_type) {
            APPEND_TYPE(union_type);
            as_free(union_type);
        }
    }
    
    #undef APPEND_TYPE
    
    return type_str;
}

/* Helper to get location from current token */
static as_loc_t get_loc(as_parser_t* parser) {
    as_pos_t pos = as_scanner_pos(parser->scanner);
    as_loc_t loc = { pos, pos };
    return loc;
}

/* Parse attribute: #[Name value] */
static as_node_attr_t* parse_attr(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    
    /* Expect #[ */
    if (!as_scanner_is(parser->scanner, TOK_SHARP_BRACKET)) {
        return NULL;
    }
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    /* Parse attribute name */
    if (!as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
        parser_error(parser, "Expected attribute name");
        return NULL;
    }
    
    as_node_attr_t* attr = (as_node_attr_t*)as_node_new(NODE_ATTR, loc);
    attr->name = as_strdup(as_scanner_peek(parser->scanner)->value);
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    /* Parse value (expression) */
    if (!as_scanner_is(parser->scanner, TOK_RBRACKET)) {
        attr->value = parse_expression(parser);
    } else {
        /* No value means true */
        as_node_bool_t* bool_node = (as_node_bool_t*)as_node_new(NODE_BOOL, loc);
        bool_node->value = true;
        attr->value = (as_node_t*)bool_node;
    }
    
    skip_newlines(parser);
    
    /* Expect ] */
    if (!as_scanner_is(parser->scanner, TOK_RBRACKET)) {
        parser_error(parser, "Expected ']' after attribute");
        as_node_free((as_node_t*)attr);
        return NULL;
    }
    as_scanner_next(parser->scanner);
    
    return attr;
}

/* Parse namespace: :: Name { members } */
static as_node_namespace_t* parse_namespace(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    
    /* Expect :: */
    if (!as_scanner_is(parser->scanner, TOK_COLON2)) {
        return NULL;
    }
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    /* Parse namespace name */
    if (!as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
        parser_error(parser, "Expected namespace name");
        return NULL;
    }
    
    as_node_namespace_t* ns = (as_node_namespace_t*)as_node_new(NODE_NAMESPACE, loc);
    ns->name = as_strdup(as_scanner_peek(parser->scanner)->value);
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    /* Parse opening brace */
    if (!as_scanner_is(parser->scanner, TOK_LBRACE)) {
        parser_error(parser, "Expected '{' after namespace name");
        as_node_free((as_node_t*)ns);
        return NULL;
    }
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    /* Parse members (variables, functions, nested namespaces) */
    as_node_t** members = NULL;
    size_t member_count = 0;
    size_t member_capacity = 0;
    
    while (!as_scanner_is(parser->scanner, TOK_RBRACE) && !as_scanner_is(parser->scanner, TOK_EOF)) {
        as_node_t* member = NULL;
        
        /* Check for nested namespace */
        if (as_scanner_is(parser->scanner, TOK_COLON2)) {
            member = (as_node_t*)parse_namespace(parser);
        }
        /* Check for attributes */
        else if (as_scanner_is(parser->scanner, TOK_SHARP_BRACKET)) {
            member = parse_statement(parser);  /* parse_statement handles attributes */
        }
        /* Check for variable declaration (let only, no var) */
        else if (as_scanner_is(parser->scanner, TOK_LET)) {
            member = parse_statement(parser);
        }
        /* Check for function definition */
        else if (as_scanner_is(parser->scanner, TOK_AT)) {
            member = parse_statement(parser);
        }
        /* Check for var (not allowed in namespaces) */
        else if (as_scanner_is(parser->scanner, TOK_VAR)) {
            parser_error(parser, "'var' not allowed in namespace declarations");
            break;
        }
        else {
            /* Unknown token, skip */
            skip_newlines(parser);
            if (!as_scanner_is(parser->scanner, TOK_RBRACE)) {
                as_scanner_next(parser->scanner);
            }
            continue;
        }
        
        if (member) {
            /* Grow members array if needed */
            if (member_count >= member_capacity) {
                member_capacity = member_capacity == 0 ? 4 : member_capacity * 2;
                members = (as_node_t**)as_realloc(members, sizeof(as_node_t*) * member_capacity);
            }
            members[member_count++] = member;
        }
        
        /* Skip terminators */
        skip_newlines(parser);
        while (as_scanner_is(parser->scanner, TOK_SEMICOLON)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
    }
    
    /* Expect closing brace */
    if (!as_scanner_is(parser->scanner, TOK_RBRACE)) {
        parser_error(parser, "Expected '}' after namespace members");
        for (size_t i = 0; i < member_count; i++) {
            as_node_free(members[i]);
        }
        if (members) as_free(members);
        as_node_free((as_node_t*)ns);
        return NULL;
    }
    as_scanner_next(parser->scanner);
    
    ns->members = members;
    ns->member_count = member_count;
    
    return ns;
}

/* Parse import statement: 
 * - import "path" [as Alias]
 * - from "path" import func1, func2, func3
 */
static as_node_import_t* parse_import(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    bool is_from = as_scanner_is(parser->scanner, TOK_FROM);
    
    /* Expect import or from keyword */
    if (!is_from && !as_scanner_is(parser->scanner, TOK_IMPORT)) {
        return NULL;
    }
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    /* Parse import path (can be a string or identifier) */
    as_node_import_t* import = (as_node_import_t*)as_node_new(NODE_IMPORT, loc);
    import->alias = NULL;
    import->imports = NULL;
    import->import_count = 0;
    
    if (as_scanner_is(parser->scanner, TOK_STRING)) {
        /* String path: import "ui_gtk.so" or import "path/to/module.ais" */
        import->path = as_strdup(as_scanner_peek(parser->scanner)->value);
        as_scanner_next(parser->scanner);
    } else if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
        /* Identifier: import ui_gtk (will search Core:modpath) */
        import->path = as_strdup(as_scanner_peek(parser->scanner)->value);
        as_scanner_next(parser->scanner);
    } else {
        parser_error(parser, "Expected string or identifier after '%s'", is_from ? "from" : "import");
        as_node_free((as_node_t*)import);
        return NULL;
    }
    
    skip_newlines(parser);
    
    if (is_from) {
        /* Expect 'import' keyword after path in 'from' syntax */
        if (!as_scanner_is(parser->scanner, TOK_IMPORT)) {
            parser_error(parser, "Expected 'import' after module path in 'from' statement");
            as_node_free((as_node_t*)import);
            return NULL;
        }
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse comma-separated list of identifiers */
        size_t capacity = 4;
        import->imports = (char**)as_malloc(sizeof(char*) * capacity);
        
        while (true) {
            if (!as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                parser_error(parser, "Expected identifier in import list");
                as_node_free((as_node_t*)import);
                return NULL;
            }
            
            /* Expand array if needed */
            if (import->import_count >= capacity) {
                capacity *= 2;
                import->imports = (char**)as_realloc(import->imports, sizeof(char*) * capacity);
            }
            
            import->imports[import->import_count++] = as_strdup(as_scanner_peek(parser->scanner)->value);
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            /* Check for comma (more imports) or end */
            if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
            } else {
                break;
            }
        }
    } else {
        /* Check for optional 'as Alias' in regular import */
        if (as_scanner_is(parser->scanner, TOK_AS)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            /* Expect identifier for alias */
            if (!as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                parser_error(parser, "Expected identifier after 'as'");
                as_node_free((as_node_t*)import);
                return NULL;
            }
            
            import->alias = as_strdup(as_scanner_peek(parser->scanner)->value);
            as_scanner_next(parser->scanner);
        }
    }
    
    return import;
}

/* Parse function parameters */
static as_fn_param_t* parse_params(as_parser_t* parser, size_t* count_out) {
    /* Expect opening paren */
    if (!as_scanner_is(parser->scanner, TOK_LPAREN)) {
        return NULL;
    }
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    as_fn_param_t* params = NULL;
    size_t count = 0;
    size_t capacity = 0;
    
    while (!as_scanner_is(parser->scanner, TOK_RPAREN) && 
           !as_scanner_is(parser->scanner, TOK_EOF)) {
        
        AS_ARRAY_GROW(params, count, capacity, as_fn_param_t);
        
        as_loc_t param_loc = get_loc(parser);
        as_node_t* dest_node = NULL;

        /* Parameter dest: identifier or destructuring pattern */
        if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
            char* param_name = as_strdup(as_scanner_peek(parser->scanner)->value);
            as_scanner_next(parser->scanner);
            as_node_identifier_t* id_node = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, param_loc);
            id_node->name = param_name;
            id_node->namespace = NULL;
            dest_node = (as_node_t*)id_node;
        } else if (as_scanner_is(parser->scanner, TOK_LBRACKET)) {
            dest_node = parse_array_pattern(parser);
        } else if (as_scanner_is(parser->scanner, TOK_LBRACE)) {
            dest_node = parse_obj_pattern(parser);
        } else {
            break;
        }

        skip_newlines(parser);
        
        params[count].dest = dest_node;
        params[count].optional = false;
        params[count].default_value = NULL;
        params[count].type_annotation = NULL;
        
        /* Parse optional type annotation: : type */
        if (as_scanner_is(parser->scanner, TOK_COLON)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            params[count].type_annotation = parse_type_annotation(parser);
            skip_newlines(parser);
        }
        
        /* Check for optional marker (?) or default value (=) */
        if (as_scanner_is(parser->scanner, TOK_QUESTION)) {
            /* Optional parameter: param? */
            params[count].optional = true;
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        } else if (as_scanner_is(parser->scanner, TOK_EQ)) {
            /* Default parameter: param = expr */
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            params[count].default_value = parse_expression(parser);
            skip_newlines(parser);
        }
        
        count++;
        
        /* Handle comma separator */
        if (as_scanner_is(parser->scanner, TOK_COMMA)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
    }
    
    /* Expect closing paren */
    if (as_scanner_is(parser->scanner, TOK_RPAREN)) {
        as_scanner_next(parser->scanner);
    }
    
    *count_out = count;
    return params;
}

/* Parse block of statements */
static as_node_t** parse_block(as_parser_t* parser, size_t* count_out) {
    /* Expect opening brace */
    if (!as_scanner_is(parser->scanner, TOK_LBRACE)) {
        return NULL;
    }
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    as_node_t** statements = NULL;
    size_t count = 0;
    size_t capacity = 0;
    
    while (!as_scanner_is(parser->scanner, TOK_RBRACE) && 
           !as_scanner_is(parser->scanner, TOK_EOF)) {
        
        AS_ARRAY_GROW(statements, count, capacity, as_node_t*);
        statements[count++] = parse_statement(parser);
        skip_newlines(parser);
        
        /* Skip optional semicolons */
        while (as_scanner_is(parser->scanner, TOK_SEMICOLON)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
    }
    
    /* Expect closing brace */
    if (as_scanner_is(parser->scanner, TOK_RBRACE)) {
        as_scanner_next(parser->scanner);
    }
    
    *count_out = count;
    return statements;
}

/* Parse function expression: @(params) { body } */
static as_node_t* parse_fn_expr(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    
    /* Consume @ */
    if (!as_scanner_is(parser->scanner, TOK_AT)) {
        return NULL;
    }
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    skip_type_params(parser);
    skip_newlines(parser);
    
    /* Parse parameters */
    size_t param_count = 0;
    as_fn_param_t* params = parse_params(parser, &param_count);
    skip_newlines(parser);
    
    /* Parse optional return type annotation: : type */
    char* return_type = NULL;
    if (as_scanner_is(parser->scanner, TOK_COLON)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        return_type = parse_type_annotation(parser);
        skip_newlines(parser);
    }
    
    /* Parse body */
    size_t statement_count = 0;
    as_node_t** statements = parse_block(parser, &statement_count);
    
    /* Create function node */
    as_node_fn_t* node = (as_node_fn_t*)as_node_new(NODE_FN, loc);
    node->params = params;
    node->param_count = param_count;
    node->statements = statements;
    node->statement_count = statement_count;
    node->return_type = return_type;
    
    return (as_node_t*)node;
}

/* Parse primary expression (literals, identifiers, parenthesized expressions) */
static as_node_t* parse_primary_expr(as_parser_t* parser) {
    as_token_t* token = as_scanner_peek(parser->scanner);
    as_loc_t loc = get_loc(parser);
    
    /* Function expression: @(params) { body } */
    if (token->kind == TOK_AT) {
        return parse_fn_expr(parser);
    }

    /* eval block expression: eval { stmts } */
    if (token->kind == TOK_EVAL) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        size_t eval_count = 0;
        as_node_t** eval_stmts = parse_block(parser, &eval_count);
        as_node_block_t* blk = (as_node_block_t*)as_node_new(NODE_BLOCK, loc);
        blk->statements = eval_stmts;
        blk->statement_count = eval_count;
        blk->label = NULL;
        return (as_node_t*)blk;
    }

    /* If expression */
    if (token->kind == TOK_IF) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);

        as_node_if_t* node = (as_node_if_t*)as_node_new(NODE_IF, loc);
        node->cond = parse_expression(parser);
        skip_newlines(parser);
        node->then_node = parse_block_or_statement(parser);
        skip_newlines(parser);
        node->elseif_count = 0;
        node->elseif_conds = NULL;
        node->elseif_then = NULL;
        node->else_node = NULL;

        size_t elif_capacity = 0;
        while (as_scanner_is(parser->scanner, TOK_ELIF)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            if (node->elseif_count >= elif_capacity) {
                elif_capacity = elif_capacity == 0 ? 4 : elif_capacity * 2;
                node->elseif_conds = as_realloc(node->elseif_conds,
                    elif_capacity * sizeof(as_node_t*));
                node->elseif_then = as_realloc(node->elseif_then,
                    elif_capacity * sizeof(as_node_t*));
            }
            node->elseif_conds[node->elseif_count] = parse_expression(parser);
            skip_newlines(parser);
            node->elseif_then[node->elseif_count] = parse_block_or_statement(parser);
            node->elseif_count++;
            skip_newlines(parser);
        }

        if (as_scanner_is(parser->scanner, TOK_ELSE)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            node->else_node = parse_block_or_statement(parser);
        }

        return (as_node_t*)node;
    }
    
    /* Number literal */
    if (token->kind == TOK_NUMBER) {
        as_node_num_t* node = (as_node_num_t*)as_node_new(NODE_NUM, loc);
        if (token->value && token->value[0] == '0' &&
            (token->value[1] == 'x' || token->value[1] == 'X')) {
            node->value = (double)strtoul(token->value, NULL, 16);
        } else {
            node->value = atof(token->value);
        }
        as_scanner_next(parser->scanner);
        return (as_node_t*)node;
    }
    
    /* String literal */
    if (token->kind == TOK_STRING) {
        as_node_str_t* node = (as_node_str_t*)as_node_new(NODE_STR, loc);
        node->value = as_strdup(token->value);
        as_scanner_next(parser->scanner);
        return (as_node_t*)node;
    }
    
    /* Template string */
    if (token->kind == TOK_TEMPLATE) {
        as_node_tmpl_t* tmpl = (as_node_tmpl_t*)as_node_new(NODE_TMPL, loc);
        tmpl->values = NULL;
        tmpl->value_count = 0;
        
        size_t capacity = 0;
        
        /* Process each child token */
        for (size_t i = 0; i < token->children_count; i++) {
            as_token_t* child = token->children[i];
            
            if (child->kind == TOK_TEMPLATE_STRING_ELEMENT) {
                /* Create string literal node */
                as_node_str_t* str_node = (as_node_str_t*)as_node_new(NODE_STR, loc);
                str_node->value = as_strdup(child->value);
                
                AS_ARRAY_GROW(tmpl->values, tmpl->value_count, capacity, as_node_t*);
                tmpl->values[tmpl->value_count++] = (as_node_t*)str_node;
            }
            else if (child->kind == TOK_TEMPLATE_EXPR_ELEMENT) {
                if (child->children_count > 0) {
                    as_scanner_t* saved_scanner = parser->scanner;
                    as_scanner_t* expr_scanner = as_scanner_from_token_list(
                        child->children, child->children_count);
                    parser->scanner = expr_scanner;
                    as_node_t* expr_node = parse_expression(parser);
                    parser->scanner = saved_scanner;
                    as_scanner_free(expr_scanner);
                    if (expr_node) {
                        AS_ARRAY_GROW(tmpl->values, tmpl->value_count, capacity, as_node_t*);
                        tmpl->values[tmpl->value_count++] = expr_node;
                    }
                }
            }
        }
        
        as_scanner_next(parser->scanner);
        return (as_node_t*)tmpl;
    }
    
    /* Boolean literals */
    if (token->kind == TOK_TRUE || token->kind == TOK_FALSE) {
        as_node_bool_t* node = (as_node_bool_t*)as_node_new(NODE_BOOL, loc);
        node->value = (token->kind == TOK_TRUE);
        as_scanner_next(parser->scanner);
        return (as_node_t*)node;
    }
    
    /* Null literal */
    if (token->kind == TOK_NULL) {
        as_scanner_next(parser->scanner);
        return as_node_new(NODE_NULL, loc);
    }
    
    /* exists operator */
    if (token->kind == TOK_EXISTS) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse the identifier to check */
        as_node_t* identifier = parse_primary_expr(parser);
        if (!identifier || identifier->type != NODE_IDENTIFIER) {
            parser_error(parser, "exists requires an identifier");
            if (identifier) as_node_free(identifier);
            return NULL;
        }
        
        as_node_exists_t* exists_node = (as_node_exists_t*)as_node_new(NODE_EXISTS, loc);
        exists_node->identifier = identifier;
        return (as_node_t*)exists_node;
    }
    
    /* match expression */
    if (token->kind == TOK_MATCH) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse the expression to match against */
        as_node_t* about = parse_expression(parser);
        if (!about) {
            parser_error(parser, "match requires an expression");
            return NULL;
        }
        
        skip_newlines(parser);
        
        /* Expect opening brace */
        if (!as_scanner_is(parser->scanner, TOK_LBRACE)) {
            parser_error(parser, "Expected '{' after match expression");
            as_node_free(about);
            return NULL;
        }
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse cases */
        as_node_match_t* match_node = (as_node_match_t*)as_node_new(NODE_MATCH, loc);
        match_node->label = NULL;
        match_node->about = about;
        match_node->cases = NULL;
        match_node->case_count = 0;
        match_node->default_case = NULL;
        
        size_t capacity = 0;
        
        while (!as_scanner_is(parser->scanner, TOK_RBRACE) && 
               !as_scanner_is(parser->scanner, TOK_EOF)) {
            
            if (as_scanner_is(parser->scanner, TOK_CASE)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                
                /* Parse case condition */
                as_node_t* case_cond = parse_expression(parser);
                if (!case_cond) {
                    parser_error(parser, "Expected expression after 'case'");
                    as_node_free((as_node_t*)match_node);
                    return NULL;
                }
                
                skip_newlines(parser);
                
                /* Expect arrow */
                if (!as_scanner_is(parser->scanner, TOK_ARROW)) {
                    parser_error(parser, "Expected '=>' after case condition");
                    as_node_free(case_cond);
                    as_node_free((as_node_t*)match_node);
                    return NULL;
                }
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                
                /* Parse case result */
                as_node_t* case_result = parse_block_or_statement(parser);
                if (!case_result) {
                    parser_error(parser, "Expected statement/block after '=>'");
                    as_node_free(case_cond);
                    as_node_free((as_node_t*)match_node);
                    return NULL;
                }
                
                /* Add case */
                if (match_node->case_count >= capacity) {
                    capacity = capacity == 0 ? 4 : capacity * 2;
                    match_node->cases = as_realloc(match_node->cases, 
                                                   capacity * sizeof(as_match_case_t));
                }
                match_node->cases[match_node->case_count].q = case_cond;
                match_node->cases[match_node->case_count].a = case_result;
                match_node->case_count++;
                
                skip_newlines(parser);
                if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                    as_scanner_next(parser->scanner);
                    skip_newlines(parser);
                }
                
            } else if (as_scanner_is(parser->scanner, TOK_DEFAULT)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                
                /* Expect arrow */
                if (!as_scanner_is(parser->scanner, TOK_ARROW)) {
                    parser_error(parser, "Expected '=>' after 'default'");
                    as_node_free((as_node_t*)match_node);
                    return NULL;
                }
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                
                /* Parse default result */
                match_node->default_case = parse_block_or_statement(parser);
                if (!match_node->default_case) {
                    parser_error(parser, "Expected statement/block after '=>'");
                    as_node_free((as_node_t*)match_node);
                    return NULL;
                }
                
                skip_newlines(parser);
                if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                    as_scanner_next(parser->scanner);
                    skip_newlines(parser);
                }
                break;  /* default should be last */
                
            } else {
                parser_error(parser, "Expected 'case' or 'default' in match block");
                as_node_free((as_node_t*)match_node);
                return NULL;
            }
        }
        
        /* Expect closing brace */
        if (!as_scanner_is(parser->scanner, TOK_RBRACE)) {
            parser_error(parser, "Expected '}' to close match block");
            as_node_free((as_node_t*)match_node);
            return NULL;
        }
        as_scanner_next(parser->scanner);
        
        return (as_node_t*)match_node;
    }
    
    /* Identifier (possibly namespaced with multiple levels like Ui:C:button) */
    if (token->kind == TOK_IDENTIFIER) {
        char* first_name = as_strdup(token->value);
        as_scanner_next(parser->scanner);
        
        /* Check for namespace syntax (Name:identifier or Name:Sub:identifier) */
        if (as_scanner_is(parser->scanner, TOK_COLON)) {
            /* Build namespace string, potentially with multiple parts */
            size_t namespace_capacity = strlen(first_name) + 256;  /* Room for multi-level namespaces */
            char* namespace_str = (char*)as_malloc(namespace_capacity);
            strcpy(namespace_str, first_name);
            as_free(first_name);
            
            /* Keep consuming colon + identifier pairs */
            while (as_scanner_is(parser->scanner, TOK_COLON)) {
                as_scanner_next(parser->scanner);  /* consume colon */
                
                /* Next must be an identifier */
                if (!as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                    parser_error(parser, "Expected identifier after namespace ':'");
                    as_free(namespace_str);
                    return NULL;
                }
                
                /* Copy the identifier value immediately (scanner tokens are reused!) */
                char* next_part = as_strdup(as_scanner_peek(parser->scanner)->value);
                as_scanner_next(parser->scanner);
                
                /* Check if there's another colon ahead (more namespace parts) */
                if (as_scanner_is(parser->scanner, TOK_COLON)) {
                    /* This is a namespace part, append it with colon */
                    size_t new_len = strlen(namespace_str) + 1 + strlen(next_part) + 1;
                    if (new_len > namespace_capacity) {
                        namespace_capacity = new_len * 2;
                        namespace_str = (char*)as_realloc(namespace_str, namespace_capacity);
                    }
                    strcat(namespace_str, ":");
                    strcat(namespace_str, next_part);
                    as_free(next_part);  /* Free the copied part */
                } else {
                    /* This is the final identifier (the name) */
                    as_node_identifier_t* node = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
                    node->namespace = namespace_str;
                    node->name = next_part;  /* Transfer ownership to node */
                    return (as_node_t*)node;
                }
            }
            
            /* Should never reach here, but handle it gracefully */
            parser_error(parser, "Unexpected end of namespace chain");
            as_free(namespace_str);
            return NULL;
        }
        
        /* Regular identifier */
        as_node_identifier_t* node = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
        node->name = first_name;
        node->namespace = NULL;
        return (as_node_t*)node;
    }
    
    /* Array literal */
    if (token->kind == TOK_LBRACKET) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_arr_t* node = (as_node_arr_t*)as_node_new(NODE_ARR, loc);
        node->elements = NULL;
        node->element_count = 0;
        
        size_t capacity = 0;
        while (!as_scanner_is(parser->scanner, TOK_RBRACKET) && 
               !as_scanner_is(parser->scanner, TOK_EOF)) {
            AS_ARRAY_GROW(node->elements, node->element_count, capacity, as_node_t*);
            node->elements[node->element_count++] = parse_expression(parser);
            
            skip_newlines(parser);
            if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
            }
        }
        
        if (as_scanner_is(parser->scanner, TOK_RBRACKET)) {
            as_scanner_next(parser->scanner);
        }
        
        return (as_node_t*)node;
    }
    
    /* Object literal */
    if (token->kind == TOK_LBRACE) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_obj_t* node = (as_node_obj_t*)as_node_new(NODE_OBJ, loc);
        node->entries = NULL;
        node->entry_count = 0;
        
        size_t capacity = 0;
        while (!as_scanner_is(parser->scanner, TOK_RBRACE) && 
               !as_scanner_is(parser->scanner, TOK_EOF)) {
            bool key_is_identifier = false;
            char* key = parse_obj_key(parser, &key_is_identifier);
            if (!key) {
                parser_error(parser, "Invalid object key");
                break;
            }
            if (obj_has_key(node, key)) {
                parser_error(parser, "Key %s is duplicated.", key);
                as_free(key);
                break;
            }
            
            skip_newlines(parser);
            if (!as_scanner_is(parser->scanner, TOK_COLON)) {
                /* Shorthand: { a } means { a: a }. Only a plain identifier can be
                 * a variable reference; keywords and strings cannot. */
                if (!key_is_identifier) {
                    parser_error(parser, "Expected ':' after object key '%s'", key);
                    as_free(key);
                    break;
                }
                as_node_identifier_t* id = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
                id->name = as_strdup(key);
                id->namespace = NULL;
                AS_ARRAY_GROW(node->entries, node->entry_count, capacity, as_obj_kv_t);
                node->entries[node->entry_count].key = key;
                node->entries[node->entry_count].value = (as_node_t*)id;
                node->entry_count++;
            } else {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);

                as_node_t* value = parse_expression(parser);

                AS_ARRAY_GROW(node->entries, node->entry_count, capacity, as_obj_kv_t);
                node->entries[node->entry_count].key = key;
                node->entries[node->entry_count].value = value;
                node->entry_count++;
            }
            
            skip_newlines(parser);
            if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
            }
        }
        
        if (as_scanner_is(parser->scanner, TOK_RBRACE)) {
            as_scanner_next(parser->scanner);
        } else {
            parser_error(parser, "Expected '}' to close object literal");
        }
        
        return (as_node_t*)node;
    }
    
    /* Parenthesized expression */
    if (token->kind == TOK_LPAREN) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        as_node_t* expr = parse_expression(parser);
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_RPAREN)) {
            as_scanner_next(parser->scanner);
        }
        return expr;
    }
    
    /* Unexpected token - report error and skip it */
    parser_error(parser, "Unexpected token");
    as_scanner_next(parser->scanner);  /* Skip the bad token */
    return NULL;
}

/* Parse postfix expression (function calls, property/index access) */
static as_node_t* parse_postfix_expr(as_parser_t* parser) {
    as_node_t* node = parse_primary_expr(parser);
    if (!node) return NULL;
    
    while (true) {
        as_loc_t loc = get_loc(parser);
        
        /* Function call */
        if (as_scanner_is(parser->scanner, TOK_LPAREN)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            as_node_call_t* call = (as_node_call_t*)as_node_new(NODE_CALL, loc);
            call->target = node;
            call->args = NULL;
            call->arg_count = 0;
            
            size_t capacity = 0;
            while (!as_scanner_is(parser->scanner, TOK_RPAREN) && 
                   !as_scanner_is(parser->scanner, TOK_EOF)) {
                AS_ARRAY_GROW(call->args, call->arg_count, capacity, as_node_t*);
                call->args[call->arg_count++] = parse_expression(parser);
                
                skip_newlines(parser);
                if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                    as_scanner_next(parser->scanner);
                    skip_newlines(parser);
                }
            }
            
            if (as_scanner_is(parser->scanner, TOK_RPAREN)) {
                as_scanner_next(parser->scanner);
            }
            
            node = (as_node_t*)call;
        }
        /* Property access */
        else if (as_scanner_is(parser->scanner, TOK_DOT)) {
            as_scanner_next(parser->scanner);
            if (as_scanner_is(parser->scanner, TOK_IDENTIFIER) ||
                as_scanner_is(parser->scanner, TOK_STRING)) {
                as_node_prop_t* prop = (as_node_prop_t*)as_node_new(NODE_PROP, loc);
                prop->target = node;
                if (as_scanner_is(parser->scanner, TOK_STRING)) {
                    prop->name = as_strdup(as_scanner_peek(parser->scanner)->value);
                } else {
                    prop->name = as_strdup(as_scanner_peek(parser->scanner)->value);
                }
                as_scanner_next(parser->scanner);
                node = (as_node_t*)prop;
            } else if (as_scanner_peek(parser->scanner) &&
                       as_scanner_peek(parser->scanner)->kind >= TOK_NULL &&
                       as_scanner_peek(parser->scanner)->kind <= TOK_AS) {
                as_node_prop_t* prop = (as_node_prop_t*)as_node_new(NODE_PROP, loc);
                prop->target = node;
                prop->name = as_strdup(as_token_kind_to_string(as_scanner_peek(parser->scanner)->kind));
                as_scanner_next(parser->scanner);
                node = (as_node_t*)prop;
            } else {
                break;
            }
        }
        /* Index access */
        else if (as_scanner_is(parser->scanner, TOK_LBRACKET)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            as_node_index_t* index = (as_node_index_t*)as_node_new(NODE_INDEX, loc);
            index->target = node;
            index->index = parse_expression(parser);
            skip_newlines(parser);
            if (as_scanner_is(parser->scanner, TOK_RBRACKET)) {
                as_scanner_next(parser->scanner);
            }
            node = (as_node_t*)index;
        }
        else {
            break;
        }
    }
    
    return node;
}

/* Parse power expression (right-associative) */
static as_node_t* parse_power_expr(as_parser_t* parser) {
    as_node_t* left = parse_unary_expr(parser);
    if (!left) return NULL;
    
    if (as_scanner_is(parser->scanner, TOK_HAT)) {
        as_loc_t loc = get_loc(parser);
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_binary_t* node = (as_node_binary_t*)as_node_new(NODE_POW, loc);
        node->left = left;
        node->right = parse_power_expr(parser);  /* Right-associative recursion */
        return (as_node_t*)node;
    }
    
    return left;
}

/* Parse unary expression */
static as_node_t* parse_unary_expr(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    
    if (as_scanner_is(parser->scanner, TOK_NOT)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        as_node_unary_t* node = (as_node_unary_t*)as_node_new(NODE_NOT, loc);
        node->expr = parse_unary_expr(parser);
        return (as_node_t*)node;
    }
    
    if (as_scanner_is(parser->scanner, TOK_PLUS)) {
        as_scanner_next(parser->scanner);
        as_node_unary_t* node = (as_node_unary_t*)as_node_new(NODE_PLUS, loc);
        node->expr = parse_unary_expr(parser);
        return (as_node_t*)node;
    }
    
    if (as_scanner_is(parser->scanner, TOK_MINUS)) {
        as_scanner_next(parser->scanner);
        as_node_unary_t* node = (as_node_unary_t*)as_node_new(NODE_MINUS, loc);
        node->expr = parse_unary_expr(parser);
        return (as_node_t*)node;
    }
    
    return parse_postfix_expr(parser);
}

/* Parse multiplicative expression */
static as_node_t* parse_multiplicative_expr(as_parser_t* parser) {
    as_node_t* left = parse_power_expr(parser);
    if (!left) return NULL;
    
    while (true) {
        as_loc_t loc = get_loc(parser);
        as_node_type_t op_type;
        
        if (as_scanner_is(parser->scanner, TOK_ASTERISK)) {
            op_type = NODE_MUL;
        } else if (as_scanner_is(parser->scanner, TOK_SLASH)) {
            op_type = NODE_DIV;
        } else if (as_scanner_is(parser->scanner, TOK_PERCENT)) {
            op_type = NODE_REM;
        } else {
            break;
        }
        
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_binary_t* node = (as_node_binary_t*)as_node_new(op_type, loc);
        node->left = left;
        node->right = parse_power_expr(parser);
        left = (as_node_t*)node;
    }
    
    return left;
}

/* Parse additive expression */
static as_node_t* parse_additive_expr(as_parser_t* parser) {
    as_node_t* left = parse_multiplicative_expr(parser);
    if (!left) return NULL;
    
    while (true) {
        as_loc_t loc = get_loc(parser);
        as_node_type_t op_type;
        
        if (as_scanner_is(parser->scanner, TOK_PLUS)) {
            op_type = NODE_ADD;
        } else if (as_scanner_is(parser->scanner, TOK_MINUS)) {
            op_type = NODE_SUB;
        } else {
            break;
        }
        
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_binary_t* node = (as_node_binary_t*)as_node_new(op_type, loc);
        node->left = left;
        node->right = parse_multiplicative_expr(parser);
        left = (as_node_t*)node;
    }
    
    return left;
}

/* Parse relational expression */
static as_node_t* parse_relational_expr(as_parser_t* parser) {
    as_node_t* left = parse_additive_expr(parser);
    if (!left) return NULL;
    
    as_loc_t loc = get_loc(parser);
    as_node_type_t op_type;
    
    if (as_scanner_is(parser->scanner, TOK_LT)) {
        op_type = NODE_LT;
    } else if (as_scanner_is(parser->scanner, TOK_LT_EQ)) {
        op_type = NODE_LTEQ;
    } else if (as_scanner_is(parser->scanner, TOK_GT)) {
        op_type = NODE_GT;
    } else if (as_scanner_is(parser->scanner, TOK_GT_EQ)) {
        op_type = NODE_GTEQ;
    } else {
        return left;
    }
    
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    as_node_binary_t* node = (as_node_binary_t*)as_node_new(op_type, loc);
    node->left = left;
    node->right = parse_additive_expr(parser);
    return (as_node_t*)node;
}

/* Parse equality expression */
static as_node_t* parse_equality_expr(as_parser_t* parser) {
    as_node_t* left = parse_relational_expr(parser);
    if (!left) return NULL;
    
    as_loc_t loc = get_loc(parser);
    as_node_type_t op_type;
    
    if (as_scanner_is(parser->scanner, TOK_EQ2)) {
        op_type = NODE_EQ;
    } else if (as_scanner_is(parser->scanner, TOK_NOT_EQ)) {
        op_type = NODE_NEQ;
    } else {
        return left;
    }
    
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    as_node_binary_t* node = (as_node_binary_t*)as_node_new(op_type, loc);
    node->left = left;
    node->right = parse_relational_expr(parser);
    return (as_node_t*)node;
}

/* Parse logical AND expression */
static as_node_t* parse_and_expr(as_parser_t* parser) {
    as_node_t* left = parse_equality_expr(parser);
    if (!left) return NULL;
    
    while (as_scanner_is(parser->scanner, TOK_AND)) {
        as_loc_t loc = get_loc(parser);
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_binary_t* node = (as_node_binary_t*)as_node_new(NODE_AND, loc);
        node->left = left;
        node->right = parse_equality_expr(parser);
        left = (as_node_t*)node;
    }
    
    return left;
}

/* Parse logical OR expression */
static as_node_t* parse_or_expr(as_parser_t* parser) {
    as_node_t* left = parse_and_expr(parser);
    if (!left) return NULL;
    
    while (as_scanner_is(parser->scanner, TOK_OR2)) {
        as_loc_t loc = get_loc(parser);
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_binary_t* node = (as_node_binary_t*)as_node_new(NODE_OR, loc);
        node->left = left;
        node->right = parse_and_expr(parser);
        left = (as_node_t*)node;
    }
    
    return left;
}

/* Parse assignment expression */
static as_node_t* parse_assignment_expr(as_parser_t* parser) {
    as_node_t* left = parse_or_expr(parser);
    if (!left) return NULL;
    
    as_loc_t loc = get_loc(parser);
    as_node_type_t op_type = NODE_ASSIGN;
    
    if (as_scanner_is(parser->scanner, TOK_EQ)) {
        op_type = NODE_ASSIGN;
    } else if (as_scanner_is(parser->scanner, TOK_PLUS_EQ)) {
        op_type = NODE_ADD_ASSIGN;
    } else if (as_scanner_is(parser->scanner, TOK_MINUS_EQ)) {
        op_type = NODE_SUB_ASSIGN;
    } else {
        return left;
    }
    
    as_scanner_next(parser->scanner);
    skip_newlines(parser);
    
    as_node_assign_t* node = (as_node_assign_t*)as_node_new(op_type, loc);
    node->dest = left;
    node->expr = parse_expression(parser);
    return (as_node_t*)node;
}

/* Parse expression */
static as_node_t* parse_expression(as_parser_t* parser) {
    skip_newlines(parser);
    as_loc_t loc = get_loc(parser);

    /* Handle labeled expression: #label: stmt-or-expr */
    if (as_scanner_is(parser->scanner, TOK_SHARP)) {
        as_token_t* next = as_scanner_lookahead(parser->scanner, 1);
        if (next && next->kind == TOK_IDENTIFIER) {
            as_scanner_next(parser->scanner); /* consume # */
            char* lbl = as_strdup(as_scanner_peek(parser->scanner)->value);
            as_scanner_next(parser->scanner); /* consume identifier */
            if (as_scanner_is(parser->scanner, TOK_COLON)) {
                as_scanner_next(parser->scanner); /* consume : */
                skip_newlines(parser);
                /* Use parse_statement so that loop/while/for/each/eval/if/match all work */
                as_node_t* labeled_node = parse_statement(parser);
                apply_label_to_node(labeled_node, lbl);
                return labeled_node;
            }
            /* Not a label - we consumed tokens we can't restore; treat as error */
            as_free(lbl);
            return as_node_new(NODE_NULL, loc);
        }
    }

    return parse_assignment_expr(parser);
}

/* Apply a label string to a parsed node (transfers ownership of label) */
static void apply_label_to_node(as_node_t* node, char* label) {
    if (!node || !label) { if (label) as_free(label); return; }
    switch (node->type) {
        case NODE_LOOP:     { as_node_loop_t*     n = (as_node_loop_t*)node;     if (n->label) as_free(n->label); n->label = label; return; }
        case NODE_WHILE:    { as_node_while_t*    n = (as_node_while_t*)node;    if (n->label) as_free(n->label); n->label = label; return; }
        case NODE_DO_WHILE: { as_node_do_while_t* n = (as_node_do_while_t*)node; if (n->label) as_free(n->label); n->label = label; return; }
        case NODE_FOR:      { as_node_for_t*      n = (as_node_for_t*)node;      if (n->label) as_free(n->label); n->label = label; return; }
        case NODE_EACH:     { as_node_each_t*     n = (as_node_each_t*)node;     if (n->label) as_free(n->label); n->label = label; return; }
        case NODE_BLOCK:    { as_node_block_t*    n = (as_node_block_t*)node;    if (n->label) as_free(n->label); n->label = label; return; }
        case NODE_IF:       { as_node_if_t*       n = (as_node_if_t*)node;       if (n->label) as_free(n->label); n->label = label; return; }
        case NODE_MATCH:    { as_node_match_t*    n = (as_node_match_t*)node;    if (n->label) as_free(n->label); n->label = label; return; }
        default: as_free(label); return;
    }
}

/* Parse array destructuring pattern: [a, b, c] */
static as_node_t* parse_array_pattern(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    as_scanner_next(parser->scanner); /* consume [ */
    skip_newlines(parser);
    as_node_arr_t* node = (as_node_arr_t*)as_node_new(NODE_ARR, loc);
    node->elements = NULL;
    node->element_count = 0;
    size_t cap = 0;
    while (!as_scanner_is(parser->scanner, TOK_RBRACKET) && !as_scanner_is(parser->scanner, TOK_EOF)) {
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_RBRACKET)) break;
        if (cap <= node->element_count) {
            cap = cap == 0 ? 4 : cap * 2;
            node->elements = as_realloc(node->elements, cap * sizeof(as_node_t*));
        }
        /* Each element is an identifier (or nested pattern) */
        if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
            as_node_identifier_t* id = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
            id->name = as_strdup(as_scanner_peek(parser->scanner)->value);
            id->namespace = NULL;
            as_scanner_next(parser->scanner);
            node->elements[node->element_count++] = (as_node_t*)id;
        } else if (as_scanner_is(parser->scanner, TOK_LBRACKET)) {
            node->elements[node->element_count++] = parse_array_pattern(parser);
        } else if (as_scanner_is(parser->scanner, TOK_LBRACE)) {
            node->elements[node->element_count++] = parse_obj_pattern(parser);
        } else {
            /* Skip unknown token in pattern */
            as_scanner_next(parser->scanner);
            continue;
        }
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_COMMA)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
    }
    if (as_scanner_is(parser->scanner, TOK_RBRACKET))
        as_scanner_next(parser->scanner);
    return (as_node_t*)node;
}

/* Parse object destructuring pattern: { key: varname, key2 } */
static as_node_t* parse_obj_pattern(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    as_scanner_next(parser->scanner); /* consume { */
    skip_newlines(parser);
    as_node_obj_t* node = (as_node_obj_t*)as_node_new(NODE_OBJ, loc);
    node->entries = NULL;
    node->entry_count = 0;
    size_t cap = 0;
    while (!as_scanner_is(parser->scanner, TOK_RBRACE) && !as_scanner_is(parser->scanner, TOK_EOF)) {
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_RBRACE)) break;
        {
            bool key_is_identifier = false;
            char* key = parse_obj_key(parser, &key_is_identifier);
            as_node_t* val_node = NULL;
            if (!key) {
                parser_error(parser, "Invalid object key in destructuring pattern");
                as_scanner_next(parser->scanner);
                continue;
            }
            if (obj_has_key(node, key)) {
                parser_error(parser, "Key %s is duplicated.", key);
                as_free(key);
                continue;
            }
            if (cap <= node->entry_count) {
                cap = cap == 0 ? 4 : cap * 2;
                node->entries = as_realloc(node->entries, cap * sizeof(as_obj_kv_t));
            }
            skip_newlines(parser);
            if (as_scanner_is(parser->scanner, TOK_COLON)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                    as_node_identifier_t* id = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
                    id->name = as_strdup(as_scanner_peek(parser->scanner)->value);
                    id->namespace = NULL;
                    as_scanner_next(parser->scanner);
                    val_node = (as_node_t*)id;
                } else if (as_scanner_is(parser->scanner, TOK_LBRACKET)) {
                    val_node = parse_array_pattern(parser);
                } else if (as_scanner_is(parser->scanner, TOK_LBRACE)) {
                    val_node = parse_obj_pattern(parser);
                }
            } else {
                /* Shorthand: { key } → bind to variable named 'key' */
                if (!key_is_identifier) {
                    parser_error(parser, "Expected ':' after object key '%s'", key);
                }
                as_node_identifier_t* id = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
                id->name = as_strdup(key);
                id->namespace = NULL;
                val_node = (as_node_t*)id;
            }
            node->entries[node->entry_count].key = key;
            node->entries[node->entry_count].value = val_node;
            node->entry_count++;
        }
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_COMMA)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
    }
    if (as_scanner_is(parser->scanner, TOK_RBRACE))
        as_scanner_next(parser->scanner);
    return (as_node_t*)node;
}

/* Parse statement */
static as_node_t* parse_statement(as_parser_t* parser) {
    as_loc_t loc = get_loc(parser);
    
    /* Collect attributes before definitions (#[attr]) */
    as_node_attr_t** attrs = NULL;
    size_t attr_count = 0;
    size_t attr_capacity = 0;
    
    while (as_scanner_is(parser->scanner, TOK_SHARP_BRACKET)) {
        /* Grow attributes array if needed */
        if (attr_count >= attr_capacity) {
            attr_capacity = attr_capacity == 0 ? 4 : attr_capacity * 2;
            attrs = (as_node_attr_t**)as_realloc(attrs, sizeof(as_node_attr_t*) * attr_capacity);
        }
        
        attrs[attr_count++] = parse_attr(parser);
        
        /* Attributes must be on separate lines */
        if (as_scanner_is(parser->scanner, TOK_NEWLINE)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        } else {
            break;
        }
    }
    
    /* Check for label: #identifier: */
    char* label = NULL;
    if (as_scanner_is(parser->scanner, TOK_SHARP)) {
        as_scanner_next(parser->scanner);
        if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
            label = as_strdup(as_scanner_peek(parser->scanner)->value);
            as_scanner_next(parser->scanner);
            if (as_scanner_is(parser->scanner, TOK_COLON)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                /* Label is valid, continue parsing the labeled statement */
            } else {
                /* No colon - not a label, restore and continue */
                as_free(label);
                label = NULL;
            }
        }
    }
    
    /* Function definition: @name(params) { body } */
    if (as_scanner_is(parser->scanner, TOK_AT)) {
        /* Lookahead to see if this is a function definition */
        as_token_t* next = as_scanner_lookahead(parser->scanner, 1);
        if (next && next->kind == TOK_IDENTIFIER) {
            /* Parse function definition */
            as_scanner_next(parser->scanner); /* consume @ */
            skip_newlines(parser);
            
            /* Parse function name */
            char* fn_name = as_strdup(as_scanner_peek(parser->scanner)->value);
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            skip_type_params(parser);
            skip_newlines(parser);
            
            /* Parse parameters */
            size_t param_count = 0;
            as_fn_param_t* params = parse_params(parser, &param_count);
            skip_newlines(parser);
            
            /* Parse optional return type annotation: : type */
            char* return_type = NULL;
            if (as_scanner_is(parser->scanner, TOK_COLON)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                
                return_type = parse_type_annotation(parser);
                skip_newlines(parser);
            }
            
            /* Parse body */
            size_t statement_count = 0;
            as_node_t** statements = parse_block(parser, &statement_count);
            
            /* Create function node */
            as_node_fn_t* fn_node = (as_node_fn_t*)as_node_new(NODE_FN, loc);
            fn_node->params = params;
            fn_node->param_count = param_count;
            fn_node->statements = statements;
            fn_node->statement_count = statement_count;
            fn_node->return_type = return_type;
            
            /* Create identifier for function name */
            as_node_identifier_t* name_node = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
            name_node->name = fn_name;
            
            /* Create definition node */
            as_node_def_t* def_node = (as_node_def_t*)as_node_new(NODE_DEF, loc);
            def_node->dest = (as_node_t*)name_node;
            def_node->expr = (as_node_t*)fn_node;
            def_node->is_mut = false;
            def_node->attrs = attrs;
            def_node->attr_count = attr_count;
            
            return (as_node_t*)def_node;
        }
    }
    
    /* Return statement */
    if (as_scanner_is(parser->scanner, TOK_RETURN)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_return_t* node = (as_node_return_t*)as_node_new(NODE_RETURN, loc);
        
        /* Parse optional return expression */
        if (!as_scanner_is(parser->scanner, TOK_NEWLINE) && 
            !as_scanner_is(parser->scanner, TOK_EOF) &&
            !as_scanner_is(parser->scanner, TOK_SEMICOLON) &&
            !as_scanner_is(parser->scanner, TOK_RBRACE)) {
            node->expr = parse_expression(parser);
        } else {
            node->expr = as_node_new(NODE_NULL, loc);
        }
        
        return (as_node_t*)node;
    }
    
    /* Variable declaration */
    if (as_scanner_is(parser->scanner, TOK_LET) || as_scanner_is(parser->scanner, TOK_VAR)) {
        bool is_mut = as_scanner_is(parser->scanner, TOK_VAR);
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_def_t* node = (as_node_def_t*)as_node_new(NODE_DEF, loc);
        
        /* Parse destination: identifier, array pattern [a,b], or object pattern {k:a} */
        as_node_t* dest_node = NULL;
        if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
            as_node_identifier_t* id_node = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
            id_node->name = as_strdup(as_scanner_peek(parser->scanner)->value);
            id_node->namespace = NULL;
            as_scanner_next(parser->scanner);
            dest_node = (as_node_t*)id_node;
        } else if (as_scanner_is(parser->scanner, TOK_LBRACKET)) {
            dest_node = parse_array_pattern(parser);
        } else if (as_scanner_is(parser->scanner, TOK_LBRACE)) {
            dest_node = parse_obj_pattern(parser);
        } else {
            parser_error(parser, "Expected identifier in variable declaration");
            return NULL;
        }
        
        node->dest = dest_node;
        node->is_mut = is_mut;
        node->type_annotation = NULL;
        node->attrs = attrs;
        node->attr_count = attr_count;
        
        /* Parse optional type annotation: : type */
        if (as_scanner_is(parser->scanner, TOK_COLON)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            node->type_annotation = parse_type_annotation(parser);
        }
        
        skip_newlines(parser);
        if (as_scanner_is(parser->scanner, TOK_EQ)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            node->expr = parse_expression(parser);
        } else {
            node->expr = as_node_new(NODE_NULL, loc);
        }
        
        return (as_node_t*)node;
    }
    
    /* Output statement <: */
    if (as_scanner_is(parser->scanner, TOK_OUT)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        as_node_t* expr = parse_expression(parser);
        /* Convert to a call to print function */
        as_node_call_t* call = (as_node_call_t*)as_node_new(NODE_CALL, loc);
        as_node_identifier_t* print_id = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
        print_id->name = as_strdup("print");
        call->target = (as_node_t*)print_id;
        call->args = as_malloc(sizeof(as_node_t*));
        call->args[0] = expr;
        call->arg_count = 1;
        return (as_node_t*)call;
    }
    
    /* Labeled eval block (#label: eval { ... }). An unlabeled eval is an
     * ordinary expression and is handled by parse_primary_expr so that it can
     * be an operand, e.g. `eval { return 1 } && false`. */
    if (label && as_scanner_is(parser->scanner, TOK_EVAL)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        size_t eval_count = 0;
        as_node_t** eval_stmts = parse_block(parser, &eval_count);
        as_node_block_t* blk = (as_node_block_t*)as_node_new(NODE_BLOCK, loc);
        blk->statements = eval_stmts;
        blk->statement_count = eval_count;
        blk->label = label; label = NULL;
        return (as_node_t*)blk;
    }
    
    /* Loop statement */
    if (as_scanner_is(parser->scanner, TOK_LOOP)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse block */
        size_t statement_count = 0;
        as_node_t** statements = parse_block(parser, &statement_count);
        
        as_node_loop_t* node = (as_node_loop_t*)as_node_new(NODE_LOOP, loc);
        node->statements = statements;
        node->statement_count = statement_count;
        node->label = label; label = NULL;
        
        return (as_node_t*)node;
    }
    
    /* While statement */
    if (as_scanner_is(parser->scanner, TOK_WHILE)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse condition */
        as_node_t* cond = parse_expression(parser);
        skip_newlines(parser);
        
        /* Parse body */
        as_node_t* body = parse_block_or_statement(parser);
        
        as_node_while_t* node = (as_node_while_t*)as_node_new(NODE_WHILE, loc);
        node->cond = cond;
        node->body = body;
        node->label = label; label = NULL;
        
        return (as_node_t*)node;
    }
    
    /* Do-while statement */
    if (as_scanner_is(parser->scanner, TOK_DO)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse body */
        as_node_t* body = parse_block_or_statement(parser);
        skip_newlines(parser);
        
        /* Expect 'while' */
        if (!as_scanner_is(parser->scanner, TOK_WHILE)) {
            parser_error(parser, "Expected 'while' after 'do' body");
            as_node_free(body);
            if (label) as_free(label);
            return NULL;
        }
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse condition */
        as_node_t* cond = parse_expression(parser);
        
        as_node_do_while_t* node = (as_node_do_while_t*)as_node_new(NODE_DO_WHILE, loc);
        node->body = body;
        node->cond = cond;
        node->label = label; label = NULL;
        
        return (as_node_t*)node;
    }
    
    /* Break statement */
    if (as_scanner_is(parser->scanner, TOK_BREAK)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_break_t* node = (as_node_break_t*)as_node_new(NODE_BREAK, loc);
        node->label = NULL;
        node->expr = NULL;
        if (label) { as_free(label); label = NULL; }
        
        /* Check for labeled break: break #label [value] */
        if (as_scanner_is(parser->scanner, TOK_SHARP)) {
            as_scanner_next(parser->scanner);
            if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                node->label = as_strdup(as_scanner_peek(parser->scanner)->value);
                as_scanner_next(parser->scanner);
                /* Parse optional value expression */
                if (!as_scanner_is(parser->scanner, TOK_NEWLINE) &&
                    !as_scanner_is(parser->scanner, TOK_SEMICOLON) &&
                    !as_scanner_is(parser->scanner, TOK_RBRACE) &&
                    !as_scanner_is(parser->scanner, TOK_EOF)) {
                    node->expr = parse_expression(parser);
                }
            }
        }
        
        return (as_node_t*)node;
    }
    
    /* Continue statement */
    if (as_scanner_is(parser->scanner, TOK_CONTINUE)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        as_node_continue_t* node = (as_node_continue_t*)as_node_new(NODE_CONTINUE, loc);
        node->label = NULL;
        
        /* Check for labeled continue: continue #label */
        if (as_scanner_is(parser->scanner, TOK_SHARP)) {
            as_scanner_next(parser->scanner);
            if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                node->label = as_strdup(as_scanner_peek(parser->scanner)->value);
                as_scanner_next(parser->scanner);
            }
        }
        
        return (as_node_t*)node;
    }
    
    /* Each loop */
    if (as_scanner_is(parser->scanner, TOK_EACH)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        bool has_paren = false;
        if (as_scanner_is(parser->scanner, TOK_LPAREN)) {
            has_paren = true;
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
        
        /* Expect 'let' */
        if (!as_scanner_is(parser->scanner, TOK_LET)) {
            parser_error(parser, "Expected 'let' in each loop");
            return as_node_new(NODE_NULL, loc);
        }
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Iterator dest: identifier or destructuring pattern */
        as_node_t* dest_node = NULL;
        if (as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
            as_node_identifier_t* var_node = (as_node_identifier_t*)as_node_new(NODE_IDENTIFIER, loc);
            var_node->name = as_strdup(as_scanner_peek(parser->scanner)->value);
            var_node->namespace = NULL;
            as_scanner_next(parser->scanner);
            dest_node = (as_node_t*)var_node;
        } else if (as_scanner_is(parser->scanner, TOK_LBRACKET)) {
            dest_node = parse_array_pattern(parser);
        } else if (as_scanner_is(parser->scanner, TOK_LBRACE)) {
            dest_node = parse_obj_pattern(parser);
        } else {
            parser_error(parser, "Expected identifier in each loop");
            return as_node_new(NODE_NULL, loc);
        }
        skip_newlines(parser);
        
        /* Expect comma */
        if (!as_scanner_is(parser->scanner, TOK_COMMA)) {
            parser_error(parser, "Expected comma in each loop");
            return as_node_new(NODE_NULL, loc);
        }
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse array expression */
        as_node_t* items_expr = parse_expression(parser);
        skip_newlines(parser);
        
        /* Close paren if needed */
        if (has_paren) {
            if (as_scanner_is(parser->scanner, TOK_RPAREN)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
            }
        }
        
        /* Parse body */
        as_node_t* body = parse_block_or_statement(parser);
        
        /* Create each node */
        as_node_each_t* node = (as_node_each_t*)as_node_new(NODE_EACH, loc);
        node->var = dest_node;
        node->items = items_expr;
        node->body = body;
        node->label = label; label = NULL;
        
        return (as_node_t*)node;
    }
    
    /* For loop */
    if (as_scanner_is(parser->scanner, TOK_FOR)) {
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        bool has_paren = false;
        if (as_scanner_is(parser->scanner, TOK_LPAREN)) {
            has_paren = true;
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
        
        /* Check for range syntax: for (let i = from, to) */
        if (as_scanner_is(parser->scanner, TOK_LET)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            /* Get variable name */
            if (!as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                parser_error(parser, "Expected identifier in for loop");
                return as_node_new(NODE_NULL, loc);
            }
            char* var_name = as_strdup(as_scanner_peek(parser->scanner)->value);
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            /* Parse from value (optional, defaults to 0) */
            as_node_t* from_expr = NULL;
            if (as_scanner_is(parser->scanner, TOK_EQ)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
                from_expr = parse_expression(parser);
                skip_newlines(parser);
            } else {
                /* Default from is 0 */
                as_node_num_t* zero = (as_node_num_t*)as_node_new(NODE_NUM, loc);
                zero->value = 0.0;
                from_expr = (as_node_t*)zero;
            }
            
            /* Expect comma */
            if (!as_scanner_is(parser->scanner, TOK_COMMA)) {
                parser_error(parser, "Expected comma in for loop");
                return as_node_new(NODE_NULL, loc);
            }
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            
            /* Parse to value */
            as_node_t* to_expr = parse_expression(parser);
            skip_newlines(parser);
            
            /* Close paren if needed */
            if (has_paren) {
                if (as_scanner_is(parser->scanner, TOK_RPAREN)) {
                    as_scanner_next(parser->scanner);
                    skip_newlines(parser);
                }
            }
            
            /* Parse body */
            as_node_t* body = parse_block_or_statement(parser);
            
            /* Create for node */
            as_node_for_t* node = (as_node_for_t*)as_node_new(NODE_FOR, loc);
            node->var = var_name;
            node->from = from_expr;
            node->to = to_expr;
            node->times = NULL;
            node->body = body;
            node->label = label; label = NULL;
            
            return (as_node_t*)node;
        } else {
            /* Times syntax: for (n) */
            as_node_t* times_expr = parse_expression(parser);
            skip_newlines(parser);
            
            /* Close paren if needed */
            if (has_paren) {
                if (as_scanner_is(parser->scanner, TOK_RPAREN)) {
                    as_scanner_next(parser->scanner);
                    skip_newlines(parser);
                }
            }
            
            /* Parse body */
            as_node_t* body = parse_block_or_statement(parser);
            
            /* Create for node */
            as_node_for_t* node = (as_node_for_t*)as_node_new(NODE_FOR, loc);
            node->var = NULL;
            node->from = NULL;
            node->to = NULL;
            node->times = times_expr;
            node->body = body;
            node->label = label; label = NULL;
            
            return (as_node_t*)node;
        }
    }
    
    /* Otherwise, parse as expression; apply any pending label to the result */
    as_node_t* expr_node = parse_expression(parser);
    if (label) {
        apply_label_to_node(expr_node, label);
        label = NULL;
    }
    return expr_node;
}

/* Parse block or statement (for if/loop/etc bodies) */
static as_node_t* parse_block_or_statement(as_parser_t* parser) {
    if (as_scanner_is(parser->scanner, TOK_LBRACE)) {
        as_loc_t loc = get_loc(parser);
        as_scanner_next(parser->scanner);
        skip_newlines(parser);
        
        /* Parse statements until we hit } */
        as_node_t** statements = NULL;
        size_t count = 0;
        size_t capacity = 0;
        
        while (!as_scanner_is(parser->scanner, TOK_RBRACE) && 
               !as_scanner_is(parser->scanner, TOK_EOF)) {
            
            if (count >= capacity) {
                capacity = capacity == 0 ? 8 : capacity * 2;
                statements = as_realloc(statements, capacity * sizeof(as_node_t*));
            }
            
            statements[count++] = parse_statement(parser);
            skip_newlines(parser);
            
            /* Skip optional semicolons */
            while (as_scanner_is(parser->scanner, TOK_SEMICOLON)) {
                as_scanner_next(parser->scanner);
                skip_newlines(parser);
            }
        }
        
        if (!as_scanner_is(parser->scanner, TOK_RBRACE)) {
            /* Error: expected } */
            return NULL;
        }
        
        as_scanner_next(parser->scanner); /* consume } */
        
        as_node_block_t* block = (as_node_block_t*)as_node_new(NODE_BLOCK, loc);
        block->statements = statements;
        block->statement_count = count;
        block->label = NULL;
        
        return (as_node_t*)block;
    } else {
        return parse_statement(parser);
    }
}

/* Public API */
as_parser_t* as_parser_new(const char* source) {
    as_parser_t* parser = as_malloc(sizeof(as_parser_t));
    parser->scanner = as_scanner_new(source);
    parser->error_message = NULL;
    return parser;
}

void as_parser_free(as_parser_t* parser) {
    if (!parser) return;
    if (parser->scanner) as_scanner_free(parser->scanner);
    if (parser->error_message) as_free(parser->error_message);
    as_free(parser);
}

as_node_t** as_parser_parse(as_parser_t* parser, size_t* count_out) {
    as_node_t** nodes = NULL;
    size_t count = 0;
    size_t capacity = 0;
    
    skip_newlines(parser);
    
    while (!as_scanner_is(parser->scanner, TOK_EOF) && !parser->error_message) {
        /* A stray ';' terminates the previous top-level item */
        if (as_scanner_is(parser->scanner, TOK_SEMICOLON)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
            continue;
        }

        /* Parse metadata blocks (### { ... }) */
        if (as_scanner_is(parser->scanner, TOK_SHARP3)) {
            as_loc_t meta_loc = get_loc(parser);
            as_scanner_next(parser->scanner);  /* consume ### */
            skip_newlines(parser);
            
            /* Expect opening brace */
            if (as_scanner_is(parser->scanner, TOK_LBRACE)) {
                as_scanner_next(parser->scanner);  /* consume { */
                skip_newlines(parser);
                
                /* Parse metadata key-value pairs */
                while (!as_scanner_is(parser->scanner, TOK_RBRACE) && !as_scanner_is(parser->scanner, TOK_EOF)) {
                    /* Parse key (identifier) */
                    if (!as_scanner_is(parser->scanner, TOK_IDENTIFIER)) {
                        skip_newlines(parser);
                        if (as_scanner_is(parser->scanner, TOK_RBRACE)) break;
                        continue;
                    }
                    
                    char* key = as_strdup(as_scanner_peek(parser->scanner)->value);
                    as_scanner_next(parser->scanner);
                    skip_newlines(parser);
                    
                    /* Expect colon */
                    if (!as_scanner_is(parser->scanner, TOK_COLON)) {
                        as_free(key);
                        break;
                    }
                    as_scanner_next(parser->scanner);  /* consume : */
                    skip_newlines(parser);
                    
                    /* Parse value (must be a literal) */
                    as_node_t* value = parse_primary_expr(parser);
                    skip_newlines(parser);
                    
                    /* Create metadata node */
                    as_node_meta_t* meta = (as_node_meta_t*)as_node_new(NODE_META, meta_loc);
                    meta->name = key;
                    meta->value = value;
                    
                    /* Add to nodes array */
                    AS_ARRAY_GROW(nodes, count, capacity, as_node_t*);
                    nodes[count++] = (as_node_t*)meta;
                    
                    /* Skip optional comma */
                    if (as_scanner_is(parser->scanner, TOK_COMMA)) {
                        as_scanner_next(parser->scanner);
                        skip_newlines(parser);
                    }
                }
                
                if (as_scanner_is(parser->scanner, TOK_RBRACE)) {
                    as_scanner_next(parser->scanner);  /* consume } */
                }
                skip_newlines(parser);
            }
            continue;
        }
        
        /* Parse namespace definitions (:: Name { }) */
        if (as_scanner_is(parser->scanner, TOK_COLON2)) {
            as_node_namespace_t* ns = parse_namespace(parser);
            if (ns) {
                AS_ARRAY_GROW(nodes, count, capacity, as_node_t*);
                nodes[count++] = (as_node_t*)ns;
            }
            skip_newlines(parser);
            continue;
        }
        
        /* Parse import statements (import "path" or import "path" as Alias or from "path" import func1, func2) */
        if (as_scanner_is(parser->scanner, TOK_IMPORT) || as_scanner_is(parser->scanner, TOK_FROM)) {
            as_node_import_t* import = parse_import(parser);
            if (import) {
                AS_ARRAY_GROW(nodes, count, capacity, as_node_t*);
                nodes[count++] = (as_node_t*)import;
            }
            skip_newlines(parser);
            continue;
        }
        
        as_node_t* node = parse_statement(parser);
        if (node) {
            AS_ARRAY_GROW(nodes, count, capacity, as_node_t*);
            nodes[count++] = node;
        }
        
        skip_newlines(parser);
        
        /* Skip optional semicolon/newline */
        if (as_scanner_is(parser->scanner, TOK_SEMICOLON)) {
            as_scanner_next(parser->scanner);
            skip_newlines(parser);
        }
    }
    
    *count_out = count;
    return nodes;
}

