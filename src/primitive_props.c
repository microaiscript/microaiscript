/*
 * AiScript - Built-in Properties Implementation
 */

#include "primitive_props.h"
#include "memory.h"
#include "error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

#include "interpreter.h"
#include "scope.h"
#include "node.h"
#include "vm.h"
#include <math.h>

/* Forward declaration of eval_node from interpreter.c */
extern as_value_t* eval_node(as_interpreter_t* interp, as_scope_t* scope, as_node_t* node);

/* --- UTF-8 helpers --- */

/* Count UTF-8 codepoints in a string */
static size_t utf8_codepoint_count(const char* str) {
    size_t count = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        if ((*p & 0x80) == 0)       p++;
        else if ((*p & 0xE0) == 0xC0) p += 2;
        else if ((*p & 0xF0) == 0xE0) p += 3;
        else if ((*p & 0xF8) == 0xF0) p += 4;
        else                          p++;
        count++;
    }
    return count;
}

/* Decode next UTF-8 codepoint, advance *p */
static uint32_t utf8_next_cp(const unsigned char** p) {
    uint32_t cp;
    if ((**p & 0x80) == 0) {
        cp = **p; (*p)++;
    } else if ((**p & 0xE0) == 0xC0) {
        cp = (**p & 0x1F) << 6; (*p)++;
        cp |= (**p & 0x3F);     (*p)++;
    } else if ((**p & 0xF0) == 0xE0) {
        cp = (**p & 0x0F) << 12; (*p)++;
        cp |= (**p & 0x3F) << 6; (*p)++;
        cp |= (**p & 0x3F);      (*p)++;
    } else if ((**p & 0xF8) == 0xF0) {
        cp = (**p & 0x07) << 18; (*p)++;
        cp |= (**p & 0x3F) << 12; (*p)++;
        cp |= (**p & 0x3F) << 6;  (*p)++;
        cp |= (**p & 0x3F);       (*p)++;
    } else {
        cp = **p; (*p)++;
    }
    return cp;
}

static int utf8_is_grapheme_extend(uint32_t cp) {
    if (cp >= 0x0300 && cp <= 0x036F) return 1;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return 1;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return 1;
    if (cp >= 0x20D0 && cp <= 0x20FF) return 1;
    if (cp >= 0xFE20 && cp <= 0xFE2F) return 1;
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return 1; /* emoji skin tone */
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 1;
    if (cp >= 0xE0100 && cp <= 0xE01EF) return 1;
    if (cp == 0x20E3) return 1;
    return 0;
}

/* Advance past one extended grapheme cluster (emoji ZWJ / skin-tone / combining). */
static const unsigned char* utf8_grapheme_next(const unsigned char* p) {
    uint32_t cp;
    if (!p || !*p) {
        return p;
    }
    cp = utf8_next_cp(&p);
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) {
        const unsigned char* save = p;
        if (*p) {
            uint32_t n = utf8_next_cp(&p);
            if (!(n >= 0x1F1E6 && n <= 0x1F1FF)) {
                p = save;
            }
        }
        return p;
    }
    for (;;) {
        const unsigned char* save;
        uint32_t n;
        if (!*p) {
            break;
        }
        save = p;
        n = utf8_next_cp(&p);
        if (n == 0x200D) {
            if (*p) {
                utf8_next_cp(&p);
            }
            continue;
        }
        if (utf8_is_grapheme_extend(n)) {
            continue;
        }
        p = save;
        break;
    }
    return p;
}

static size_t utf8_grapheme_count(const char* str) {
    size_t count = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        p = utf8_grapheme_next(p);
        count++;
    }
    return count;
}

static int grapheme_index_to_bytes(const char* str, int gidx) {
    const unsigned char* p = (const unsigned char*)str;
    int i = 0;
    if (gidx <= 0) {
        return 0;
    }
    while (*p && i < gidx) {
        p = utf8_grapheme_next(p);
        i++;
    }
    return (int)((const char*)p - str);
}

static int bytes_to_grapheme_index(const char* str, int byte_off) {
    const unsigned char* p = (const unsigned char*)str;
    int i = 0;
    while (*p && (const char*)p < str + byte_off) {
        p = utf8_grapheme_next(p);
        i++;
    }
    return i;
}

static size_t utf16_unit_count(const char* str) {
    size_t n = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        uint32_t cp = utf8_next_cp(&p);
        n += (cp > 0xFFFF) ? 2 : 1;
    }
    return n;
}

static size_t utf16_index_to_bytes(const char* str, int idx) {
    const unsigned char* p = (const unsigned char*)str;
    int u = 0;
    if (idx <= 0) {
        return 0;
    }
    while (*p && u < idx) {
        uint32_t cp = utf8_next_cp(&p);
        u += (cp > 0xFFFF) ? 2 : 1;
    }
    return (size_t)((const char*)p - str);
}

/* Encode codepoint to UTF-8, writes NUL-terminated to buf (needs 5 bytes), returns byte length */
static int utf8_encode_cp(uint32_t cp, char* buf) {
    if (cp < 0x80) {
        buf[0] = (char)cp; buf[1] = '\0'; return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        buf[2] = '\0'; return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        buf[3] = '\0'; return 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6)  & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        buf[4] = '\0'; return 4;
    }
}

/* Build a flat UTF-16 array from a UTF-8 string; caller must free */
static uint16_t* utf8_to_utf16(const char* str, size_t* out_len) {
    size_t cap = 64;
    uint16_t* buf = as_malloc(cap * sizeof(uint16_t));
    size_t len = 0;
    const unsigned char* p = (const unsigned char*)str;
    while (*p) {
        uint32_t cp = utf8_next_cp(&p);
        if (cp < 0x10000) {
            if (len + 1 >= cap) { cap *= 2; buf = as_realloc(buf, cap * sizeof(uint16_t)); }
            buf[len++] = (uint16_t)cp;
        } else {
            uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
            uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
            if (len + 2 >= cap) { cap *= 2; buf = as_realloc(buf, cap * sizeof(uint16_t)); }
            buf[len++] = (uint16_t)hi;
            buf[len++] = (uint16_t)lo;
        }
    }
    buf[len] = 0;
    *out_len = len;
    return buf;
}

/* Helper to call a user function from native code */
as_value_t* as_call_user_fn(as_interpreter_t* interp, as_value_t* fn, 
                            as_value_t** args, size_t arg_count) {
    if (!fn) {
        return as_value_null();
    }
    if (fn->type == AS_VALUE_ARR) {
        as_vm_t* vm = as_vm_current();
        if (vm) {
            return as_vm_call_value(vm, fn, args, arg_count);
        }
        return as_value_null();
    }
    if (fn->type != AS_VALUE_FN) {
        return as_value_null();
    }
    
    if (fn->data.fn_val->kind == AS_FN_NATIVE) {
        as_value_t* r = fn->data.fn_val->native_fn(args, arg_count, fn->data.fn_val->userdata);
        return r ? r : as_value_null();
    }

    if (!interp) {
        as_vm_t* vm = as_vm_current();
        if (vm) {
            return as_vm_call_value(vm, fn, args, arg_count);
        }
        return as_value_null();
    }
    
    /* Call user function */
    as_scope_t* fn_scope = as_scope_create_child(fn->data.fn_val->closure_scope, "<callback>");
    
    /* Bind parameters */
    for (size_t i = 0; i < fn->data.fn_val->param_count; i++) {
        if (fn->data.fn_val->params[i].dest->type == NODE_IDENTIFIER) {
            as_node_identifier_t* param_id = 
                (as_node_identifier_t*)fn->data.fn_val->params[i].dest;
            
            as_value_t* param_value = NULL;
            
            if (i < arg_count) {
                param_value = as_value_retain(args[i]);
            } else if (fn->data.fn_val->params[i].default_value) {
                param_value = eval_node(interp, fn_scope, fn->data.fn_val->params[i].default_value);
            } else if (fn->data.fn_val->params[i].optional) {
                param_value = as_value_null();
            } else {
                param_value = as_value_null();
            }
            
            as_scope_add(fn_scope, param_id->name, param_value, false);
            as_value_release(param_value);
        }
    }
    
    /* Save and execute function body */
    bool prev_aborted = interp->aborted;
    interp->aborted = false;
    
    as_value_t* result = as_value_null();
    for (size_t i = 0; i < fn->data.fn_val->statement_count; i++) {
        as_value_release(result);
        result = eval_node(interp, fn_scope, fn->data.fn_val->statements[i]);
        
        if (interp->aborted) {
            interp->aborted = false;
            break;
        }
    }
    
    interp->aborted = prev_aborted;
    as_scope_release(fn_scope);
    
    return result;
}

