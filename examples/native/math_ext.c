/*
 * Math extension native module
 * Compile: gcc -shared -fPIC -o math_ext.so math_ext.c -I../../include
 */

#include <aiscript.h>

/* Factorial function */
static as_value_t* factorial(as_value_t** args, size_t count, void* userdata) {
    (void)userdata;
    
    if (count < 1 || as_value_get_type(args[0]) != AS_VALUE_NUM) {
        return as_value_null();
    }
    
    int n = (int)as_value_get_num(args[0]);
    if (n < 0) return as_value_null();
    
    long result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    
    return as_value_num((double)result);
}

/* Fibonacci function */
static as_value_t* fibonacci(as_value_t** args, size_t count, void* userdata) {
    (void)userdata;
    
    if (count < 1 || as_value_get_type(args[0]) != AS_VALUE_NUM) {
        return as_value_null();
    }
    
    int n = (int)as_value_get_num(args[0]);
    if (n < 0) return as_value_null();
    if (n <= 1) return as_value_num(n);
    
    long a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        long temp = a + b;
        a = b;
        b = temp;
    }
    
    return as_value_num((double)b);
}

/* NEW: Now takes module_scope parameter */
AS_EXPORT void as_modinit_math_ext(as_interpreter_t* interp, as_scope_t* module_scope) {
    (void)interp;  /* Unused */
    
    as_value_t* fn1 = as_value_fn_native(factorial, NULL);
    as_scope_add(module_scope, "factorial", fn1, false);
    as_value_release(fn1);
    
    as_value_t* fn2 = as_value_fn_native(fibonacci, NULL);
    as_scope_add(module_scope, "fibonacci", fn2, false);
    as_value_release(fn2);
}
