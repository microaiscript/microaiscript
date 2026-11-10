/*
 * Tiny native module for ais-pack demo.
 * Build: make -C examples/ais-pack-demo native
 */

#include <aiscript.h>
#include <stdio.h>

static as_value_t *say_hi(as_value_t **args, size_t count, void *userdata) {
    (void)args;
    (void)count;
    (void)userdata;
    printf("hi from demo_native\n");
    return as_value_null();
}

AS_EXPORT void as_modinit_demo_native(as_interpreter_t *interp, as_scope_t *module_scope) {
    (void)interp;
    as_value_t *fn = as_value_fn_native(say_hi, NULL);
    as_scope_add(module_scope, "say_hi", fn, false);
    as_value_release(fn);
}
