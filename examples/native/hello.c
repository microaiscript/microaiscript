/*
 * Example native AiScript module
 * Compile: gcc -shared -fPIC -o hello.so hello.c -I../../include
 */

#include <aiscript.h>
#include <stdio.h>

/* Example function: say_hello() */
static as_value_t* say_hello(as_value_t** args, size_t count, void* userdata) {
    (void)args;
    (void)count;
    (void)userdata;
    
    printf("Hello from native module!\n");
    return as_value_null();
}

/* Example function: greet(name) */
static as_value_t* greet(as_value_t** args, size_t count, void* userdata) {
    (void)userdata;
    
    if (count < 1 || as_value_get_type(args[0]) != AS_VALUE_STR) {
        printf("Hello, stranger!\n");
    } else {
        printf("Hello, %s!\n", as_value_get_str(args[0]));
    }
    
    return as_value_null();
}

/* Module initialization function: as_modinit_<module_name> */
/* NEW: Now takes module_scope parameter instead of using global scope */
AS_EXPORT void as_modinit_hello(as_interpreter_t* interp, as_scope_t* module_scope) {
    (void)interp;  /* Unused */
    
    /* Register say_hello function in module scope */
    as_value_t* fn1 = as_value_fn_native(say_hello, NULL);
    as_scope_add(module_scope, "say_hello", fn1, false);
    as_value_release(fn1);
    
    /* Register greet function in module scope */
    as_value_t* fn2 = as_value_fn_native(greet, NULL);
    as_scope_add(module_scope, "greet", fn2, false);
    as_value_release(fn2);
}
