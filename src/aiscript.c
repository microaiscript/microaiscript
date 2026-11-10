/*
 * AiScript - Public API implementation (see include/aiscript.h for the
 * documented contract of each function)
 */

#include "aiscript.h"
#include "parser.h"
#include "interpreter.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

as_node_t** as_parse(const char* source, size_t* count_out, char** error_out) {
    size_t count = 0;
    if (count_out) *count_out = 0;

    as_parser_t* parser = as_parser_new(source);
    if (!parser) {
        if (error_out) *error_out = as_strdup("Failed to create parser");
        return NULL;
    }

    as_node_t** nodes = as_parser_parse(parser, &count);

    if (parser->error_message) {
        if (error_out) *error_out = as_strdup(parser->error_message);
        as_parser_free(parser);
        as_nodes_free(nodes, count);
        return NULL;
    }

    as_parser_free(parser);
    if (count_out) *count_out = count;
    return nodes;
}

/* as_interpreter_new is defined in interpreter.c */

as_value_t* as_interpreter_exec(as_interpreter_t* interp, as_node_t** nodes, size_t node_count) {
    return as_interpreter_exec_nodes(interp, nodes, node_count);
}

as_value_t* as_eval(as_interpreter_t* interp, const char* source, char** error_out) {
    /* An error from a previous call must not make this one fail */
    as_interpreter_clear_error(interp);

    /* Parse source */
    as_parser_t* parser = as_parser_new(source);
    if (!parser) {
        if (error_out) *error_out = as_strdup("Failed to create parser");
        return NULL;
    }
    
    size_t count;
    as_node_t** nodes = as_parser_parse(parser, &count);
    
    if (parser->error_message) {
        if (error_out) *error_out = as_strdup(parser->error_message);
        as_parser_free(parser);
        return NULL;
    }
    
    as_parser_free(parser);
    
    if (!nodes || count == 0) {
        return as_value_null();
    }
    
    /* Execute nodes */
    as_value_t* result = as_interpreter_exec_nodes(interp, nodes, count);
    
    /* Free nodes */
    for (size_t i = 0; i < count; i++) {
        as_node_free(nodes[i]);
    }
    as_free(nodes);
    
    return result;
}

/* Get metadata from parsed nodes */
size_t as_get_metadata(as_node_t** nodes, size_t node_count, as_node_t** meta_out) {
    if (!nodes) return 0;
    
    size_t meta_count = 0;
    
    /* Count and optionally collect metadata nodes */
    for (size_t i = 0; i < node_count; i++) {
        if (nodes[i] && nodes[i]->type == NODE_META) {
            if (meta_out) {
                meta_out[meta_count] = nodes[i];
            }
            meta_count++;
        }
    }
    
    return meta_count;
}

/* as_interpreter_free is defined in interpreter.c */

void as_value_free(as_value_t* value) {
    as_value_release(value);
}

void as_nodes_free(as_node_t** nodes, size_t count) {
    if (!nodes) return;
    for (size_t i = 0; i < count; i++) {
        as_node_free(nodes[i]);
    }
    as_free(nodes);
}

/* -------------------------------------------------------------------------
 * Embedding API — global variable helpers
 * ------------------------------------------------------------------------- */

void as_set_global(as_interpreter_t* interp, const char* name, as_value_t* value) {
    if (!interp || !name || !value) return;
    if (as_scope_exists(interp->global_scope, name)) {
        as_scope_assign(interp->global_scope, name, value);
    } else {
        as_scope_add(interp->global_scope, name, value, false);
    }
}

as_value_t* as_get_global(as_interpreter_t* interp, const char* name) {
    if (!interp || !name) return NULL;
    return as_scope_get(interp->global_scope, name);
}

void as_register_fn(as_interpreter_t* interp, const char* name,
                    as_native_fn_t fn, void* userdata) {
    if (!interp || !name || !fn) return;
    as_value_t* fn_val = as_value_fn_native(fn, userdata);
    as_set_global(interp, name, fn_val);
    as_value_release(fn_val);
}

/* -------------------------------------------------------------------------
 * Embedding API — calling script functions from C
 * ------------------------------------------------------------------------- */

as_value_t* as_interpreter_call(as_interpreter_t* interp, const char* fn_name,
                                 as_value_t** args, size_t arg_count) {
    if (!interp || !fn_name) return NULL;

    as_value_t* fn_val = as_scope_get(interp->global_scope, fn_name);
    if (!fn_val) {
        if (interp->error_message) as_free(interp->error_message);
        char buf[256];
        snprintf(buf, sizeof(buf), "as_interpreter_call: '%s' is not defined", fn_name);
        interp->error_message = as_strdup(buf);
        return NULL;
    }
    if (fn_val->type != AS_VALUE_FN) {
        if (interp->error_message) as_free(interp->error_message);
        char buf[256];
        snprintf(buf, sizeof(buf), "as_interpreter_call: '%s' is not a function", fn_name);
        interp->error_message = as_strdup(buf);
        return NULL;
    }

    return as_interpreter_call_value(interp, fn_val, args, arg_count);
}

/* -------------------------------------------------------------------------
 * Embedding API — error retrieval
 * ------------------------------------------------------------------------- */

const char* as_interpreter_get_error(as_interpreter_t* interp) {
    if (!interp) return NULL;
    return interp->error_message;
}

void as_interpreter_clear_error(as_interpreter_t* interp) {
    if (!interp) return;
    if (interp->error_message) {
        as_free(interp->error_message);
        interp->error_message = NULL;
    }
    interp->aborted = false;
}

/* -------------------------------------------------------------------------
 * Embedding API — type assertion helpers
 * ------------------------------------------------------------------------- */

static bool as_check_type(as_interpreter_t* interp, as_value_t* v,
                           as_value_type_t expected, const char* type_name,
                           const char* fn_hint) {
    if (!v || v->type != expected) {
        if (interp) {
            if (interp->error_message) as_free(interp->error_message);
            char buf[256];
            const char* actual = v ? (const char*[]){
                "null","bool","num","str","arr","obj","fn","error"
            }[v->type < 8 ? v->type : 7] : "null";
            if (fn_hint) {
                snprintf(buf, sizeof(buf), "%s: expected %s, got %s",
                         fn_hint, type_name, actual);
            } else {
                snprintf(buf, sizeof(buf), "expected %s, got %s",
                         type_name, actual);
            }
            interp->error_message = as_strdup(buf);
        }
        return false;
    }
    return true;
}

bool as_check_num (as_interpreter_t* i, as_value_t* v, const char* h) {
    return as_check_type(i, v, AS_VALUE_NUM,   "num",  h);
}
bool as_check_str (as_interpreter_t* i, as_value_t* v, const char* h) {
    return as_check_type(i, v, AS_VALUE_STR,   "str",  h);
}
bool as_check_bool(as_interpreter_t* i, as_value_t* v, const char* h) {
    return as_check_type(i, v, AS_VALUE_BOOL,  "bool", h);
}
bool as_check_arr (as_interpreter_t* i, as_value_t* v, const char* h) {
    return as_check_type(i, v, AS_VALUE_ARR,   "arr",  h);
}
bool as_check_obj (as_interpreter_t* i, as_value_t* v, const char* h) {
    return as_check_type(i, v, AS_VALUE_OBJ,   "obj",  h);
}
bool as_check_fn  (as_interpreter_t* i, as_value_t* v, const char* h) {
    return as_check_type(i, v, AS_VALUE_FN,    "fn",   h);
}

