/*
 * AiScript Standard Library - Uri Namespace Implementation
 * 
 * RFC 3986 compliant URI encoding/decoding
 */

#include "std_uri.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef NO_URI

/* Helper: Check if character is unreserved (always safe, never encoded) */
static bool is_unreserved(unsigned char c) {
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

/* Helper: Check if character should NOT be encoded in encodeURI */
static bool is_uri_safe(unsigned char c) {
    /* Unreserved + reserved characters */
    return is_unreserved(c) || 
           strchr(";,/?:@&=+$#!*'()", c) != NULL;
}

/* Helper: Check if character should NOT be encoded in encodeURIComponent */
static bool is_component_safe(unsigned char c) {
    /* Unreserved + ! * ' ( ) */
    return is_unreserved(c) || 
           strchr("!*'()", c) != NULL;
}

/* Helper: Convert hex digit to value */
static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Helper: Check if character should stay encoded in decodeURI */
static bool should_stay_encoded_in_uri(char c) {
    /* Reserved characters that should not be decoded in decodeURI */
    return strchr(";,/?:@&=+$#", c) != NULL;
}

/* Generic encode function */
static char* uri_encode(const char* str, bool is_component) {
    size_t len = strlen(str);
    /* Worst case: every byte becomes %XX (3 bytes) */
    char* result = (char*)as_malloc(len * 3 + 1);
    size_t out_pos = 0;
    
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        
        bool safe = is_component ? is_component_safe(c) : is_uri_safe(c);
        
        if (safe) {
            result[out_pos++] = c;
        } else {
            /* Percent-encode */
            sprintf(&result[out_pos], "%%%02X", c);
            out_pos += 3;
        }
    }
    
    result[out_pos] = '\0';
    return result;
}

/* Generic decode function */
static char* uri_decode(const char* str, bool is_full) {
    size_t len = strlen(str);
    char* result = (char*)as_malloc(len + 1);
    size_t out_pos = 0;
    
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len) {
            int high = hex_to_int(str[i + 1]);
            int low = hex_to_int(str[i + 2]);
            
            if (high >= 0 && low >= 0) {
                char decoded = (char)((high << 4) | low);
                
                /* For decodeURI, keep reserved characters encoded */
                if (is_full && should_stay_encoded_in_uri(decoded)) {
                    result[out_pos++] = str[i];
                    result[out_pos++] = str[i + 1];
                    result[out_pos++] = str[i + 2];
                } else {
                    result[out_pos++] = decoded;
                }
                
                i += 2;
                continue;
            }
        }
        
        result[out_pos++] = str[i];
    }
    
    result[out_pos] = '\0';
    return result;
}

/* Uri:encode_full(uri: str): str */
static as_value_t* std_uri_encode_full(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Uri:encode_full: Expected string argument\n");
        return as_value_str("");
    }
    
    char* encoded = uri_encode(args[0]->data.str_val, false);
    as_value_t* result = as_value_str(encoded);
    as_free(encoded);
    
    return result;
}

/* Uri:encode_component(text: str): str */
static as_value_t* std_uri_encode_component(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Uri:encode_component: Expected string argument\n");
        return as_value_str("");
    }
    
    char* encoded = uri_encode(args[0]->data.str_val, true);
    as_value_t* result = as_value_str(encoded);
    as_free(encoded);
    
    return result;
}

/* Uri:decode_full(encoded_uri: str): str */
static as_value_t* std_uri_decode_full(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Uri:decode_full: Expected string argument\n");
        return as_value_str("");
    }
    
    char* decoded = uri_decode(args[0]->data.str_val, true);
    as_value_t* result = as_value_str(decoded);
    as_free(decoded);
    
    return result;
}

/* Uri:decode_component(encoded_text: str): str */
static as_value_t* std_uri_decode_component(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Uri:decode_component: Expected string argument\n");
        return as_value_str("");
    }
    
    char* decoded = uri_decode(args[0]->data.str_val, false);
    as_value_t* result = as_value_str(decoded);
    as_free(decoded);
    
    return result;
}

/* Register Uri namespace functions */
void as_std_uri_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_uri_encode_full, NULL);
    as_scope_add(global, "Uri:encode_full", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_uri_encode_component, NULL);
    as_scope_add(global, "Uri:encode_component", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_uri_decode_full, NULL);
    as_scope_add(global, "Uri:decode_full", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_uri_decode_component, NULL);
    as_scope_add(global, "Uri:decode_component", fn, false);
    as_value_release(fn);
}

#else /* NO_URI */

void as_std_uri_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Uri namespace is not available in this build (NO_URI)\n");
}

#endif /* NO_URI */

