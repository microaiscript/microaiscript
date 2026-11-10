/*
 * AiScript - Public embedding API
 * C implementation of the AiScript programming language
 *
 * This is the only header an embedding application or a native module needs
 * to include. Link against libaiscript (libaiscript.so / libaiscript.a).
 *
 * Minimal example:
 *
 *     #include <aiscript.h>
 *     #include <stdio.h>
 *     #include <stdlib.h>
 *
 *     int main(void) {
 *         as_interpreter_t* interp = as_interpreter_new();
 *         char* error = NULL;
 *         as_value_t* result = as_eval(interp, "<: Core:v", &error);
 *         if (error) {
 *             fprintf(stderr, "%s\n", error);
 *             free(error);
 *         }
 *         as_value_free(result);
 *         as_interpreter_free(interp);
 *         return 0;
 *     }
 *
 * Memory model: every as_value_t is reference counted. Functions that return
 * a value hand the caller one reference, which must be dropped with
 * as_value_free(). See docs/embedding.md for the full guide.
 */

#ifndef AISCRIPT_H
#define AISCRIPT_H

/* Version of the AiScript language specification implemented, with the "-c"
 * suffix marking this C implementation. Exposed to scripts as Core:v. */
#define AISCRIPT_VERSION "1.3.0-c"

/* Export macro for native modules (see as_modinit_fn_t below). AISCRIPT_API,
 * used for the functions declared here, is defined in types.h. */
#ifdef _WIN32
    #define AS_EXPORT __declspec(dllexport)
#else
    #define AS_EXPORT __attribute__((visibility("default")))
#endif

/* Module metadata structure for native modules */
typedef struct {
    const char* name;        /* Module name */
    const char* version;     /* Module version (e.g., "1.0.0") */
    const char* author;      /* Module author */
    const char* description; /* Module description */
    const char* license;     /* Module license (e.g., "MIT", "GPL") */
    const char* url;         /* Module homepage/repository URL */
} as_module_metadata_t;

/* Internal types that native modules and embedders work with */
#include "types.h"
#include "interpreter.h"
#include "scope.h"

/* -----------------------------------------------------------------------
 * Value accessors
 *
 * Null-safe readers for native modules. Each returns a neutral default
 * (AS_VALUE_NULL, 0.0, "", false) when the value is NULL or has a
 * different type, so check the type first when the distinction matters.
 * ----------------------------------------------------------------------- */

AISCRIPT_API as_value_type_t as_value_get_type(as_value_t* value);
AISCRIPT_API double          as_value_get_num(as_value_t* value);
AISCRIPT_API const char*     as_value_get_str(as_value_t* value);
AISCRIPT_API bool            as_value_get_bool(as_value_t* value);

/* Return the interpreter's global scope (NULL if interp is NULL). Native
 * module init functions use this to register their functions. */
AISCRIPT_API as_scope_t* as_interpreter_get_global_scope(as_interpreter_t* interp);

/* -----------------------------------------------------------------------
 * Embedding API
 * ----------------------------------------------------------------------- */

/* Options passed to as_interpreter_new_ex().
 * Zero-initialise the struct and set only the fields you need; unused
 * fields default to the standard behaviour (stdout / stderr / stdin). */
typedef struct {
    as_out_fn_t  out;       /* called for every <: / print output value  */
    as_err_fn_t  err;       /* receives runtime error messages instead of stderr */
    as_in_fn_t   in;        /* called for readline() input requests       */
    void*        userdata;  /* forwarded to every callback as-is          */
} as_interp_opts_t;

/**
 * Create an interpreter with custom I/O callbacks and the standard library
 * registered. Pass NULL (or a zeroed struct) to get the same behaviour as
 * as_interpreter_new().
 *
 * @param opts Optional callbacks; copied, so it need not outlive the call
 * @return New interpreter, to be released with as_interpreter_free()
 */
AISCRIPT_API as_interpreter_t* as_interpreter_new_ex(const as_interp_opts_t* opts);

/**
 * Create an interpreter that prints to stdout/stderr and reads from stdin.
 * @return New interpreter, to be released with as_interpreter_free()
 */
AISCRIPT_API as_interpreter_t* as_interpreter_new(void);

/**
 * Free an interpreter, its global scope and its module cache.
 * Values previously returned to the caller remain valid until freed.
 * @param interp The interpreter to free (NULL is allowed)
 */
AISCRIPT_API void as_interpreter_free(as_interpreter_t* interp);

/**
 * Parse and execute AiScript source in an interpreter.
 *
 * Definitions persist in the interpreter's global scope between calls, so a
 * host can feed a program in several pieces.
 *
 * On a syntax error NULL is returned and, if error_out is non-NULL,
 * *error_out receives a message that the caller must release with free().
 * On a runtime error NULL is returned, *error_out is left untouched and the
 * message is available from as_interpreter_get_error().
 *
 * @param interp    The interpreter instance
 * @param source    NUL-terminated source code
 * @param error_out Optional pointer that receives a syntax error message
 * @return Value of the last statement (release with as_value_free()), or NULL on error
 */
AISCRIPT_API as_value_t* as_eval(as_interpreter_t* interp, const char* source, char** error_out);

