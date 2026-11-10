/*
 * AiScript Standard Library - Core Namespace Implementation
 */

#include "aiscript.h"
#include "std_core.h"
#include "std_async.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include "node.h"
#include "error.h"
#include "vm.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* Platform-specific sleep headers */
#ifdef _WIN32
    #include <windows.h>
#else
    #ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 199309L  /* Enable nanosleep */
    #endif
    #include <unistd.h>
    #include <time.h>
#endif

/* Platform-specific dynamic loading headers */
#ifndef _WIN32
    #include <dlfcn.h>
#endif

/* Platform-specific readline headers */
#ifdef HAVE_READLINE
    #include <readline/readline.h>
    #include <readline/history.h>
#endif

typedef struct {
    void** items;
    size_t count;
    size_t cap;
} ptr_set_t;

static int ptr_set_has(ptr_set_t* set, void* p)
{
    size_t i;
    for (i = 0; i < set->count; i++) {
        if (set->items[i] == p) {
            return 1;
        }
    }
    return 0;
}

static void ptr_set_push(ptr_set_t* set, void* p)
{
    if (set->count >= set->cap) {
        set->cap = set->cap ? set->cap * 2 : 8;
        set->items = as_realloc(set->items, set->cap * sizeof(void*));
    }
    set->items[set->count++] = p;
}

static char* str_append(char* dst, size_t* len, const char* add)
{
    size_t n = strlen(add);
    dst = as_realloc(dst, *len + n + 1);
    memcpy(dst + *len, add, n + 1);
    *len += n;
    return dst;
}

static char* repr_value(as_value_t* value, int literal_like, ptr_set_t* arrs, ptr_set_t* objs);

static char* repr_fn(as_value_t* value)
{
    as_fn_t* fn;
    char* out;
    size_t len = 0;
    size_t i;
    if (!value || value->type != AS_VALUE_FN || !value->data.fn_val) {
        return as_strdup("?");
    }
    fn = value->data.fn_val;
    if (fn->kind == AS_FN_NATIVE) {
        as_value_t* ud = (as_value_t*)fn->userdata;
        if (ud && ud->type == AS_VALUE_ARR && ud->data.arr_val &&
            ud->data.arr_val->length >= 4 &&
            ud->data.arr_val->items[0]->type == AS_VALUE_NUM &&
            ud->data.arr_val->items[1]->type == AS_VALUE_NUM) {
            as_arr_t* a = ud->data.arr_val;
            size_t param_count = (size_t)a->items[1]->data.num_val;
            size_t ncap = (size_t)a->items[2]->data.num_val;
            size_t name_i = 4 + ncap + param_count;
            out = as_strdup("@( ");
            len = strlen(out);
            for (i = 0; i < param_count; i++) {
                const char* pname = "?";
                if (i > 0) {
                    out = str_append(out, &len, ", ");
                }
                if (name_i + i < a->length && a->items[name_i + i] &&
                    a->items[name_i + i]->type == AS_VALUE_STR &&
                    a->items[name_i + i]->data.str_val) {
                    pname = a->items[name_i + i]->data.str_val;
                }
                out = str_append(out, &len, pname);
            }
            out = str_append(out, &len, " ) { ... }");
            return out;
        }
        return as_strdup("@( ?? ) { native code }");
    }
    out = as_strdup("@( ");
    len = strlen(out);
    for (i = 0; i < fn->param_count; i++) {
        const char* pname = "?";
        if (i > 0) {
            out = str_append(out, &len, ", ");
        }
        if (fn->params && fn->params[i].dest &&
            fn->params[i].dest->type == NODE_IDENTIFIER) {
            as_node_identifier_t* id = (as_node_identifier_t*)fn->params[i].dest;
            if (id->name) {
                pname = id->name;
            }
        }
        out = str_append(out, &len, pname);
    }
    out = str_append(out, &len, " ) { ... }");
    return out;
}