static int value_is_callable(as_value_t* fn) {
    return fn && (fn->type == AS_VALUE_FN || fn->type == AS_VALUE_ARR);
}

/* String properties */

static as_value_t* str_prop_len(as_value_t* target) {
    return as_value_num((double)utf8_grapheme_count(target->data.str_val));
}

static as_value_t* str_method_to_num(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    char* endptr;
    double num = strtod(target->data.str_val, &endptr);
    
    /* If nothing was parsed, return null */
    if (endptr == target->data.str_val) {
        return as_value_null();
    }
    
    return as_value_num(num);
}

static as_value_t* str_method_split(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    const char* str = target->data.str_val;
    
    /* If no separator provided, split into individual characters */
    if (arg_count == 0 || args[0]->type == AS_VALUE_NULL) {
        as_value_t* arr = as_value_arr();
        const unsigned char* p = (const unsigned char*)str;
        while (*p) {
            const unsigned char* start = p;
            char* piece;
            size_t n;
            p = utf8_grapheme_next(p);
            n = (size_t)(p - start);
            piece = as_malloc(n + 1);
            memcpy(piece, start, n);
            piece[n] = '\0';
            {
                as_value_t* item = as_value_str(piece);
                as_arr_push(arr->data.arr_val, item);
                as_value_release(item);
            }
            as_free(piece);
        }
        return arr;
    }
    
    /* Split by separator */
    if (args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Runtime error: split separator must be a string\n");
        return as_value_arr();
    }
    
    const char* sep = args[0]->data.str_val;
    size_t sep_len = strlen(sep);
    
    if (sep_len == 0) {
        /* Empty separator - split into chars */
        size_t len = strlen(str);
        as_value_t* arr = as_value_arr();
        
        for (size_t i = 0; i < len; i++) {
            char ch[2] = {str[i], '\0'};
            as_value_t* item = as_value_str(ch);
            as_arr_push(arr->data.arr_val, item);
            as_value_release(item);
        }
        
        return arr;
    }
    
    /* Split by separator */
    as_value_t* arr = as_value_arr();
    const char* start = str;
    const char* p = str;
    
    while ((p = strstr(start, sep)) != NULL) {
        size_t part_len = p - start;
        char* part = as_malloc(part_len + 1);
        memcpy(part, start, part_len);
        part[part_len] = '\0';
        as_value_t* item = as_value_str(part);
        as_arr_push(arr->data.arr_val, item);
        as_value_release(item);
        as_free(part);
        start = p + sep_len;
    }
    
    /* Add final part */
    as_value_t* item = as_value_str(start);
    as_arr_push(arr->data.arr_val, item);
    as_value_release(item);
    
    return arr;
}

static as_value_t* str_method_upper(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    const char* str = target->data.str_val;
    size_t len = strlen(str);
    char* upper = as_malloc(len + 1);
    
    for (size_t i = 0; i < len; i++) {
        upper[i] = toupper((unsigned char)str[i]);
    }
    upper[len] = '\0';
    
    as_value_t* result = as_value_str(upper);
    as_free(upper);
    return result;
}

static as_value_t* str_method_lower(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    const char* str = target->data.str_val;
    size_t len = strlen(str);
    char* lower = as_malloc(len + 1);
    
    for (size_t i = 0; i < len; i++) {
        lower[i] = tolower((unsigned char)str[i]);
    }
    lower[len] = '\0';
    
    as_value_t* result = as_value_str(lower);
    as_free(lower);
    return result;
}

static as_value_t* str_method_trim(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    
    const char* str = target->data.str_val;
    size_t len = strlen(str);
    
    /* Find start (skip leading whitespace) */
    size_t start = 0;
    while (start < len && (str[start] == ' ' || str[start] == '\t' || 
                           str[start] == '\n' || str[start] == '\r')) {
        start++;
    }
    
    /* Find end (skip trailing whitespace) */
    size_t end = len;
    while (end > start && (str[end-1] == ' ' || str[end-1] == '\t' || 
                          str[end-1] == '\n' || str[end-1] == '\r')) {
        end--;
    }
    
    /* Extract trimmed string */
    size_t trimmed_len = end - start;
    char* result = as_malloc(trimmed_len + 1);
    memcpy(result, str + start, trimmed_len);
    result[trimmed_len] = '\0';
    
    as_value_t* val = as_value_str(result);
    as_free(result);
    return val;
}

static as_value_t* str_method_replace(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_STR || args[1]->type != AS_VALUE_STR) {
        return as_value_str(target->data.str_val);
    }
    
    const char* str = target->data.str_val;
    const char* pattern = args[0]->data.str_val;
    const char* replacement = args[1]->data.str_val;
    
    /* If pattern is empty, return original */
    if (strlen(pattern) == 0) {
        return as_value_str(str);
    }
    
    /* Count occurrences */
    size_t count = 0;
    const char* p = str;
    while ((p = strstr(p, pattern)) != NULL) {
        count++;
        p += strlen(pattern);
    }
    
    /* Calculate result size */
    size_t pattern_len = strlen(pattern);
    size_t replacement_len = strlen(replacement);
    size_t result_len = strlen(str) + count * (replacement_len - pattern_len);
    
    /* Build result */
    char* result = as_malloc(result_len + 1);
    char* dst = result;
    const char* src = str;
    
    while ((p = strstr(src, pattern)) != NULL) {
        /* Copy before pattern */
        size_t len = p - src;
        memcpy(dst, src, len);
        dst += len;
        
        /* Copy replacement */
        memcpy(dst, replacement, replacement_len);
        dst += replacement_len;
        
        /* Move past pattern */
        src = p + pattern_len;
    }
    
    /* Copy remaining */
    strcpy(dst, src);
    
    as_value_t* val = as_value_str(result);
    as_free(result);
    return val;
}

static as_value_t* str_method_slice(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
        return as_value_str(target->data.str_val);
    }
    
    const char* str = target->data.str_val;
    int cp_len = (int)utf8_grapheme_count(str);
    
    int start = (int)args[0]->data.num_val;
    int end   = (int)args[1]->data.num_val;
    
    if (start < 0) start = 0;
    if (end   < 0) end   = 0;
    if (start > cp_len) start = cp_len;
    if (end   > cp_len) end   = cp_len;
    if (end < start) end = start;
    
    /* Walk graphemes; collect bytes from start..end */
    const unsigned char* p = (const unsigned char*)str;
    int cur = 0;
    const unsigned char* begin_ptr = p;
    const unsigned char* end_ptr   = p;
    while (*p) {
        if (cur == start) begin_ptr = p;
        p = utf8_grapheme_next(p);
        cur++;
        if (cur == end) end_ptr = p;
    }
    if (end == cp_len) end_ptr = p;
    if (start == cp_len) begin_ptr = p;
    
    size_t slice_len = (size_t)(end_ptr - begin_ptr);
    char* result = as_malloc(slice_len + 1);
    memcpy(result, begin_ptr, slice_len);
    result[slice_len] = '\0';
    
    as_value_t* val = as_value_str(result);
    as_free(result);
    return val;
}