/**
 * Parse AiScript source into an AST without running it.
 *
 * @param source    NUL-terminated source code
 * @param count_out Receives the number of top-level nodes (may be NULL)
 * @param error_out Optional pointer that receives a syntax error message
 *                  (release with free()); NULL is returned in that case
 * @return Array of top-level nodes, release with as_nodes_free()
 */
AISCRIPT_API as_node_t** as_parse(const char* source, size_t* count_out, char** error_out);

/**
 * Execute previously parsed nodes. The nodes are not consumed.
 *
 * @param interp     The interpreter instance
 * @param nodes      Array returned by as_parse()
 * @param node_count Number of nodes
 * @return Value of the last statement (release with as_value_free()), or NULL on error
 */
AISCRIPT_API as_value_t* as_interpreter_exec(as_interpreter_t* interp, as_node_t** nodes, size_t node_count);

/**
 * Release one reference to a value.
 * @param value The value to release (NULL is allowed)
 */
AISCRIPT_API void as_value_free(as_value_t* value);

/**
 * Free an AST node array returned by as_parse().
 * @param nodes The node array to free (NULL is allowed)
 * @param count Number of nodes
 */
AISCRIPT_API void as_nodes_free(as_node_t** nodes, size_t count);

/**
 * Collect the metadata blocks (### { ... }) of a parsed program.
 *
 * @param nodes      Array returned by as_parse()
 * @param node_count Number of nodes
 * @param meta_out   Optional output array with room for the result; pass NULL
 *                   to only get the count. The entries point into nodes.
 * @return Number of metadata nodes found
 */
AISCRIPT_API size_t as_get_metadata(as_node_t** nodes, size_t node_count, as_node_t** meta_out);

/* ---- Global variable helpers ----------------------------------------- */

/**
 * Define (or redefine) a global variable visible to AiScript scripts.
 * The interpreter takes its own reference; the caller keeps theirs.
 */
AISCRIPT_API void as_set_global(as_interpreter_t* interp,
                                const char* name,
                                as_value_t* value);

/**
 * Retrieve a global variable by name. Returns NULL if not found.
 * The returned pointer is owned by the interpreter; retain it with
 * as_value_retain() if you need to keep it alive beyond the next eval call.
 */
AISCRIPT_API as_value_t* as_get_global(as_interpreter_t* interp,
                                       const char* name);

/**
 * Register a native C function as a global.
 *
 * @param fn       Called as fn(args, arg_count, userdata); returns a new value
 *                 (never NULL; return as_value_null() for "no result")
 * @param userdata Passed back to fn on every call
 */
AISCRIPT_API void as_register_fn(as_interpreter_t* interp,
                                 const char* name,
                                 as_native_fn_t fn,
                                 void* userdata);

/* ---- Calling script functions from C ---------------------------------- */

/**
 * Call a top-level AiScript function (defined in a previous as_eval call)
 * by name.
 *
 * @return The function's return value (release with as_value_free()), or NULL
 *         on error; check as_interpreter_get_error() when NULL is returned
 */
AISCRIPT_API as_value_t* as_interpreter_call(as_interpreter_t* interp,
                                             const char* fn_name,
                                             as_value_t** args,
                                             size_t arg_count);

/* ---- Error retrieval -------------------------------------------------- */

/** Return the current error string, or NULL if no error is set. The string
 *  is owned by the interpreter and valid until the next call that sets or
 *  clears an error. */
AISCRIPT_API const char* as_interpreter_get_error(as_interpreter_t* interp);

/** Clear the current error so the interpreter can be reused. */
AISCRIPT_API void as_interpreter_clear_error(as_interpreter_t* interp);

/* ---- Type assertion helpers ------------------------------------------- */
/* Each function returns true if the value has the expected type.
 * On mismatch it returns false and sets the interpreter error message
 * (mentioning fn_hint so the user knows which native function complained). */
AISCRIPT_API bool as_check_num (as_interpreter_t* interp, as_value_t* v, const char* fn_hint);
AISCRIPT_API bool as_check_str (as_interpreter_t* interp, as_value_t* v, const char* fn_hint);
AISCRIPT_API bool as_check_bool(as_interpreter_t* interp, as_value_t* v, const char* fn_hint);
AISCRIPT_API bool as_check_arr (as_interpreter_t* interp, as_value_t* v, const char* fn_hint);
AISCRIPT_API bool as_check_obj (as_interpreter_t* interp, as_value_t* v, const char* fn_hint);
AISCRIPT_API bool as_check_fn  (as_interpreter_t* interp, as_value_t* v, const char* fn_hint);

/* ---- Native modules ---------------------------------------------------- */

/**
 * Native module initialization function signature.
 * `import "name.so"` loads the library and calls
 *     AS_EXPORT void as_modinit_<name>(as_interpreter_t*, as_scope_t*)
 * where <name> is the file name without directory or extension. See
 * docs/MODULE_METADATA.md.
 */
typedef void (*as_modinit_fn_t)(as_interpreter_t*, as_scope_t*);

/**
 * Native module metadata function signature (optional).
 * Modules can export
 *     AS_EXPORT as_module_metadata_t* as_modmeta_<name>(void)
 * to provide metadata, readable from scripts via Core:metadata.
 */
typedef as_module_metadata_t* (*as_modmeta_fn_t)(void);

#endif /* AISCRIPT_H */
