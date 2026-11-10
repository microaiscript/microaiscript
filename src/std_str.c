/*
 * AiScript Standard Library - Str Namespace Implementation
 */

#include "std_str.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifndef NO_STR

/* Helper: Encode UTF-8 codepoint */
static size_t encode_utf8(uint32_t codepoint, char* out) {
    if (codepoint <= 0x7F) {
        /* 1 byte: 0xxxxxxx */
        out[0] = (char)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        /* 2 bytes: 110xxxxx 10xxxxxx */
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint <= 0xFFFF) {
        /* 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx */
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        /* 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0; /* Invalid codepoint */
}

static uint32_t str_utf8_next(const unsigned char** p)
{
    unsigned char c = **p;
    if (c < 0x80) {
        *p += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        uint32_t cp = ((c & 0x1F) << 6) | ((*p)[1] & 0x3F);
        *p += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        uint32_t cp = ((c & 0x0F) << 12) | (((*p)[1] & 0x3F) << 6) | ((*p)[2] & 0x3F);
        *p += 3;
        return cp;
    }
    {
        uint32_t cp = ((c & 0x07) << 18) | (((*p)[1] & 0x3F) << 12) | (((*p)[2] & 0x3F) << 6) | ((*p)[3] & 0x3F);
        *p += 4;
        return cp;
    }
}

static int utf16_cmp(const char* a, const char* b)
{
    const unsigned char* pa = (const unsigned char*)(a ? a : "");
    const unsigned char* pb = (const unsigned char*)(b ? b : "");
    for (;;) {
        int enda = (*pa == 0);
        int endb = (*pb == 0);
        uint32_t ca = enda ? 0 : str_utf8_next(&pa);
        uint32_t cb = endb ? 0 : str_utf8_next(&pb);
        uint16_t ua[2], ub[2];
        int na = 1, nb = 1, i;
        if (enda && endb) {
            return 0;
        }
        if (ca >= 0x10000) {
            ua[0] = (uint16_t)(0xD800 + ((ca - 0x10000) >> 10));
            ua[1] = (uint16_t)(0xDC00 + ((ca - 0x10000) & 0x3FF));
            na = 2;
        } else {
            ua[0] = (uint16_t)ca;
        }
        if (cb >= 0x10000) {
            ub[0] = (uint16_t)(0xD800 + ((cb - 0x10000) >> 10));
            ub[1] = (uint16_t)(0xDC00 + ((cb - 0x10000) & 0x3FF));
            nb = 2;
        } else {
            ub[0] = (uint16_t)cb;
        }
        for (i = 0; i < na || i < nb; i++) {
            uint16_t xa = (i < na) ? ua[i] : 0;
            uint16_t xb = (i < nb) ? ub[i] : 0;
            if (enda && i == 0 && !endb) {
                return -1;
            }
            if (endb && i == 0 && !enda) {
                return 1;
            }
            if (xa < xb) {
                return -1;
            }
            if (xa > xb) {
                return 1;
            }
        }
    }
}

/* Str:lt(a, b): num - Compare strings */
static as_value_t* std_str_lt(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_STR || args[1]->type != AS_VALUE_STR) {
        fprintf(stderr, "Str:lt: Expected two string arguments\n");
        return as_value_num(0);
    }
    
    int cmp = utf16_cmp(args[0]->data.str_val, args[1]->data.str_val);
    
    if (cmp < 0) {
        return as_value_num(-1);
    } else if (cmp > 0) {
        return as_value_num(1);
    } else {
        return as_value_num(0);
    }
}

/* Str:gt(a, b): num - Compare strings (reverse) */
static as_value_t* std_str_gt(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_STR || args[1]->type != AS_VALUE_STR) {
        fprintf(stderr, "Str:gt: Expected two string arguments\n");
        return as_value_num(0);
    }
    
    int cmp = utf16_cmp(args[0]->data.str_val, args[1]->data.str_val);
    
    if (cmp > 0) {
        return as_value_num(-1);
    } else if (cmp < 0) {
        return as_value_num(1);
    } else {
        return as_value_num(0);
    }
}