static as_value_t* str_method_pick(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) {
        return as_value_null();
    }
    
    const char* str = target->data.str_val;
    int index = (int)args[0]->data.num_val;
    
    if (index < 0) {
        return as_value_null();
    }
    
    /* Walk graphemes to find the one at 'index' */
    const unsigned char* p = (const unsigned char*)str;
    int cur = 0;
    while (*p) {
        const unsigned char* start = p;
        size_t n;
        char* buf;
        p = utf8_grapheme_next(p);
        if (cur == index) {
            n = (size_t)(p - start);
            buf = as_malloc(n + 1);
            memcpy(buf, start, n);
            buf[n] = '\0';
            {
                as_value_t* val = as_value_str(buf);
                as_free(buf);
                return val;
            }
        }
        cur++;
    }
    
    return as_value_null();
}

static as_value_t* str_method_incl(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        return as_value_bool(false);
    }
    
    const char* str = target->data.str_val;
    const char* search = args[0]->data.str_val;
    
    return as_value_bool(strstr(str, search) != NULL);
}

static as_value_t* str_method_index_of(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        return as_value_num(-1);
    }
    
    const char* str = target->data.str_val;
    const char* search = args[0]->data.str_val;
    int glen = (int)utf8_grapheme_count(str);
    int from = 0;
    
    if (arg_count >= 2 && args[1]->type == AS_VALUE_NUM) {
        from = (int)args[1]->data.num_val;
        if (from < 0) {
            from = glen + from;
        }
    }
    if (from < 0) {
        from = 0;
    }
    if (from > glen) {
        return as_value_num(-1);
    }
    
    {
        int byte_from = grapheme_index_to_bytes(str, from);
        const char* found = strstr(str + byte_from, search);
        if (found == NULL) {
            return as_value_num(-1);
        }
        return as_value_num((double)bytes_to_grapheme_index(str, (int)(found - str)));
    }
}

static as_value_t* str_method_starts_with(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    const char* str;
    const char* prefix;
    size_t slen;
    size_t plen;
    double raw;
    int index;
    size_t byte_off;
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) return as_value_bool(false);
    str = target->data.str_val;
    prefix = args[0]->data.str_val;
    plen = strlen(prefix);
    if (plen == 0) {
        return as_value_bool(true);
    }
    slen = utf16_unit_count(str);
    raw = 0;
    if (arg_count >= 2 && args[1]->type == AS_VALUE_NUM) {
        raw = args[1]->data.num_val;
    }
    if (raw < -(double)slen || raw > (double)slen) {
        return as_value_bool(false);
    }
    index = (raw >= 0) ? (int)raw : (int)slen + (int)raw;
    byte_off = utf16_index_to_bytes(str, index);
    return as_value_bool(strncmp(str + byte_off, prefix, plen) == 0);
}

static as_value_t* str_method_ends_with(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    const char* str;
    const char* suffix;
    size_t slen;
    size_t suflen;
    double raw;
    int end;
    size_t limit;
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) return as_value_bool(false);
    str = target->data.str_val;
    suffix = args[0]->data.str_val;
    suflen = strlen(suffix);
    if (suflen == 0) {
        return as_value_bool(true);
    }
    slen = utf16_unit_count(str);
    raw = (double)slen;
    if (arg_count >= 2 && args[1]->type == AS_VALUE_NUM) {
        raw = args[1]->data.num_val;
    }
    if (raw < -(double)slen || raw > (double)slen) {
        return as_value_bool(false);
    }
    end = (raw >= 0) ? (int)raw : (int)slen + (int)raw;
    limit = utf16_index_to_bytes(str, end);
    if (suflen > limit) {
        return as_value_bool(false);
    }
    return as_value_bool(strncmp(str + limit - suflen, suffix, suflen) == 0);
}

static as_value_t* str_method_pad_start(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) return as_value_str(target->data.str_val);
    int width = (int)args[0]->data.num_val;
    const char* pad = " ";
    if (arg_count >= 2 && args[1]->type == AS_VALUE_STR && strlen(args[1]->data.str_val) > 0)
        pad = args[1]->data.str_val;
    const char* str = target->data.str_val;
    int cur_len = (int)utf8_codepoint_count(str);
    if (cur_len >= width) return as_value_str(str);
    int pad_cp_len = (int)utf8_codepoint_count(pad);
    int needed = width - cur_len;
    /* Build pad prefix */
    size_t pad_byte_len = strlen(pad);
    size_t str_byte_len = strlen(str);
    size_t result_cap = needed * pad_byte_len + str_byte_len + 1;
    char* result = as_malloc(result_cap);
    char* dst = result;
    int added = 0;
    while (added < needed) {
        int take = (needed - added < pad_cp_len) ? (needed - added) : pad_cp_len;
        /* copy 'take' codepoints from pad */
        const unsigned char* pp = (const unsigned char*)pad;
        for (int t = 0; t < take && *pp; t++) {
            char buf[5];
            uint32_t cp = utf8_next_cp(&pp);
            int b = utf8_encode_cp(cp, buf);
            memcpy(dst, buf, b); dst += b;
        }
        added += take;
    }
    memcpy(dst, str, str_byte_len);
    dst[str_byte_len] = '\0';
    as_value_t* val = as_value_str(result);
    as_free(result);
    return val;
}

static as_value_t* str_method_pad_end(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) return as_value_str(target->data.str_val);
    int width = (int)args[0]->data.num_val;
    const char* pad = " ";
    if (arg_count >= 2 && args[1]->type == AS_VALUE_STR && strlen(args[1]->data.str_val) > 0)
        pad = args[1]->data.str_val;
    const char* str = target->data.str_val;
    int cur_len = (int)utf8_codepoint_count(str);
    if (cur_len >= width) return as_value_str(str);
    int pad_cp_len = (int)utf8_codepoint_count(pad);
    int needed = width - cur_len;
    size_t pad_byte_len = strlen(pad);
    size_t str_byte_len = strlen(str);
    size_t result_cap = str_byte_len + needed * pad_byte_len + 1;
    char* result = as_malloc(result_cap);
    memcpy(result, str, str_byte_len);
    char* dst = result + str_byte_len;
    int added = 0;
    while (added < needed) {
        int take = (needed - added < pad_cp_len) ? (needed - added) : pad_cp_len;
        const unsigned char* pp = (const unsigned char*)pad;
        for (int t = 0; t < take && *pp; t++) {
            char buf[5];
            uint32_t cp = utf8_next_cp(&pp);
            int b = utf8_encode_cp(cp, buf);
            memcpy(dst, buf, b); dst += b;
        }
        added += take;
    }
    *dst = '\0';
    as_value_t* val = as_value_str(result);
    as_free(result);
    return val;
}

static as_value_t* str_method_charcode_at(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) return as_value_num(0);
    size_t idx = (size_t)(int)args[0]->data.num_val;
    size_t utf16_len = 0;
    uint16_t* utf16 = utf8_to_utf16(target->data.str_val, &utf16_len);
    double result = (idx < utf16_len) ? (double)utf16[idx] : 0.0;
    int oob = idx >= utf16_len;
    as_free(utf16);
    if (oob) {
        return as_value_null();
    }
    return as_value_num(result);
}

