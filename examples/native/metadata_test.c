/* Example native module with metadata */
#include "../../include/aiscript.h"
#include <stdio.h>

/* Module functions */
static as_value_t* hello(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    (void)userdata;
    printf("Hello from metadata_test module!\n");
    return as_value_null();
}

static as_value_t* get_info(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    (void)userdata;
    return as_value_str("This is a test module with metadata");
}

/* Module initialization */
AS_EXPORT void as_modinit_metadata_test(as_interpreter_t* interp, as_scope_t* module_scope) {
    (void)interp;  /* interp is no longer directly used for global scope */
    
    as_value_t* fn = as_value_fn_native(hello, NULL);
    as_scope_add(module_scope, "hello", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(get_info, NULL);
    as_scope_add(module_scope, "get_info", fn, false);
    as_value_release(fn);
}

/* Module metadata */
static as_module_metadata_t module_meta = {
    .name = "metadata_test",
    .version = "1.2.3",
    .author = "AiScript C Team",
    .description = "Test module demonstrating metadata support",
    .license = "MIT",
    .url = "https://github.com/aiscript/aiscript-c"
};

AS_EXPORT as_module_metadata_t* as_modmeta_metadata_test(void) {
    return &module_meta;
}


