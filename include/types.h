/*
 * AiScript - Type definitions
 */

#ifndef AISCRIPT_TYPES_H
#define AISCRIPT_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Public API visibility. Functions marked AISCRIPT_API are part of the
 * supported embedding interface and are exported from libaiscript. */
#ifndef AISCRIPT_API
#  ifdef _WIN32
#    define AISCRIPT_API __declspec(dllexport)
#  else
#    define AISCRIPT_API __attribute__((visibility("default")))
#  endif
#endif

/* Position in source code */
typedef struct {
    int line;
    int column;
} as_pos_t;

typedef struct {
    as_pos_t start;
    as_pos_t end;
} as_loc_t;

/* Value types */
typedef enum {
    AS_VALUE_NULL,
    AS_VALUE_BOOL,
    AS_VALUE_NUM,
    AS_VALUE_STR,
    AS_VALUE_ARR,
    AS_VALUE_OBJ,
    AS_VALUE_FN,
    AS_VALUE_ERROR
} as_value_type_t;

/* Forward declarations for circular dependencies */
typedef struct as_value as_value_t;
typedef struct as_scope as_scope_t;
typedef struct as_node as_node_t;
typedef struct as_obj_entry as_obj_entry_t;

/* Array value */
typedef struct {
    as_value_t** items;
    size_t length;
    size_t capacity;
} as_arr_t;

/* Object entry (key-value pair) */
struct as_obj_entry {
    char* key;
    as_value_t* value;
    as_obj_entry_t* next; /* For hash table collision */
    as_obj_entry_t* order_next; /* Insertion order */
    as_obj_entry_t* order_prev;
};

/* Object value (hash table) */
typedef struct {
    as_obj_entry_t** buckets;
    size_t bucket_count;
    size_t size;
    as_obj_entry_t* order_head;
    as_obj_entry_t* order_tail;
} as_obj_t;

/* Function parameter */
typedef struct {
    as_node_t* dest;  /* Parameter name/pattern */
    as_node_t* default_value;  /* AST node to evaluate for default value */
    bool optional;
    char* type_annotation;  /* Optional type annotation (e.g., "num", "str") */
} as_fn_param_t;

/* Function value */
typedef enum {
    AS_FN_USER,    /* AiScript function */
    AS_FN_NATIVE   /* C native function */
} as_fn_kind_t;

/* Native function: receives borrowed arguments and returns a new value (never NULL;
 * use as_value_null() for "no result"). `userdata` is the pointer given at registration. */
typedef as_value_t* (*as_native_fn_t)(as_value_t** args, size_t arg_count, void* userdata);

typedef struct {
    as_fn_kind_t kind;
    char* name;
    
    /* For user functions */
    as_fn_param_t* params;
    size_t param_count;
    as_node_t** statements;
    size_t statement_count;
    as_scope_t* closure_scope;
    
    /* For native functions */
    as_native_fn_t native_fn;
    void* userdata;
} as_fn_t;

/* Main value structure */
struct as_value {
    as_value_type_t type;
    union {
        bool bool_val;
        double num_val;
        char* str_val;
        as_arr_t* arr_val;
        as_obj_t* obj_val;
        as_fn_t* fn_val;
        struct {
            char* name;       /* Error name */
            as_value_t* info; /* Optional additional info */
        } error_val;
    } data;
    int ref_count; /* Reference counting for memory management */
};

/* ---- Value constructors ----------------------------------------------
 * Each constructor returns a new value with one reference owned by the
 * caller; release it with as_value_release() (alias: as_value_free()). */
AISCRIPT_API as_value_t* as_value_null(void);
AISCRIPT_API as_value_t* as_value_bool(bool value);
AISCRIPT_API as_value_t* as_value_num(double value);
/* Copies `value`. */
AISCRIPT_API as_value_t* as_value_str(const char* value);
AISCRIPT_API as_value_t* as_value_arr(void);
AISCRIPT_API as_value_t* as_value_obj(void);
/* Create a user function. The value takes ownership of params/statements and
 * retains `scope` as its closure. Used by the interpreter; embedders normally
 * define functions by evaluating AiScript source. */
AISCRIPT_API as_value_t* as_value_fn_user(as_fn_param_t* params, size_t param_count,
                               as_node_t** statements, size_t statement_count,
                               as_scope_t* scope);
/* Create a native function value; see as_native_fn_t for the calling convention. */
AISCRIPT_API as_value_t* as_value_fn_native(as_native_fn_t fn, void* userdata);
/* Create an AiScript error value (Error:create). `info` is retained. */
AISCRIPT_API as_value_t* as_value_error(const char* name, as_value_t* info);

/* ---- Value operations ---------------------------------------------------- */

/* Add a reference and return the same pointer. */
AISCRIPT_API as_value_t* as_value_retain(as_value_t* value);
/* Drop a reference; the value is destroyed when the count reaches zero. NULL is allowed. */
AISCRIPT_API void as_value_release(as_value_t* value);
/* AiScript equality (Core:eq): deep for arrays and objects. */
AISCRIPT_API bool as_value_equals(as_value_t* a, as_value_t* b);
/* Generic truthiness: null, false, 0, NaN and empty strings, arrays and objects are
 * false. (AiScript conditions themselves require an actual bool.) */
AISCRIPT_API bool as_value_to_bool(as_value_t* value);
/* Debug/display representation, e.g. 42, "text" (strings quoted), [1, 2],
 * { a: 1 }. The returned string must be released with free(). */
AISCRIPT_API char* as_value_to_string(as_value_t* value);
/* Format a number as AiScript prints it (NaN, Infinity, shortest round-trip digits). */
AISCRIPT_API void as_num_to_str(double n, char* out, size_t out_size);

/* ---- Array operations ---------------------------------------------------- */

/* Append; the array retains `value`. */
AISCRIPT_API void as_arr_push(as_arr_t* arr, as_value_t* value);
/* Borrowed element, or NULL if out of range. */
AISCRIPT_API as_value_t* as_arr_get(as_arr_t* arr, size_t index);
/* Replace an element; the array retains `value`. */
AISCRIPT_API void as_arr_set(as_arr_t* arr, size_t index, as_value_t* value);

/* ---- Object operations --------------------------------------------------- */

/* Insert or replace; the object retains `value`. Insertion order is preserved. */
AISCRIPT_API void as_obj_set(as_obj_t* obj, const char* key, as_value_t* value);
/* Borrowed value, or NULL if the key is absent. */
AISCRIPT_API as_value_t* as_obj_get(as_obj_t* obj, const char* key);
AISCRIPT_API bool as_obj_has(as_obj_t* obj, const char* key);
AISCRIPT_API void as_obj_delete(as_obj_t* obj, const char* key);

#endif /* AISCRIPT_TYPES_H */