static char* repr_value(as_value_t* value, int literal_like, ptr_set_t* arrs, ptr_set_t* objs)
{
    if (!value) {
        return as_strdup("null");
    }
    switch (value->type) {
        case AS_VALUE_NULL:
            return as_strdup("null");
        case AS_VALUE_BOOL:
            return as_strdup(value->data.bool_val ? "true" : "false");
        case AS_VALUE_NUM: {
            char buf[64];
            as_num_to_str(value->data.num_val, buf, sizeof(buf));
            return as_strdup(buf);
        }
        case AS_VALUE_STR:
            if (!literal_like) {
                return as_strdup(value->data.str_val ? value->data.str_val : "");
            } else {
                const char* s = value->data.str_val ? value->data.str_val : "";
                size_t n = strlen(s);
                char* out = as_malloc(n * 2 + 3);
                size_t o = 0;
                size_t i;
                out[o++] = '"';
                for (i = 0; i < n; i++) {
                    char c = s[i];
                    if (c == '\\' || c == '"' || c == '\r' || c == '\n') {
                        out[o++] = '\\';
                        if (c == '\r') {
                            out[o++] = 'r';
                        } else if (c == '\n') {
                            out[o++] = 'n';
                        } else {
                            out[o++] = c;
                        }
                    } else {
                        out[o++] = c;
                    }
                }
                out[o++] = '"';
                out[o] = 0;
                return out;
            }
        case AS_VALUE_ARR: {
            as_arr_t* arr = value->data.arr_val;
            char* out;
            size_t len = 0;
            size_t i;
            size_t saved;
            if (ptr_set_has(arrs, arr)) {
                return as_strdup("...");
            }
            saved = arrs->count;
            ptr_set_push(arrs, arr);
            out = as_strdup("[ ");
            len = 2;
            for (i = 0; i < arr->length; i++) {
                char* item;
                if (i > 0) {
                    out = str_append(out, &len, ", ");
                }
                item = repr_value(arr->items[i], 1, arrs, objs);
                out = str_append(out, &len, item);
                as_free(item);
            }
            out = str_append(out, &len, " ]");
            arrs->count = saved;
            return out;
        }
        case AS_VALUE_OBJ: {
            as_obj_t* obj = value->data.obj_val;
            char* out;
            size_t len = 0;
            as_obj_entry_t* e;
            size_t saved;
            int first = 1;
            if (ptr_set_has(objs, obj)) {
                return as_strdup("...");
            }
            saved = objs->count;
            ptr_set_push(objs, obj);
            out = as_strdup("{ ");
            len = 2;
            for (e = obj->order_head; e; e = e->order_next) {
                char* item;
                if (!first) {
                    out = str_append(out, &len, ", ");
                }
                first = 0;
                out = str_append(out, &len, e->key);
                out = str_append(out, &len, ": ");
                item = repr_value(e->value, 1, arrs, objs);
                out = str_append(out, &len, item);
                as_free(item);
            }
            out = str_append(out, &len, " }");
            objs->count = saved;
            return out;
        }
        case AS_VALUE_FN:
            return repr_fn(value);
        default:
            return as_strdup("?");
    }
}

/*
 * Helper: Convert value to string representation
 * This uses the same logic as the interpreter's value display
 */
static char* value_to_string(as_value_t* value) {
    ptr_set_t arrs = {0};
    ptr_set_t objs = {0};
    char* s = repr_value(value, 0, &arrs, &objs);
    as_free(arrs.items);
    as_free(objs.items);
    return s;
}

/* ============================================================================
 * REQUIRED CORE UTILITIES (Cannot be disabled)
 * ============================================================================ */

/* Core:type(v): str - Get type name */
static as_value_t* std_core_type(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1) {
        fprintf(stderr, "Core:type: Expected 1 argument\n");
        return as_value_str("unknown");
    }
    
    as_value_t* v = args[0];
    const char* type_name = NULL;
    
    switch (v->type) {
        case AS_VALUE_NULL:  type_name = "null"; break;
        case AS_VALUE_BOOL:  type_name = "bool"; break;
        case AS_VALUE_NUM:   type_name = "num"; break;
        case AS_VALUE_STR:   type_name = "str"; break;
        case AS_VALUE_ARR:   type_name = "arr"; break;
        case AS_VALUE_OBJ:   type_name = "obj"; break;
        case AS_VALUE_FN:    type_name = "fn"; break;
        case AS_VALUE_ERROR: type_name = "error"; break;
        default:             type_name = "unknown"; break;
    }
    
    return as_value_str(type_name);
}

