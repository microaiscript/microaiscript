/*
 * AiScript - Interpreter interface
 */

#ifndef AISCRIPT_INTERPRETER_H
#define AISCRIPT_INTERPRETER_H

#include "node.h"
#include "scope.h"
#include "types.h"

/* I/O callback types for embedding (see as_interp_opts_t in aiscript.h) */
/* Receives every value printed with `<:` or print(). */
typedef void  (*as_out_fn_t)(as_value_t* value, void* userdata);
/* Receives runtime error messages instead of them being written to stderr. */
typedef void  (*as_err_fn_t)(const char* message, void* userdata);
typedef char* (*as_in_fn_t)(const char* prompt, void* userdata); /* caller owns returned string */

/* Module cache entry */
typedef struct as_module_cache_entry {
    char* path;        /* Module name/path */
    bool loaded;       /* true if already loaded */
    void* handle;      /* Native module handle (dlopen/LoadLibrary), NULL for .ais */
    bool is_native;    /* true if native module */
    as_value_t* metadata; /* Module metadata as AiScript object, NULL if none */
    struct as_module_cache_entry* next;
} as_module_cache_entry_t;

/* Interpreter state. Fields are exposed for native modules; embedders should
 * use the functions in aiscript.h rather than touching them directly. */
typedef struct as_interpreter {
    as_scope_t* global_scope;
    char* error_message;
    bool aborted;       /* Unwinding for `return`, or after a runtime error (error_message set) */
    bool break_flag;    /* For break statements */
    bool continue_flag; /* For continue statements */
    char* break_label;      /* Target label for labeled break (NULL for unlabeled) */
    char* continue_label;   /* Target label for labeled continue (NULL for unlabeled) */
    as_value_t* break_value;    /* Value carried by break #label expr */
    as_module_cache_entry_t* module_cache;  /* Imported modules cache */
    const char* current_file;  /* Current source filename for error reporting */
    /* Embedding I/O hooks (NULL = use defaults: stdout/stderr/stdin) */
    as_out_fn_t  out_fn;
    as_err_fn_t  err_fn;
    as_in_fn_t   in_fn;
    void*        cb_userdata;
} as_interpreter_t;

/* Create and destroy (documented in aiscript.h) */
AISCRIPT_API as_interpreter_t* as_interpreter_new(void);
AISCRIPT_API void as_interpreter_free(as_interpreter_t* interp);
AISCRIPT_API as_interpreter_t* as_interpreter_current(void);

/* Execution: run parsed nodes in the global scope, or evaluate one node in a given scope. */
AISCRIPT_API as_value_t* as_interpreter_exec_nodes(as_interpreter_t* interp, as_node_t** nodes, size_t count);
AISCRIPT_API as_value_t* as_interpreter_eval_node(as_interpreter_t* interp, as_scope_t* scope, as_node_t* node);

/* Register the standard library (called by as_interpreter_new_ex). */
AISCRIPT_API void as_interpreter_register_std(as_interpreter_t* interp);

/* Call an AiScript function value from C (used by as_interpreter_call) */
AISCRIPT_API as_value_t* as_interpreter_call_value(as_interpreter_t* interp,
                                       as_value_t* fn_val,
                                       as_value_t** args,
                                       size_t arg_count);

#endif /* AISCRIPT_INTERPRETER_H */