static as_value_t* str_method_codepoint_at(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) return as_value_num(0);
    size_t idx = (size_t)(int)args[0]->data.num_val;
    size_t utf16_len = 0;
    uint16_t* utf16 = utf8_to_utf16(target->data.str_val, &utf16_len);
    uint32_t cp = 0;
    if (idx < utf16_len) {
        uint16_t hi = utf16[idx];
        if (hi >= 0xD800 && hi <= 0xDBFF && idx + 1 < utf16_len) {
            uint16_t lo = utf16[idx + 1];
            cp = (uint32_t)(((hi - 0xD800u) << 10) | (lo - 0xDC00u)) + 0x10000u;
        } else {
            cp = hi;
        }
    }
    as_free(utf16);
    return as_value_num((double)cp);
}

static as_value_t* str_method_to_arr(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    const unsigned char* p = (const unsigned char*)target->data.str_val;
    as_value_t* arr = as_value_arr();
    while (*p) {
        const unsigned char* start = p;
        size_t n;
        char* buf;
        p = utf8_grapheme_next(p);
        n = (size_t)(p - start);
        buf = as_malloc(n + 1);
        memcpy(buf, start, n);
        buf[n] = '\0';
        {
            as_value_t* item = as_value_str(buf);
            as_arr_push(arr->data.arr_val, item);
            as_value_release(item);
        }
        as_free(buf);
    }
    return arr;
}

static as_value_t* str_method_to_unicode_arr(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    const unsigned char* p = (const unsigned char*)target->data.str_val;
    as_value_t* arr = as_value_arr();
    while (*p) {
        char buf[5];
        uint32_t cp = utf8_next_cp(&p);
        utf8_encode_cp(cp, buf);
        {
            as_value_t* item = as_value_str(buf);
            as_arr_push(arr->data.arr_val, item);
            as_value_release(item);
        }
    }
    return arr;
}

static as_value_t* str_method_to_unicode_codepoint_arr(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    const unsigned char* p = (const unsigned char*)target->data.str_val;
    as_value_t* arr = as_value_arr();
    while (*p) {
        uint32_t cp = utf8_next_cp(&p);
        as_value_t* item = as_value_num((double)cp);
        as_arr_push(arr->data.arr_val, item);
        as_value_release(item);
    }
    return arr;
}

static as_value_t* str_method_to_char_arr(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    size_t utf16_len = 0;
    uint16_t* utf16 = utf8_to_utf16(target->data.str_val, &utf16_len);
    as_value_t* arr = as_value_arr();
    for (size_t i = 0; i < utf16_len; i++) {
        /* Encode each UTF-16 unit back to a one-code-unit string */
        uint32_t cp = utf16[i];
        char buf[5];
        utf8_encode_cp(cp, buf);
        as_value_t* item = as_value_str(buf);
        as_arr_push(arr->data.arr_val, item);
        as_value_release(item);
    }
    as_free(utf16);
    return arr;
}

static as_value_t* str_method_to_charcode_arr(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    size_t utf16_len = 0;
    uint16_t* utf16 = utf8_to_utf16(target->data.str_val, &utf16_len);
    as_value_t* arr = as_value_arr();
    for (size_t i = 0; i < utf16_len; i++) {
        as_value_t* item = as_value_num((double)utf16[i]);
        as_arr_push(arr->data.arr_val, item);
        as_value_release(item);
    }
    as_free(utf16);
    return arr;
}

static as_value_t* str_method_to_utf8_byte_arr(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    const unsigned char* p = (const unsigned char*)target->data.str_val;
    as_value_t* arr = as_value_arr();
    while (*p) {
        as_value_t* item = as_value_num((double)*p);
        as_arr_push(arr->data.arr_val, item);
        as_value_release(item);
        p++;
    }
    return arr;
}

/* Array properties */

static as_value_t* arr_prop_len(as_value_t* target) {
    return as_value_num((double)target->data.arr_val->length);
}

static as_value_t* arr_method_push(as_value_t** args, size_t arg_count, void* userdata) {
    if (arg_count == 0) {
        return as_value_null();
    }
    
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    as_arr_push(arr, args[0]);
    
    return as_value_null();
}

static as_value_t* arr_method_pop(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    if (arr->length == 0) {
        return as_value_null();
    }
    
    as_value_t* item = arr->items[arr->length - 1];
    arr->length--;
    
    /* Return the item without releasing (caller gets ownership) */
    return item;
}

static as_value_t* arr_method_join(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    if (arr->length == 0) {
        return as_value_str("");
    }
    
    /* Get separator (default is empty string) */
    const char* sep = "";
    if (arg_count > 0 && args[0]->type == AS_VALUE_STR) {
        sep = args[0]->data.str_val;
    }
    
    size_t sep_len = strlen(sep);
    
    /* Calculate total length */
    size_t total_len = 0;
    for (size_t i = 0; i < arr->length; i++) {
        if (arr->items[i]->type == AS_VALUE_STR) {
            total_len += strlen(arr->items[i]->data.str_val);
        }
        if (i < arr->length - 1) {
            total_len += sep_len;
        }
    }
    
    /* Build result string */
    char* result = as_malloc(total_len + 1);
    char* p = result;
    
    for (size_t i = 0; i < arr->length; i++) {
        if (arr->items[i]->type == AS_VALUE_STR) {
            const char* str = arr->items[i]->data.str_val;
            size_t len = strlen(str);
            memcpy(p, str, len);
            p += len;
        }
        
        if (i < arr->length - 1) {
            memcpy(p, sep, sep_len);
            p += sep_len;
        }
    }
    *p = '\0';
    
    as_value_t* result_val = as_value_str(result);
    as_free(result);
    return result_val;
}

static as_value_t* arr_method_shift(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    if (arr->length == 0) {
        return as_value_null();
    }
    
    /* Get first item */
    as_value_t* first = arr->items[0];
    
    /* Shift remaining items */
    for (size_t i = 0; i < arr->length - 1; i++) {
        arr->items[i] = arr->items[i + 1];
    }
    arr->length--;
    
    return first;
}

static as_value_t* arr_method_unshift(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    if (arg_count == 0) {
        return as_value_retain(target);
    }
    
    /* Ensure capacity */
    if (arr->length >= arr->capacity) {
        size_t new_capacity = arr->capacity == 0 ? 4 : arr->capacity * 2;
        arr->items = as_realloc(arr->items, new_capacity * sizeof(as_value_t*));
        arr->capacity = new_capacity;
    }
    
    /* Shift existing items right */
    for (size_t i = arr->length; i > 0; i--) {
        arr->items[i] = arr->items[i - 1];
    }
    
    /* Insert new item at beginning */
    arr->items[0] = as_value_retain(args[0]);
    arr->length++;
    
    return as_value_retain(target);
}

static as_value_t* arr_method_concat(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    if (arg_count == 0 || args[0]->type != AS_VALUE_ARR) {
        /* Return copy of original array */
        as_value_t* result = as_value_arr();
        as_arr_t* result_arr = result->data.arr_val;
        for (size_t i = 0; i < arr->length; i++) {
            as_arr_push(result_arr, arr->items[i]);
        }
        return result;
    }
    
    as_arr_t* other = args[0]->data.arr_val;
    
    /* Create new array with combined items */
    as_value_t* result = as_value_arr();
    as_arr_t* result_arr = result->data.arr_val;
    
    /* Copy items from first array */
    for (size_t i = 0; i < arr->length; i++) {
        as_arr_push(result_arr, arr->items[i]);
    }
    
    /* Copy items from second array */
    for (size_t i = 0; i < other->length; i++) {
        as_arr_push(result_arr, other->items[i]);
    }
    
    return result;
}