/* Core:to_str(v): str - Convert value to string */
static as_value_t* std_core_to_str(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1) {
        return as_value_str("");
    }
    
    char* str = value_to_string(args[0]);
    as_value_t* result = as_value_str(str);
    as_free(str);
    
    return result;
}

/* Core:range(a, b): arr<num> - Generate number sequence */
static as_value_t* std_core_range(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2) {
        fprintf(stderr, "Core:range: Expected 2 arguments\n");
        return as_value_arr();
    }
    
    if (args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
        fprintf(stderr, "Core:range: Arguments must be numbers\n");
        return as_value_arr();
    }
    
    double a = args[0]->data.num_val;
    double b = args[1]->data.num_val;
    
    as_value_t* result = as_value_arr();
    
    if (a < b) {
        /* Ascending: a to b */
        for (double i = a; i <= b; i += 1.0) {
            as_value_t* num = as_value_num(i);
            as_arr_push(result->data.arr_val, num);
            as_value_release(num);
        }
    } else if (a > b) {
        /* Descending: a to b */
        for (double i = a; i >= b; i -= 1.0) {
            as_value_t* num = as_value_num(i);
            as_arr_push(result->data.arr_val, num);
            as_value_release(num);
        }
    } else {
        /* Equal: return [a] */
        as_value_t* num = as_value_num(a);
        as_arr_push(result->data.arr_val, num);
        as_value_release(num);
    }
    
    return result;
}

/* Core:sleep(ms): void - Sleep for specified milliseconds */
static as_value_t* std_core_sleep(as_value_t** args, size_t arg_count, void* userdata) {
    as_interpreter_t* interp = (as_interpreter_t*)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) {
        fprintf(stderr, "Core:sleep: Expected number argument\n");
        return as_value_null();
    }
    
    double ms = args[0]->data.num_val;
    if (ms < 0) {
        ms = 0;
    }
    
    /* Sleep for specified milliseconds */
    #ifdef _WIN32
        /* Windows: use Sleep (milliseconds) */
        Sleep((DWORD)ms);
    #else
        /* POSIX: use nanosleep (more portable than usleep) */
        struct timespec ts;
        long ms_long = (long)ms;
        ts.tv_sec = ms_long / 1000;
        ts.tv_nsec = (ms_long % 1000) * 1000000;
        nanosleep(&ts, NULL);
    #endif
    
    /* Dispatch any async callbacks that fired during sleep */
    if (interp) as_std_async_dispatch(interp);
    
    return as_value_null();
}

/* Core:abort(msg): never - Abort execution */
static as_value_t* std_core_abort(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    const char* message = "Execution aborted";
    if (arg_count >= 1 && args[0]->type == AS_VALUE_STR) {
        message = args[0]->data.str_val;
    }
    as_raise_runtime_error(message);
    return as_value_null();
}

/* ============================================================================
 * OPTIONAL OPERATOR FUNCTIONS (Can be disabled via compiler flags)
 * ============================================================================ */

/* Helper macro for creating stub functions */
#define STUB_FUNCTION(name, msg) \
    static as_value_t* name(as_value_t** args, size_t arg_count, void* userdata) { \
        (void)args; (void)arg_count; (void)userdata; \
        fprintf(stderr, "Function " #name " is not available in this build.\n"); \
        fprintf(stderr, "Disabled features: %s\n", as_std_core_get_disabled_list()); \
        return as_value_null(); \
    }

/* Logical operations */
#ifndef NO_CORE_LOGICAL
    #ifndef NO_CORE_NOT
        static as_value_t* std_core_not(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 1 || args[0]->type != AS_VALUE_BOOL) {
                return as_value_bool(false);
            }
            return as_value_bool(!args[0]->data.bool_val);
        }
    #else
        STUB_FUNCTION(std_core_not, "Core:not")
    #endif
    
    #ifndef NO_CORE_AND
        static as_value_t* std_core_and(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_BOOL || args[1]->type != AS_VALUE_BOOL) {
                return as_value_bool(false);
            }
            return as_value_bool(args[0]->data.bool_val && args[1]->data.bool_val);
        }
    #else
        STUB_FUNCTION(std_core_and, "Core:and")
    #endif
    
    #ifndef NO_CORE_OR
        static as_value_t* std_core_or(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_BOOL || args[1]->type != AS_VALUE_BOOL) {
                return as_value_bool(false);
            }
            return as_value_bool(args[0]->data.bool_val || args[1]->data.bool_val);
        }
    #else
        STUB_FUNCTION(std_core_or, "Core:or")
    #endif