/* Str:from_codepoint(codepoint): str - Create char from codepoint */
static as_value_t* std_str_from_codepoint(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) {
        fprintf(stderr, "Str:from_codepoint: Expected number argument\n");
        return as_value_str("");
    }
    
    uint32_t codepoint = (uint32_t)args[0]->data.num_val;
    
    /* Validate codepoint range */
    if (codepoint > 0x10FFFF) {
        fprintf(stderr, "Str:from_codepoint: Codepoint out of range (max 0x10FFFF)\n");
        return as_value_str("");
    }
    
    char buffer[5] = {0};
    size_t len = encode_utf8(codepoint, buffer);
    
    if (len == 0) {
        return as_value_str("");
    }
    
    return as_value_str(buffer);
}

/* Str:from_unicode_codepoints(codepoints): str - Create string from codepoint array */
static as_value_t* std_str_from_unicode_codepoints(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_ARR) {
        fprintf(stderr, "Str:from_unicode_codepoints: Expected array argument\n");
        return as_value_str("");
    }
    
    as_arr_t* arr = args[0]->data.arr_val;
    
    /* Calculate maximum buffer size (4 bytes per codepoint + null) */
    size_t max_len = arr->length * 4 + 1;
    char* buffer = (char*)as_malloc(max_len);
    size_t pos = 0;
    
    for (size_t i = 0; i < arr->length; i++) {
        if (arr->items[i]->type != AS_VALUE_NUM) {
            fprintf(stderr, "Str:from_unicode_codepoints: Array element %zu is not a number\n", i);
            as_free(buffer);
            return as_value_str("");
        }
        
        uint32_t codepoint = (uint32_t)arr->items[i]->data.num_val;
        
        if (codepoint > 0x10FFFF) {
            fprintf(stderr, "Str:from_unicode_codepoints: Codepoint at index %zu out of range\n", i);
            as_free(buffer);
            return as_value_str("");
        }
        
        pos += encode_utf8(codepoint, &buffer[pos]);
    }
    
    buffer[pos] = '\0';
    
    as_value_t* result = as_value_str(buffer);
    as_free(buffer);
    return result;
}

/* Str:from_utf8_bytes(bytes): str - Create string from UTF-8 bytes */
static as_value_t* std_str_from_utf8_bytes(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_ARR) {
        fprintf(stderr, "Str:from_utf8_bytes: Expected array argument\n");
        return as_value_str("");
    }
    
    as_arr_t* arr = args[0]->data.arr_val;
    
    /* Allocate buffer */
    char* buffer = (char*)as_malloc(arr->length + 1);
    
    for (size_t i = 0; i < arr->length; i++) {
        if (arr->items[i]->type != AS_VALUE_NUM) {
            fprintf(stderr, "Str:from_utf8_bytes: Array element %zu is not a number\n", i);
            as_free(buffer);
            return as_value_str("");
        }
        
        int byte = (int)arr->items[i]->data.num_val;
        
        if (byte < 0 || byte > 255) {
            fprintf(stderr, "Str:from_utf8_bytes: Byte at index %zu out of range (0-255)\n", i);
            as_free(buffer);
            return as_value_str("");
        }
        
        buffer[i] = (char)byte;
    }
    
    buffer[arr->length] = '\0';
    
    as_value_t* result = as_value_str(buffer);
    as_free(buffer);
    return result;
}

/* Register Str namespace functions */
void as_std_str_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    as_value_t* constant;
    
    /* Constants */
    constant = as_value_str("\n");
    as_scope_add(global, "Str:lf", constant, false);
    as_value_release(constant);
    
    /* Functions */
    fn = as_value_fn_native(std_str_lt, NULL);
    as_scope_add(global, "Str:lt", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_str_gt, NULL);
    as_scope_add(global, "Str:gt", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_str_from_codepoint, NULL);
    as_scope_add(global, "Str:from_codepoint", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_str_from_unicode_codepoints, NULL);
    as_scope_add(global, "Str:from_unicode_codepoints", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_str_from_utf8_bytes, NULL);
    as_scope_add(global, "Str:from_utf8_bytes", fn, false);
    as_value_release(fn);
}

#else /* NO_STR */

void as_std_str_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Str namespace is not available in this build (NO_STR)\n");
}

#endif /* NO_STR */