static as_value_t* arr_method_slice(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
        /* Return copy of original array */
        as_value_t* result = as_value_arr();
        as_arr_t* result_arr = result->data.arr_val;
        for (size_t i = 0; i < arr->length; i++) {
            as_arr_push(result_arr, arr->items[i]);
        }
        return result;
    }
    
    int start = (int)args[0]->data.num_val;
    int end = (int)args[1]->data.num_val;
    
    /* Handle negative indices */
    if (start < 0) start = arr->length + start;
    if (end < 0) end = arr->length + end;
    
    /* Clamp to valid range */
    if (start < 0) start = 0;
    if (start > (int)arr->length) start = arr->length;
    if (end < 0) end = 0;
    if (end > (int)arr->length) end = arr->length;
    if (end < start) end = start;
    
    /* Create new array with sliced items */
    as_value_t* result = as_value_arr();
    as_arr_t* result_arr = result->data.arr_val;
    
    for (int i = start; i < end; i++) {
        as_arr_push(result_arr, arr->items[i]);
    }
    
    return result;
}

static as_value_t* arr_method_incl(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    if (arg_count == 0) {
        return as_value_bool(false);
    }
    
    as_value_t* search = args[0];
    
    /* Simple equality check - compare by value for primitives */
    for (size_t i = 0; i < arr->length; i++) {
        as_value_t* item = arr->items[i];
        
        if (item->type != search->type) continue;
        
        bool equal = false;
        switch (item->type) {
            case AS_VALUE_NULL:
                equal = true;
                break;
            case AS_VALUE_BOOL:
                equal = (item->data.bool_val == search->data.bool_val);
                break;
            case AS_VALUE_NUM:
                equal = (item->data.num_val == search->data.num_val);
                break;
            case AS_VALUE_STR:
                equal = (strcmp(item->data.str_val, search->data.str_val) == 0);
                break;
            default:
                /* For objects/arrays/functions, compare by reference */
                equal = (item == search);
                break;
        }
        
        if (equal) {
            return as_value_bool(true);
        }
    }
    
    return as_value_bool(false);
}

static as_value_t* arr_method_index_of(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    as_value_t* val;
    int start = 0;
    int i;
    int len = (int)arr->length;
    if (arg_count < 1) {
        return as_value_num(-1);
    }
    val = args[0];
    if (arg_count >= 2 && args[1]->type == AS_VALUE_NUM) {
        int from = (int)args[1]->data.num_val;
        if (from < 0) {
            from = len + from;
            if (from < 0) {
                from = 0;
            }
        } else if (from > len) {
            from = len;
        }
        start = from;
    }
    for (i = start; i < len; i++) {
        if (as_value_equals(arr->items[i], val)) {
            return as_value_num((double)i);
        }
    }
    return as_value_num(-1);
}

static as_value_t* arr_method_reverse(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    /* Reverse in place */
    for (size_t i = 0; i < arr->length / 2; i++) {
        size_t j = arr->length - 1 - i;
        as_value_t* temp = arr->items[i];
        arr->items[i] = arr->items[j];
        arr->items[j] = temp;
    }
    
    return as_value_retain(target);
}

static as_value_t* arr_method_copy(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    
    /* Create copy */
    as_value_t* result = as_value_arr();
    as_arr_t* result_arr = result->data.arr_val;
    
    for (size_t i = 0; i < arr->length; i++) {
        as_arr_push(result_arr, arr->items[i]);
    }
    
    return result;
}

/* Helper struct to pass both interpreter and target array to callbacks */
typedef struct {
    as_interpreter_t* interp;
    as_value_t* target;
} arr_fn_context_t;

/* Sort context for comparator-based sort (defined here, used by sort_comparator and arr_method_sort) */
typedef struct {
    as_interpreter_t* interp;
    as_value_t* fn;
} sort_ctx_t;

static sort_ctx_t* g_sort_ctx = NULL;

static int sort_comparator(const void* a, const void* b);  /* forward decl */

static void arr_merge_sort(as_value_t** items, size_t n, as_value_t** tmp)
{
    size_t mid, i, j, k;
    if (n <= 1) {
        return;
    }
    mid = n / 2;
    arr_merge_sort(items, mid, tmp);
    arr_merge_sort(items + mid, n - mid, tmp + mid);
    i = 0;
    j = 0;
    k = 0;
    while (i < mid && j < n - mid) {
        if (sort_comparator(&items[i], &items[mid + j]) <= 0) {
            tmp[k++] = items[i++];
        } else {
            tmp[k++] = items[mid + j++];
        }
    }
    while (i < mid) {
        tmp[k++] = items[i++];
    }
    while (j < n - mid) {
        tmp[k++] = items[mid + j++];
    }
    memcpy(items, tmp, n * sizeof(as_value_t*));
}

/* Comparison function for qsort - compares as_value_t* */
static int value_compare(const void* a, const void* b) {
    as_value_t* val_a = *(as_value_t**)a;
    as_value_t* val_b = *(as_value_t**)b;
    
    /* Handle null values - nulls go to end */
    if (val_a->type == AS_VALUE_NULL && val_b->type == AS_VALUE_NULL) return 0;
    if (val_a->type == AS_VALUE_NULL) return 1;
    if (val_b->type == AS_VALUE_NULL) return -1;
    
    /* If types differ, sort by type order: bool < num < str < others */
    if (val_a->type != val_b->type) {
        return (int)val_a->type - (int)val_b->type;
    }
    
    /* Same type - compare by value */
    switch (val_a->type) {
        case AS_VALUE_BOOL:
            /* false < true */
            return (int)val_a->data.bool_val - (int)val_b->data.bool_val;
            
        case AS_VALUE_NUM: {
            /* Numeric comparison */
            double diff = val_a->data.num_val - val_b->data.num_val;
            if (diff < 0) return -1;
            if (diff > 0) return 1;
            return 0;
        }
        
        case AS_VALUE_STR:
            /* Lexicographic comparison */
            return strcmp(val_a->data.str_val, val_b->data.str_val);
            
        default:
            /* For arrays, objects, functions - keep original order (stable) */
            return 0;
    }
}

static as_value_t* arr_method_sort(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    
    if (arr->length <= 1) return as_value_retain(ctx->target);
    
    if (arg_count > 0 && value_is_callable(args[0])) {
        /* Stable merge sort so equal keys keep original order. */
        sort_ctx_t sc;
        as_value_t** tmp;
        sc.interp = ctx->interp;
        sc.fn = args[0];
        sort_ctx_t* prev = g_sort_ctx;
        g_sort_ctx = &sc;
        tmp = as_malloc(arr->length * sizeof(as_value_t*));
        arr_merge_sort(arr->items, arr->length, tmp);
        as_free(tmp);
        g_sort_ctx = prev;
    } else {
        qsort(arr->items, arr->length, sizeof(as_value_t*), value_compare);
    }
    
    return as_value_retain(ctx->target);
}

static as_value_t* arr_method_map(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    
    /* Check if callback function was provided */
    if (arg_count == 0 || !value_is_callable(args[0])) {
        fprintf(stderr, "Runtime error: arr.map() requires a function argument\n");
        return as_value_null();
    }
    
    as_value_t* fn = args[0];
    as_value_t* result = as_value_arr();
    as_arr_t* result_arr = result->data.arr_val;
    
    /* Call function for each item */
    for (size_t i = 0; i < arr->length; i++) {
        as_value_t* fn_args[2];
        fn_args[0] = arr->items[i];           /* item */
        fn_args[1] = as_value_num((double)i);  /* index */
        
        as_value_t* mapped = as_call_user_fn(ctx->interp, fn, fn_args, 2);
        as_arr_push(result_arr, mapped);
        as_value_release(fn_args[1]);
        as_value_release(mapped);
    }
    
    return result;
}