#else
    STUB_FUNCTION(std_core_not, "Core:not")
    STUB_FUNCTION(std_core_and, "Core:and")
    STUB_FUNCTION(std_core_or, "Core:or")
#endif

/* Comparison operations */
#ifndef NO_CORE_COMPARISON
    /* Helper for value comparison */
    static bool values_equal(as_value_t* a, as_value_t* b) {
        if (a->type != b->type) return false;
        
        switch (a->type) {
            case AS_VALUE_NULL: return true;
            case AS_VALUE_BOOL: return a->data.bool_val == b->data.bool_val;
            case AS_VALUE_NUM: return a->data.num_val == b->data.num_val;
            case AS_VALUE_STR: return strcmp(a->data.str_val, b->data.str_val) == 0;
            default: return false;  /* Arrays/objects/functions not compared */
        }
    }
    
    #ifndef NO_CORE_EQ
        static as_value_t* std_core_eq(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2) {
                as_raise_runtime_error("Core:eq requires 2 arguments");
                return as_value_null();
            }
            return as_value_bool(values_equal(args[0], args[1]));
        }
    #else
        STUB_FUNCTION(std_core_eq, "Core:eq")
    #endif
    
    #ifndef NO_CORE_NEQ
        static as_value_t* std_core_neq(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2) return as_value_bool(false);
            return as_value_bool(!values_equal(args[0], args[1]));
        }
    #else
        STUB_FUNCTION(std_core_neq, "Core:neq")
    #endif
    
    #ifndef NO_CORE_GT
        static as_value_t* std_core_gt(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_bool(false);
            }
            return as_value_bool(args[0]->data.num_val > args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_gt, "Core:gt")
    #endif
    
    #ifndef NO_CORE_LT
        static as_value_t* std_core_lt(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_bool(false);
            }
            return as_value_bool(args[0]->data.num_val < args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_lt, "Core:lt")
    #endif
    
    #ifndef NO_CORE_GTEQ
        static as_value_t* std_core_gteq(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_bool(false);
            }
            return as_value_bool(args[0]->data.num_val >= args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_gteq, "Core:gteq")
    #endif
    
    #ifndef NO_CORE_LTEQ
        static as_value_t* std_core_lteq(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_bool(false);
            }
            return as_value_bool(args[0]->data.num_val <= args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_lteq, "Core:lteq")
    #endif
#else
    STUB_FUNCTION(std_core_eq, "Core:eq")
    STUB_FUNCTION(std_core_neq, "Core:neq")
    STUB_FUNCTION(std_core_gt, "Core:gt")
    STUB_FUNCTION(std_core_lt, "Core:lt")
    STUB_FUNCTION(std_core_gteq, "Core:gteq")
    STUB_FUNCTION(std_core_lteq, "Core:lteq")
#endif

