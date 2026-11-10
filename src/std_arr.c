/*
 * AiScript Standard Library - Arr Namespace Implementation
 */

#include "std_arr.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <math.h>

#ifndef NO_ARR

/* Arr:create(length, initial?): arr - Create array of specified length */
static as_value_t* std_arr_create(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    /* Get length parameter */
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) {
        fprintf(stderr, "Arr:create: Expected number as first argument\n");
        return as_value_null();
    }
    
    double length_d = args[0]->data.num_val;
    
    /* Validate length */
    if (length_d < 0) {
        fprintf(stderr, "Arr:create: Expected non-negative number, got negative\n");
        return as_value_null();
    }
    
    if (!isfinite(length_d)) {
        fprintf(stderr, "Arr:create: Expected finite number\n");
        return as_value_null();
    }
    
    if (floor(length_d) != length_d) {
        fprintf(stderr, "Arr:create: Expected integer, got non-integer\n");
        return as_value_null();
    }
    
    size_t length = (size_t)length_d;
    
    /* Get initial value (optional, default: null) */
    as_value_t* initial = (arg_count >= 2) ? args[1] : as_value_null();
    
    /* Create array */
    as_value_t* arr = as_value_arr();
    
    /* Fill array with initial value */
    for (size_t i = 0; i < length; i++) {
        as_arr_push(arr->data.arr_val, as_value_retain(initial));
    }
    
    /* Release initial if we created it */
    if (arg_count < 2) {
        as_value_release(initial);
    }
    
    return arr;
}

/* Register Arr namespace functions */
void as_std_arr_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_arr_create, NULL);
    as_scope_add(global, "Arr:create", fn, false);
    as_value_release(fn);
}

#else /* NO_ARR */

void as_std_arr_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Arr namespace is not available in this build (NO_ARR)\n");
}

#endif /* NO_ARR */

