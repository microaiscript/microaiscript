/*
 * AiScript Standard Library - Error Namespace Implementation
 */

#include "std_error.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include "traceback.h"
#include <stdio.h>
#include <string.h>

#ifndef NO_ERROR

/* Error:create(name: str, info?: any): error - Create error value */
static as_value_t* std_error_create(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Error:create: Expected string as first argument\n");
        return as_value_error("invalid_argument", as_value_null());
    }
    
    const char* name = args[0]->data.str_val;
    
    /* Get optional info parameter */
    as_value_t* info = (arg_count >= 2) ? as_value_retain(args[1]) : as_value_null();
    
    /* Report error with Pythonic traceback */
    #ifndef NO_PYTHONIC_ERRORS
    const char* info_str = (info->type == AS_VALUE_STR) ? info->data.str_val : name;
    
    /* Map error types to Pythonic equivalents */
    as_error_type_t error_type = AS_ERROR_RUNTIME;
    if (strcmp(name, "NameError") == 0) {
        error_type = (as_error_type_t)AS_ERROR_PYTHONIC_NAME;
    } else if (strcmp(name, "TypeError") == 0) {
        error_type = AS_ERROR_TYPE;
    } else if (strcmp(name, "IndexError") == 0) {
        error_type = (as_error_type_t)AS_ERROR_PYTHONIC_INDEX;
    } else if (strcmp(name, "AttributeError") == 0) {
        error_type = (as_error_type_t)AS_ERROR_PYTHONIC_ATTRIBUTE;
    } else if (strcmp(name, "ImportError") == 0) {
        error_type = (as_error_type_t)AS_ERROR_PYTHONIC_IMPORT;
    } else if (strcmp(name, "OSError") == 0) {
        error_type = (as_error_type_t)AS_ERROR_PYTHONIC_OS;
    }
    
    as_error_report(error_type, info_str);
    #else
    fprintf(stderr, "Error: %s\n", name);
    #endif
    
    return as_value_error(name, info);
}

/* Register Error namespace functions */
void as_std_error_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_error_create, NULL);
    as_scope_add(global, "Error:create", fn, false);
    as_value_release(fn);
}

#else /* NO_ERROR */

void as_std_error_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Error namespace is not available in this build (NO_ERROR)\n");
}

#endif /* NO_ERROR */