/* Arithmetic operations */
#ifndef NO_CORE_ARITHMETIC
    #ifndef NO_CORE_ADD
        static as_value_t* std_core_add(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_num(0);
            }
            return as_value_num(args[0]->data.num_val + args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_add, "Core:add")
    #endif
    
    #ifndef NO_CORE_SUB
        static as_value_t* std_core_sub(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_num(0);
            }
            return as_value_num(args[0]->data.num_val - args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_sub, "Core:sub")
    #endif
    
    #ifndef NO_CORE_MUL
        static as_value_t* std_core_mul(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_num(0);
            }
            return as_value_num(args[0]->data.num_val * args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_mul, "Core:mul")
    #endif
    
    #ifndef NO_CORE_DIV
        static as_value_t* std_core_div(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_num(0);
            }
            return as_value_num(args[0]->data.num_val / args[1]->data.num_val);
        }
    #else
        STUB_FUNCTION(std_core_div, "Core:div")
    #endif
    
    #ifndef NO_CORE_MOD
        static as_value_t* std_core_mod(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_num(0);
            }
            return as_value_num(fmod(args[0]->data.num_val, args[1]->data.num_val));
        }
    #else
        STUB_FUNCTION(std_core_mod, "Core:mod")
    #endif
    
    #ifndef NO_CORE_POW
        static as_value_t* std_core_pow(as_value_t** args, size_t arg_count, void* userdata) {
            (void)userdata;
            if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
                return as_value_num(0);
            }
            return as_value_num(pow(args[0]->data.num_val, args[1]->data.num_val));
        }
    #else
        STUB_FUNCTION(std_core_pow, "Core:pow")
    #endif
#else
    STUB_FUNCTION(std_core_add, "Core:add")
    STUB_FUNCTION(std_core_sub, "Core:sub")
    STUB_FUNCTION(std_core_mul, "Core:mul")
    STUB_FUNCTION(std_core_div, "Core:div")
    STUB_FUNCTION(std_core_mod, "Core:mod")
    STUB_FUNCTION(std_core_pow, "Core:pow")
#endif

/* ============================================================================
 * Module Management Functions
 * ============================================================================ */

/* Helper: Remove all variables with a given namespace prefix from scope */
static void remove_namespace_from_scope(as_scope_t* scope, const char* namespace_name) {
    /* Create the prefix to match: "namespace:" */
    size_t prefix_len = strlen(namespace_name) + 1;  /* +1 for ':' */
    char* prefix = (char*)as_malloc(prefix_len + 1);
    snprintf(prefix, prefix_len + 1, "%s:", namespace_name);
    
    /* Iterate through all variables in scope */
    for (size_t i = 0; i < scope->var_count; i++) {
        as_variable_t* var = scope->variables[i];
        
        /* Check if variable name starts with prefix */
        if (strncmp(var->name, prefix, prefix_len) == 0) {
            /* Mark for removal by setting name to NULL */
            /* We'll actually remove it in a second pass to avoid index issues */
            as_free(var->name);
            var->name = NULL;
            as_value_release(var->value);
            var->value = NULL;
        }
    }
    
    /* Second pass: compact the array by removing NULL entries */
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < scope->var_count; read_idx++) {
        if (scope->variables[read_idx]->name != NULL) {
            if (write_idx != read_idx) {
                scope->variables[write_idx] = scope->variables[read_idx];
            }
            write_idx++;
        } else {
            /* Free the variable structure itself */
            as_free(scope->variables[read_idx]);
        }
    }
    scope->var_count = write_idx;
    
    as_free(prefix);
}

/* Core:unload_module(name: str) - Unload a loaded module */
static as_value_t* std_core_unload_module(as_value_t** args, size_t arg_count, void* userdata) {
    as_interpreter_t* interp = (as_interpreter_t*)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Runtime error: Core:unload_module requires a string argument\n");
        return as_value_bool(false);
    }
    
    const char* module_name = args[0]->data.str_val;
    
    /* Find module in cache */
    as_module_cache_entry_t** cache_ptr = &interp->module_cache;
    while (*cache_ptr) {
        as_module_cache_entry_t* entry = *cache_ptr;
        if (strcmp(entry->path, module_name) == 0) {
            /* Found it - unload */
            
            /* Remove all namespaced functions from global scope */
            remove_namespace_from_scope(interp->global_scope, module_name);
            
            /* Close native module if applicable */
            if (entry->is_native && entry->handle) {
                #ifdef _WIN32
                    FreeLibrary((HMODULE)entry->handle);
                #else
                    dlclose(entry->handle);
                #endif
            }
            
            /* Remove from cache */
            *cache_ptr = entry->next;
            as_free(entry->path);
            as_free(entry);
            
            return as_value_bool(true);
        }
        cache_ptr = &(*cache_ptr)->next;
    }
    
    /* Module not found */
    return as_value_bool(false);
}

