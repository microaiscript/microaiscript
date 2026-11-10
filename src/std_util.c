/*
 * AiScript Standard Library - Util Namespace Implementation
 */

#include "std_util.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef NO_UTIL

/* Initialize random for UUID generation */
static bool uuid_rng_initialized = false;

static void init_uuid_rng(void) {
    if (!uuid_rng_initialized) {
        srand((unsigned int)time(NULL));
        uuid_rng_initialized = true;
    }
}

/* Generate random hex character (0-9, a-f) */
static char random_hex_char(void) {
    static const char hex[] = "0123456789abcdef";
    return hex[rand() % 16];
}

/* Util:uuid(): str - Generate UUID v4 */
static as_value_t* std_util_uuid(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    (void)userdata;
    
    init_uuid_rng();
    
    /* UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx */
    /* Where:
     * - x is any hex digit
     * - 4 is the version (UUID v4)
     * - y is one of 8, 9, a, or b (variant bits)
     */
    
    char uuid[37];  /* 36 chars + null terminator */
    int pos = 0;
    
    /* Generate 8 hex digits */
    for (int i = 0; i < 8; i++) {
        uuid[pos++] = random_hex_char();
    }
    uuid[pos++] = '-';
    
    /* Generate 4 hex digits */
    for (int i = 0; i < 4; i++) {
        uuid[pos++] = random_hex_char();
    }
    uuid[pos++] = '-';
    
    /* Generate 4xxx (version 4) */
    uuid[pos++] = '4';
    for (int i = 0; i < 3; i++) {
        uuid[pos++] = random_hex_char();
    }
    uuid[pos++] = '-';
    
    /* Generate yxxx (variant bits) */
    /* y must be one of 8, 9, a, b (bits 10xx) */
    static const char variant[] = "89ab";
    uuid[pos++] = variant[rand() % 4];
    for (int i = 0; i < 3; i++) {
        uuid[pos++] = random_hex_char();
    }
    uuid[pos++] = '-';
    
    /* Generate 12 hex digits */
    for (int i = 0; i < 12; i++) {
        uuid[pos++] = random_hex_char();
    }
    
    uuid[pos] = '\0';
    
    return as_value_str(uuid);
}

/* Register Util namespace functions */
void as_std_util_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_util_uuid, NULL);
    as_scope_add(global, "Util:uuid", fn, false);
    as_value_release(fn);
}

#else /* NO_UTIL */

void as_std_util_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Util namespace is not available in this build (NO_UTIL)\n");
}

#endif /* NO_UTIL */