static as_value_t* arr_method_filter(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    
    /* Check if callback function was provided */
    if (arg_count == 0 || !value_is_callable(args[0])) {
        fprintf(stderr, "Runtime error: arr.filter() requires a function argument\n");
        return as_value_null();
    }
    
    as_value_t* fn = args[0];
    as_value_t* result = as_value_arr();
    as_arr_t* result_arr = result->data.arr_val;
    
    /* Call function for each item, keep if returns true */
    for (size_t i = 0; i < arr->length; i++) {
        as_value_t* fn_args[2];
        fn_args[0] = arr->items[i];           /* item */
        fn_args[1] = as_value_num((double)i);  /* index */
        
        as_value_t* test = as_call_user_fn(ctx->interp, fn, fn_args, 2);
        
        /* Check if result is truthy */
        bool keep = false;
        if (test->type == AS_VALUE_BOOL) {
            keep = test->data.bool_val;
        } else if (test->type == AS_VALUE_NUM) {
            keep = test->data.num_val != 0;
        } else if (test->type != AS_VALUE_NULL) {
            keep = true;  /* Non-null, non-false values are truthy */
        }
        
        if (keep) {
            as_arr_push(result_arr, arr->items[i]);
        }
        
        as_value_release(fn_args[1]);
        as_value_release(test);
    }
    
    return result;
}

static as_value_t* arr_method_reduce(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    
    /* Check if callback function was provided */
    if (arg_count == 0 || !value_is_callable(args[0])) {
        fprintf(stderr, "Runtime error: arr.reduce() requires a function argument\n");
        return as_value_null();
    }
    
    as_value_t* fn = args[0];
    bool has_initial = (arg_count >= 2);
    
    /* Check for empty array without initial value */
    if (!has_initial && arr->length == 0) {
        fprintf(stderr, "Runtime error: Reduce of empty array without initial value\n");
        return as_value_null();
    }
    
    /* Initialize accumulator */
    as_value_t* accumulator = NULL;
    size_t start_index = 0;
    
    if (has_initial) {
        accumulator = as_value_retain(args[1]);
        start_index = 0;
    } else {
        accumulator = as_value_retain(arr->items[0]);
        start_index = 1;
    }
    
    /* Reduce */
    for (size_t i = start_index; i < arr->length; i++) {
        as_value_t* fn_args[3];
        fn_args[0] = accumulator;              /* accumulator */
        fn_args[1] = arr->items[i];            /* current item */
        fn_args[2] = as_value_num((double)i);   /* index */
        
        as_value_t* new_acc = as_call_user_fn(ctx->interp, fn, fn_args, 3);
        as_value_release(accumulator);
        accumulator = new_acc;
        as_value_release(fn_args[2]);
    }
    
    return accumulator;
}

static as_value_t* arr_method_find(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    
    /* Check if callback function was provided */
    if (arg_count == 0 || !value_is_callable(args[0])) {
        fprintf(stderr, "Runtime error: arr.find() requires a function argument\n");
        return as_value_null();
    }
    
    as_value_t* fn = args[0];
    
    /* Call function for each item, return first match */
    for (size_t i = 0; i < arr->length; i++) {
        as_value_t* fn_args[2];
        fn_args[0] = arr->items[i];           /* item */
        fn_args[1] = as_value_num((double)i);  /* index */
        
        as_value_t* test = as_call_user_fn(ctx->interp, fn, fn_args, 2);
        
        /* Check if result is truthy */
        bool found = false;
        if (test->type == AS_VALUE_BOOL) {
            found = test->data.bool_val;
        } else if (test->type == AS_VALUE_NUM) {
            found = test->data.num_val != 0;
        } else if (test->type != AS_VALUE_NULL) {
            found = true;  /* Non-null, non-false values are truthy */
        }
        
        as_value_release(fn_args[1]);
        as_value_release(test);
        
        if (found) {
            return as_value_retain(arr->items[i]);
        }
    }
    
    /* Not found */
    return as_value_null();
}

/* ---- New array methods ---- */

static as_value_t* arr_method_at(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) return as_value_null();
    int idx = (int)args[0]->data.num_val;
    if (idx < 0) idx = (int)arr->length + idx;
    if (idx < 0 || idx >= (int)arr->length) {
        /* Return 'otherwise' if provided */
        if (arg_count >= 2) return as_value_retain(args[1]);
        return as_value_null();
    }
    return as_value_retain(arr->items[idx]);
}

static as_value_t* arr_method_fill(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    as_value_t* fill_val = as_value_null();
    if (arg_count >= 1) fill_val = args[0];
    int from = 0;
    int to   = (int)arr->length;
    if (arg_count >= 2 && args[1]->type == AS_VALUE_NUM) from = (int)args[1]->data.num_val;
    if (arg_count >= 3 && args[2]->type == AS_VALUE_NUM) to   = (int)args[2]->data.num_val;
    if (from < 0) from = (int)arr->length + from;
    if (to   < 0) to   = (int)arr->length + to;
    if (from < 0) from = 0;
    if (to > (int)arr->length) to = (int)arr->length;
    for (int i = from; i < to; i++) {
        as_value_release(arr->items[i]);
        arr->items[i] = as_value_retain(fill_val);
    }
    if (arg_count < 1) as_value_release(fill_val);
    return as_value_retain(target);
}

static as_value_t* arr_method_repeat(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    int times = 0;
    if (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) times = (int)args[0]->data.num_val;
    if (times <= 0) return as_value_arr();
    as_value_t* result = as_value_arr();
    as_arr_t* res = result->data.arr_val;
    for (int t = 0; t < times; t++)
        for (size_t i = 0; i < arr->length; i++)
            as_arr_push(res, arr->items[i]);
    return result;
}

static as_value_t* arr_method_insert(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    if (arg_count < 2 || args[0]->type != AS_VALUE_NUM) return as_value_null();
    int idx = (int)args[0]->data.num_val;
    if (idx < 0) idx = (int)arr->length + idx;
    if (idx < 0) idx = 0;
    if (idx > (int)arr->length) idx = (int)arr->length;
    /* Ensure capacity */
    if (arr->length >= arr->capacity) {
        size_t nc = arr->capacity == 0 ? 4 : arr->capacity * 2;
        arr->items = as_realloc(arr->items, nc * sizeof(as_value_t*));
        arr->capacity = nc;
    }
    for (size_t i = arr->length; i > (size_t)idx; i--)
        arr->items[i] = arr->items[i - 1];
    arr->items[idx] = as_value_retain(args[1]);
    arr->length++;
    return as_value_null();
}

static as_value_t* arr_method_remove(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) return as_value_null();
    int idx = (int)args[0]->data.num_val;
    if (idx < 0) idx = (int)arr->length + idx;
    if (idx < 0 || idx >= (int)arr->length) return as_value_null();
    as_value_t* removed = arr->items[idx];
    for (size_t i = (size_t)idx; i < arr->length - 1; i++)
        arr->items[i] = arr->items[i + 1];
    arr->length--;
    /* Transfer ownership to caller */
    return removed;
}

static as_value_t* arr_method_splice(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    as_arr_t* arr = target->data.arr_val;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM) return as_value_arr();
    int idx = (int)args[0]->data.num_val;
    if (idx < 0) idx = (int)arr->length + idx;
    if (idx < 0) idx = 0;
    if (idx > (int)arr->length) idx = (int)arr->length;
    int del_count = (int)arr->length - idx;
    if (arg_count >= 2 && args[1]->type == AS_VALUE_NUM) {
        del_count = (int)args[1]->data.num_val;
        if (del_count < 0) del_count = 0;
        if (del_count > (int)arr->length - idx) del_count = (int)arr->length - idx;
    }
    size_t ins_count = 0;
    as_value_t** ins_items = NULL;
    if (arg_count >= 3 && args[2] && args[2]->type == AS_VALUE_ARR) {
        ins_count = args[2]->data.arr_val->length;
        ins_items = args[2]->data.arr_val->items;
    }
    /* Build removed array */
    as_value_t* removed = as_value_arr();
    for (int i = 0; i < del_count; i++)
        as_arr_push(removed->data.arr_val, arr->items[idx + i]);
    /* Release removed items' extra refs (as_arr_push retains) */
    for (int i = 0; i < del_count; i++)
        as_value_release(arr->items[idx + i]);
    /* Shift remaining */
    size_t tail = arr->length - idx - del_count;
    if (ins_count != (size_t)del_count) {
        /* Resize if needed */
        size_t new_len = arr->length - del_count + ins_count;
        if (new_len > arr->capacity) {
            while (arr->capacity < new_len) arr->capacity = arr->capacity == 0 ? 4 : arr->capacity * 2;
            arr->items = as_realloc(arr->items, arr->capacity * sizeof(as_value_t*));
        }
        memmove(arr->items + idx + ins_count,
                arr->items + idx + del_count,
                tail * sizeof(as_value_t*));
    }
    for (size_t i = 0; i < ins_count; i++)
        arr->items[idx + i] = as_value_retain(ins_items[i]);
    arr->length = arr->length - del_count + ins_count;
    return removed;
}

