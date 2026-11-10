/*
 * AiScript Standard Library - Num Namespace Implementation
 */

#include "std_num.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef NO_NUM

/* Num:from_hex(hex: str): num - Parse hexadecimal string */
static as_value_t* std_num_from_hex(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Num:from_hex: Expected string argument\n");
        return as_value_num(NAN);
    }
    
    const char* hex_str = args[0]->data.str_val;
    
    /* Skip whitespace */
    while (isspace((unsigned char)*hex_str)) {
        hex_str++;
    }
    
    /* Handle optional 0x or 0X prefix */
    if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) {
        hex_str += 2;
    }
    
    /* Parse hexadecimal */
    char* endptr;
    long value = strtol(hex_str, &endptr, 16);
    
    /* Check for parsing errors */
    if (endptr == hex_str) {
        /* No digits parsed */
        return as_value_num(NAN);
    }
    
    return as_value_num((double)value);
}

/* Register Num namespace functions */
void as_std_num_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_num_from_hex, NULL);
    as_scope_add(global, "Num:from_hex", fn, false);
    as_value_release(fn);
}

#else /* NO_NUM */

void as_std_num_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Num namespace is not available in this build (NO_NUM)\n");
}

#endif /* NO_NUM */