/* Core:reload_module(name: str) - Force reload a module */
static as_value_t* std_core_reload_module(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Runtime error: Core:reload_module requires a string argument\n");
        return as_value_bool(false);
    }
    
    const char* module_name = args[0]->data.str_val;
    
    /* Unload first */
    as_value_t* unload_result = std_core_unload_module(args, arg_count, userdata);
    bool was_loaded = unload_result->data.bool_val;
    as_value_release(unload_result);
    
    if (!was_loaded) {
        fprintf(stderr, "Warning: Module '%s' was not loaded, cannot reload\n", module_name);
        return as_value_bool(false);
    }
    
    /* Note: Actual reloading requires import statement to be re-executed
     * This function just clears the cache to allow re-import */
    
    return as_value_bool(true);
}

/* Core:metadata(module_name) - Get metadata for a loaded module */
static as_value_t* std_core_metadata(as_value_t** args, size_t arg_count, void* userdata) {
    as_interpreter_t* interp = (as_interpreter_t*)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Runtime error: Core:metadata requires a string argument\n");
        return as_value_null();
    }
    
    const char* module_name = args[0]->data.str_val;
    
    /* Find module in cache */
    as_module_cache_entry_t* cache = interp->module_cache;
    while (cache) {
        if (strcmp(cache->path, module_name) == 0 && cache->loaded) {
            if (cache->metadata) {
                /* Return a copy of the metadata object */
                as_value_retain(cache->metadata);
                return cache->metadata;
            } else {
                /* Module loaded but has no metadata */
                return as_value_null();
            }
        }
        cache = cache->next;
    }
    
    /* Module not loaded */
    fprintf(stderr, "Runtime error: Module '%s' not loaded\n", module_name);
    return as_value_null();
}

/* ============================================================================
 * Registration and Utility Functions
 * ============================================================================ */

void as_std_core_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    
    /* Constants */
    as_value_t* help = as_value_str("SEE: https://aiscript-dev.github.io/guides/get-started.html");
    as_scope_add(global, "help", help, false);
    as_value_release(help);

    as_value_t* version = as_value_str(AISCRIPT_VERSION);
    as_scope_add(global, "Core:v", version, false);
    as_value_release(version);
    
    as_value_t* ai = as_value_str("kawaii");
    as_scope_add(global, "Core:ai", ai, false);
    as_value_release(ai);
    
    /* Module search paths (Core:modpath) - mutable array */
    as_value_t* modpath = as_value_arr();
    as_arr_t* modpath_arr = modpath->data.arr_val;
    
    /* Add default search paths */
    as_value_t* path1 = as_value_str(".");  /* Current directory */
    as_arr_push(modpath_arr, path1);
    as_value_release(path1);
    
    #ifdef _WIN32
        /* Windows default paths */
        as_value_t* path2 = as_value_str("C:\\Program Files\\AiScript\\lib");
        as_arr_push(modpath_arr, path2);
        as_value_release(path2);
        
        as_value_t* path3 = as_value_str("C:\\Program Files\\AiScript\\modules");
        as_arr_push(modpath_arr, path3);
        as_value_release(path3);
    #else
        /* Unix/Linux default paths */
        as_value_t* path2 = as_value_str("/usr/local/lib/aiscript");
        as_arr_push(modpath_arr, path2);
        as_value_release(path2);
        
        as_value_t* path3 = as_value_str("/usr/lib/aiscript");
        as_arr_push(modpath_arr, path3);
        as_value_release(path3);
        
        /* User home directory path (if HOME is set) */
        const char* home = getenv("HOME");
        if (home) {
            char user_path[512];
            snprintf(user_path, sizeof(user_path), "%s/.local/lib/aiscript", home);
            as_value_t* path4 = as_value_str(user_path);
            as_arr_push(modpath_arr, path4);
            as_value_release(path4);
        }
    #endif
    
    as_scope_add(global, "Core:modpath", modpath, true);  /* Mutable */
    as_value_release(modpath);
    
    /* Required utilities */
    as_value_t* fn;
    
    fn = as_value_fn_native(std_core_type, NULL);
    as_scope_add(global, "Core:type", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_to_str, NULL);
    as_scope_add(global, "Core:to_str", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_range, NULL);
    as_scope_add(global, "Core:range", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_sleep, interp);
    as_scope_add(global, "Core:sleep", fn, false);
    as_value_release(fn);
    
    /* Module management functions */
    fn = as_value_fn_native(std_core_unload_module, interp);
    as_scope_add(global, "Core:unload_module", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_reload_module, interp);
    as_scope_add(global, "Core:reload_module", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_abort, NULL);
    as_scope_add(global, "Core:abort", fn, false);
    as_value_release(fn);
    
    /* Optional logical operations */
    fn = as_value_fn_native(std_core_not, NULL);
    as_scope_add(global, "Core:not", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_and, NULL);
    as_scope_add(global, "Core:and", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_or, NULL);
    as_scope_add(global, "Core:or", fn, false);
    as_value_release(fn);
    
    /* Optional comparison operations */
    fn = as_value_fn_native(std_core_eq, NULL);
    as_scope_add(global, "Core:eq", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_neq, NULL);
    as_scope_add(global, "Core:neq", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_gt, NULL);
    as_scope_add(global, "Core:gt", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_lt, NULL);
    as_scope_add(global, "Core:lt", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_gteq, NULL);
    as_scope_add(global, "Core:gteq", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_lteq, NULL);
    as_scope_add(global, "Core:lteq", fn, false);
    as_value_release(fn);
    
    /* Optional arithmetic operations */
    fn = as_value_fn_native(std_core_add, NULL);
    as_scope_add(global, "Core:add", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_sub, NULL);
    as_scope_add(global, "Core:sub", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_mul, NULL);
    as_scope_add(global, "Core:mul", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_div, NULL);
    as_scope_add(global, "Core:div", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_mod, NULL);
    as_scope_add(global, "Core:mod", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_core_pow, NULL);
    as_scope_add(global, "Core:pow", fn, false);
    as_value_release(fn);
    
    /* Module metadata function */
    as_value_t* metadata_fn = as_value_fn_native(std_core_metadata, interp);
    as_scope_add(global, "Core:metadata", metadata_fn, false);
    as_value_release(metadata_fn);
}