/* Recursive helper for flat */
static void arr_flatten(as_arr_t* src, as_arr_t* dst, int depth) {
    for (size_t i = 0; i < src->length; i++) {
        as_value_t* item = src->items[i];
        if (depth > 0 && item->type == AS_VALUE_ARR)
            arr_flatten(item->data.arr_val, dst, depth - 1);
        else
            as_arr_push(dst, item);
    }
}

static as_value_t* arr_method_flat(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* target = (as_value_t*)userdata;
    int depth = 1;
    if (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) depth = (int)args[0]->data.num_val;
    as_value_t* result = as_value_arr();
    arr_flatten(target->data.arr_val, result->data.arr_val, depth);
    return result;
}

/* ---- Higher-order array methods needing interpreter context ---- */

static as_value_t* arr_method_every(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    if (arg_count == 0 || !value_is_callable(args[0])) return as_value_bool(true);
    as_value_t* fn = args[0];
    for (size_t i = 0; i < arr->length; i++) {
        as_value_t* fn_args[2];
        fn_args[0] = arr->items[i];
        fn_args[1] = as_value_num((double)i);
        as_value_t* test = as_call_user_fn(ctx->interp, fn, fn_args, 2);
        bool pass = (test->type == AS_VALUE_BOOL) ? test->data.bool_val
                  : (test->type == AS_VALUE_NUM)  ? test->data.num_val != 0
                  : (test->type != AS_VALUE_NULL);
        as_value_release(fn_args[1]);
        as_value_release(test);
        if (!pass) return as_value_bool(false);
    }
    return as_value_bool(true);
}

static as_value_t* arr_method_some(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    if (arg_count == 0 || !value_is_callable(args[0])) return as_value_bool(false);
    as_value_t* fn = args[0];
    for (size_t i = 0; i < arr->length; i++) {
        as_value_t* fn_args[2];
        fn_args[0] = arr->items[i];
        fn_args[1] = as_value_num((double)i);
        as_value_t* test = as_call_user_fn(ctx->interp, fn, fn_args, 2);
        bool pass = (test->type == AS_VALUE_BOOL) ? test->data.bool_val
                  : (test->type == AS_VALUE_NUM)  ? test->data.num_val != 0
                  : (test->type != AS_VALUE_NULL);
        as_value_release(fn_args[1]);
        as_value_release(test);
        if (pass) return as_value_bool(true);
    }
    return as_value_bool(false);
}

static as_value_t* arr_method_flat_map(as_value_t** args, size_t arg_count, void* userdata) {
    arr_fn_context_t* ctx = (arr_fn_context_t*)userdata;
    as_arr_t* arr = ctx->target->data.arr_val;
    if (arg_count == 0 || !value_is_callable(args[0])) return as_value_arr();
    as_value_t* fn = args[0];
    as_value_t* result = as_value_arr();
    for (size_t i = 0; i < arr->length; i++) {
        as_value_t* fn_args[2];
        fn_args[0] = arr->items[i];
        fn_args[1] = as_value_num((double)i);
        as_value_t* mapped = as_call_user_fn(ctx->interp, fn, fn_args, 2);
        as_value_release(fn_args[1]);
        if (mapped->type == AS_VALUE_ARR) {
            as_arr_t* sub = mapped->data.arr_val;
            for (size_t j = 0; j < sub->length; j++)
                as_arr_push(result->data.arr_val, sub->items[j]);
        } else {
            as_arr_push(result->data.arr_val, mapped);
        }
        as_value_release(mapped);
    }
    return result;
}

static int sort_comparator(const void* a, const void* b) {
    if (!g_sort_ctx) return 0;
    as_value_t* va = *(as_value_t**)a;
    as_value_t* vb = *(as_value_t**)b;
    as_value_t* fn_args[2];
    fn_args[0] = va;
    fn_args[1] = vb;
    as_value_t* res = as_call_user_fn(g_sort_ctx->interp, g_sort_ctx->fn, fn_args, 2);
    int cmp = 0;
    if (res->type == AS_VALUE_NUM) {
        double d = res->data.num_val;
        cmp = (d < 0) ? -1 : (d > 0) ? 1 : 0;
    }
    as_value_release(res);
    return cmp;
}

/* Number properties */

static as_value_t* num_method_to_str(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    
    as_value_t* target = (as_value_t*)userdata;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", target->data.num_val);
    return as_value_str(buf);
}

static void js_double_to_radix16(double value, char* out, size_t out_size)
{
    static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    enum { kBufferSize = 2200 };
    char buffer[kBufferSize];
    int integer_cursor = kBufferSize / 2;
    int fraction_cursor = integer_cursor;
    int negative;
    double integer, fraction, delta;
    int digit;

    if (out_size == 0) {
        return;
    }
    if (value == 0.0) {
        snprintf(out, out_size, "0");
        return;
    }
    if (!isfinite(value)) {
        if (isnan(value)) {
            snprintf(out, out_size, "NaN");
        } else if (value < 0) {
            snprintf(out, out_size, "-Infinity");
        } else {
            snprintf(out, out_size, "Infinity");
        }
        return;
    }

    negative = value < 0;
    if (negative) {
        value = -value;
    }

    integer = floor(value);
    fraction = value - integer;
    delta = 0.5 * (nextafter(value, INFINITY) - value);
    {
        double min_delta = nextafter(0.0, 1.0);
        if (delta < min_delta) {
            delta = min_delta;
        }
    }
    if (fraction >= delta) {
        buffer[fraction_cursor++] = '.';
        do {
            fraction *= 16.0;
            delta *= 16.0;
            digit = (int)fraction;
            buffer[fraction_cursor++] = chars[digit];
            fraction -= digit;
            if (fraction > 0.5 || (fraction == 0.5 && (digit & 1))) {
                if (fraction + delta > 1) {
                    while (1) {
                        fraction_cursor--;
                        if (fraction_cursor == kBufferSize / 2) {
                            integer += 1;
                            break;
                        }
                        {
                            char c = buffer[fraction_cursor];
                            int d = c > '9' ? (c - 'a' + 10) : (c - '0');
                            if (d + 1 < 16) {
                                buffer[fraction_cursor++] = chars[d + 1];
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        } while (fraction >= delta);
    }

    do {
        double remainder = fmod(integer, 16.0);
        int rem;
        if (remainder < 0) {
            remainder += 16.0;
        }
        rem = (int)(remainder + 0.5);
        if (rem >= 16) {
            rem = 0;
        }
        buffer[--integer_cursor] = chars[rem];
        integer = floor((integer - remainder) / 16.0 + 1e-12);
    } while (integer > 0.5);

    if (negative) {
        buffer[--integer_cursor] = '-';
    }
    buffer[fraction_cursor] = '\0';
    snprintf(out, out_size, "%s", buffer + integer_cursor);
}

static as_value_t* num_method_to_hex(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    as_value_t* target = (as_value_t*)userdata;
    char buf[2200];
    js_double_to_radix16(target->data.num_val, buf, sizeof(buf));
    return as_value_str(buf);
}

/* Main property lookup function */

as_value_t* as_get_prim_prop(struct as_interpreter* interp, as_value_t* target, const char* name) {
    switch (target->type) {
        case AS_VALUE_STR:
            /* String properties */
            if (strcmp(name, "len") == 0) {
                return str_prop_len(target);
            }
            /* String methods - wrap in native function */
            if (strcmp(name, "to_num") == 0) {
                return as_value_fn_native(str_method_to_num, as_value_retain(target));
            }
            if (strcmp(name, "split") == 0) {
                return as_value_fn_native(str_method_split, as_value_retain(target));
            }
            if (strcmp(name, "upper") == 0) {
                return as_value_fn_native(str_method_upper, as_value_retain(target));
            }
            if (strcmp(name, "lower") == 0) {
                return as_value_fn_native(str_method_lower, as_value_retain(target));
            }
            if (strcmp(name, "trim") == 0) {
                return as_value_fn_native(str_method_trim, as_value_retain(target));
            }
            if (strcmp(name, "replace") == 0) {
                return as_value_fn_native(str_method_replace, as_value_retain(target));
            }
            if (strcmp(name, "slice") == 0) {
                return as_value_fn_native(str_method_slice, as_value_retain(target));
            }
            if (strcmp(name, "pick") == 0) {
                return as_value_fn_native(str_method_pick, as_value_retain(target));
            }
            if (strcmp(name, "incl") == 0) {
                return as_value_fn_native(str_method_incl, as_value_retain(target));
            }
            if (strcmp(name, "index_of") == 0) {
                return as_value_fn_native(str_method_index_of, as_value_retain(target));
            }
            if (strcmp(name, "starts_with") == 0) {
                return as_value_fn_native(str_method_starts_with, as_value_retain(target));
            }
            if (strcmp(name, "ends_with") == 0) {
                return as_value_fn_native(str_method_ends_with, as_value_retain(target));
            }
            if (strcmp(name, "pad_start") == 0) {
                return as_value_fn_native(str_method_pad_start, as_value_retain(target));
            }
            if (strcmp(name, "pad_end") == 0) {
                return as_value_fn_native(str_method_pad_end, as_value_retain(target));
            }
            if (strcmp(name, "charcode_at") == 0) {
                return as_value_fn_native(str_method_charcode_at, as_value_retain(target));
            }
            if (strcmp(name, "codepoint_at") == 0) {
                return as_value_fn_native(str_method_codepoint_at, as_value_retain(target));
            }
            if (strcmp(name, "to_arr") == 0) {
                return as_value_fn_native(str_method_to_arr, as_value_retain(target));
            }
            if (strcmp(name, "to_unicode_arr") == 0) {
                return as_value_fn_native(str_method_to_unicode_arr, as_value_retain(target));
            }
            if (strcmp(name, "to_unicode_codepoint_arr") == 0) {
                return as_value_fn_native(str_method_to_unicode_codepoint_arr, as_value_retain(target));
            }
            if (strcmp(name, "to_char_arr") == 0) {
                return as_value_fn_native(str_method_to_char_arr, as_value_retain(target));
            }
            if (strcmp(name, "to_charcode_arr") == 0) {
                return as_value_fn_native(str_method_to_charcode_arr, as_value_retain(target));
            }
            if (strcmp(name, "to_utf8_byte_arr") == 0) {
                return as_value_fn_native(str_method_to_utf8_byte_arr, as_value_retain(target));
            }
            break;
            
        case AS_VALUE_ARR:
            /* Array properties */
            if (strcmp(name, "len") == 0) {
                return arr_prop_len(target);
            }
            /* Array methods */
            if (strcmp(name, "push") == 0) {
                return as_value_fn_native(arr_method_push, as_value_retain(target));
            }
            if (strcmp(name, "pop") == 0) {
                return as_value_fn_native(arr_method_pop, as_value_retain(target));
            }
            if (strcmp(name, "shift") == 0) {
                return as_value_fn_native(arr_method_shift, as_value_retain(target));
            }
            if (strcmp(name, "unshift") == 0) {
                return as_value_fn_native(arr_method_unshift, as_value_retain(target));
            }
            if (strcmp(name, "join") == 0) {
                return as_value_fn_native(arr_method_join, as_value_retain(target));
            }
            if (strcmp(name, "concat") == 0) {
                return as_value_fn_native(arr_method_concat, as_value_retain(target));
            }
            if (strcmp(name, "slice") == 0) {
                return as_value_fn_native(arr_method_slice, as_value_retain(target));
            }
            if (strcmp(name, "incl") == 0) {
                return as_value_fn_native(arr_method_incl, as_value_retain(target));
            }
            if (strcmp(name, "index_of") == 0) {
                return as_value_fn_native(arr_method_index_of, as_value_retain(target));
            }
            if (strcmp(name, "reverse") == 0) {
                return as_value_fn_native(arr_method_reverse, as_value_retain(target));
            }
            if (strcmp(name, "copy") == 0) {
                return as_value_fn_native(arr_method_copy, as_value_retain(target));
            }
            if (strcmp(name, "sort") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_sort, ctx);
            }
            /* Function-based array methods - need context with interpreter */
            if (strcmp(name, "map") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_map, ctx);
            }
            if (strcmp(name, "filter") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_filter, ctx);
            }
            if (strcmp(name, "reduce") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_reduce, ctx);
            }
            if (strcmp(name, "find") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_find, ctx);
            }
            /* New array methods that don't need interpreter context */
            if (strcmp(name, "at") == 0) {
                return as_value_fn_native(arr_method_at, as_value_retain(target));
            }
            if (strcmp(name, "fill") == 0) {
                return as_value_fn_native(arr_method_fill, as_value_retain(target));
            }
            if (strcmp(name, "repeat") == 0) {
                return as_value_fn_native(arr_method_repeat, as_value_retain(target));
            }
            if (strcmp(name, "insert") == 0) {
                return as_value_fn_native(arr_method_insert, as_value_retain(target));
            }
            if (strcmp(name, "remove") == 0) {
                return as_value_fn_native(arr_method_remove, as_value_retain(target));
            }
            if (strcmp(name, "splice") == 0) {
                return as_value_fn_native(arr_method_splice, as_value_retain(target));
            }
            if (strcmp(name, "flat") == 0) {
                return as_value_fn_native(arr_method_flat, as_value_retain(target));
            }
            /* Higher-order array methods that need interpreter context */
            if (strcmp(name, "every") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_every, ctx);
            }
            if (strcmp(name, "some") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_some, ctx);
            }
            if (strcmp(name, "flat_map") == 0) {
                arr_fn_context_t* ctx = as_malloc(sizeof(arr_fn_context_t));
                ctx->interp = interp;
                ctx->target = as_value_retain(target);
                return as_value_fn_native(arr_method_flat_map, ctx);
            }
            break;
            
        case AS_VALUE_NUM:
            /* Number methods */
            if (strcmp(name, "to_str") == 0) {
                return as_value_fn_native(num_method_to_str, as_value_retain(target));
            }
            if (strcmp(name, "to_hex") == 0) {
                return as_value_fn_native(num_method_to_hex, as_value_retain(target));
            }
            break;
            
        default:
            break;
    }
    
    /* Property not found: raise a runtime error and let the caller substitute null */
    {
        static const char* const type_names[] = {
            "null", "bool", "num", "str", "arr", "obj", "fn", "error"
        };
        char message[256];
        snprintf(message, sizeof(message), "No such property '%s' for type %s", name,
                 (unsigned)target->type < 8 ? type_names[target->type] : "unknown");
        as_raise_runtime_error(message);
    }
    return NULL;
}