bool as_std_core_is_available(const char* function_name) {
    /* All required functions are always available */
    if (strcmp(function_name, "Core:type") == 0) return true;
    if (strcmp(function_name, "Core:to_str") == 0) return true;
    if (strcmp(function_name, "Core:range") == 0) return true;
    if (strcmp(function_name, "Core:abort") == 0) return true;
    
    /* Check optional functions */
    #ifdef NO_CORE_LOGICAL
        if (strstr(function_name, "Core:not") || strstr(function_name, "Core:and") || 
            strstr(function_name, "Core:or")) return false;
    #endif
    
    #ifdef NO_CORE_COMPARISON
        if (strstr(function_name, "Core:eq") || strstr(function_name, "Core:neq") ||
            strstr(function_name, "Core:gt") || strstr(function_name, "Core:lt") ||
            strstr(function_name, "Core:gteq") || strstr(function_name, "Core:lteq")) return false;
    #endif
    
    #ifdef NO_CORE_ARITHMETIC
        if (strstr(function_name, "Core:add") || strstr(function_name, "Core:sub") ||
            strstr(function_name, "Core:mul") || strstr(function_name, "Core:div") ||
            strstr(function_name, "Core:mod") || strstr(function_name, "Core:pow")) return false;
    #endif
    
    return true;
}

const char* as_std_core_get_disabled_list(void) {
    static char disabled_list[256] = "";
    
    if (disabled_list[0] == '\0') {
        #ifdef NO_CORE_LOGICAL
            strcat(disabled_list, "NO_CORE_LOGICAL ");
        #endif
        #ifdef NO_CORE_COMPARISON
            strcat(disabled_list, "NO_CORE_COMPARISON ");
        #endif
        #ifdef NO_CORE_ARITHMETIC
            strcat(disabled_list, "NO_CORE_ARITHMETIC ");
        #endif
        
        if (disabled_list[0] == '\0') {
            strcpy(disabled_list, "none");
        }
    }
    
    return disabled_list;
}

