/*
 * AiScript - Value implementation
 */

#include "types.h"
#include "memory.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Hash function for object keys */
static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/* Create value constructors */
as_value_t* as_value_null(void) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_NULL;
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_bool(bool value) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_BOOL;
    v->data.bool_val = value;
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_num(double value) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_NUM;
    v->data.num_val = value;
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_str(const char* value) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_STR;
    v->data.str_val = as_strdup(value);
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_arr(void) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_ARR;
    v->data.arr_val = as_calloc(1, sizeof(as_arr_t));
    v->data.arr_val->items = NULL;
    v->data.arr_val->length = 0;
    v->data.arr_val->capacity = 0;
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_obj(void) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_OBJ;
    v->data.obj_val = as_calloc(1, sizeof(as_obj_t));
    v->data.obj_val->bucket_count = 16;
    v->data.obj_val->buckets = as_calloc(16, sizeof(as_obj_entry_t*));
    v->data.obj_val->size = 0;
    v->data.obj_val->order_head = NULL;
    v->data.obj_val->order_tail = NULL;
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_fn_user(as_fn_param_t* params, size_t param_count,
                               as_node_t** statements, size_t statement_count,
                               as_scope_t* scope) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_FN;
    v->data.fn_val = as_calloc(1, sizeof(as_fn_t));
    v->data.fn_val->kind = AS_FN_USER;
    v->data.fn_val->params = params;
    v->data.fn_val->param_count = param_count;
    v->data.fn_val->statements = statements;
    v->data.fn_val->statement_count = statement_count;
    v->data.fn_val->closure_scope = scope;
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_fn_native(as_native_fn_t fn, void* userdata) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_FN;
    v->data.fn_val = as_calloc(1, sizeof(as_fn_t));
    v->data.fn_val->kind = AS_FN_NATIVE;
    v->data.fn_val->native_fn = fn;
    v->data.fn_val->userdata = userdata;
    v->ref_count = 1;
    return v;
}

as_value_t* as_value_error(const char* name, as_value_t* info) {
    as_value_t* v = as_calloc(1, sizeof(as_value_t));
    v->type = AS_VALUE_ERROR;
    v->data.error_val.name = as_strdup(name);
    v->data.error_val.info = info ? as_value_retain(info) : NULL;
    v->ref_count = 1;
    return v;
}

/* Reference counting */
as_value_t* as_value_retain(as_value_t* value) {
    if (value) {
        value->ref_count++;
    }
    return value;
}

void as_value_release(as_value_t* value) {
    if (!value) return;
    
    value->ref_count--;
    if (value->ref_count > 0) return;
    
    /* Free internal data */
    switch (value->type) {
        case AS_VALUE_STR:
            as_free(value->data.str_val);
            break;
            
        case AS_VALUE_ARR: {
            as_arr_t* arr = value->data.arr_val;
            for (size_t i = 0; i < arr->length; i++) {
                as_value_release(arr->items[i]);
            }
            as_free(arr->items);
            as_free(arr);
            break;
        }
            
        case AS_VALUE_OBJ: {
            as_obj_t* obj = value->data.obj_val;
            for (size_t i = 0; i < obj->bucket_count; i++) {
                as_obj_entry_t* entry = obj->buckets[i];
                while (entry) {
                    as_obj_entry_t* next = entry->next;
                    as_free(entry->key);
                    as_value_release(entry->value);
                    as_free(entry);
                    entry = next;
                }
            }
            as_free(obj->buckets);
            as_free(obj);
            break;
        }
            
        case AS_VALUE_FN:
            if (value->data.fn_val->name) {
                as_free(value->data.fn_val->name);
            }
            /* Note: We don't free params, statements, or scope here 
             * as they are managed separately by the interpreter */
            as_free(value->data.fn_val);
            break;
            
        case AS_VALUE_ERROR:
            as_free(value->data.error_val.name);
            if (value->data.error_val.info) {
                as_value_release(value->data.error_val.info);
            }
            break;
            
        default:
            break;
    }
    
    as_free(value);
}

/* Value operations */
bool as_value_equals(as_value_t* a, as_value_t* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    
    switch (a->type) {
        case AS_VALUE_NULL:
            return true;
        case AS_VALUE_BOOL:
            return a->data.bool_val == b->data.bool_val;
        case AS_VALUE_NUM:
            return a->data.num_val == b->data.num_val;
        case AS_VALUE_STR:
            return strcmp(a->data.str_val, b->data.str_val) == 0;
        case AS_VALUE_FN:
            if (a->data.fn_val->kind == AS_FN_NATIVE && b->data.fn_val->kind == AS_FN_NATIVE) {
                return a->data.fn_val->native_fn == b->data.fn_val->native_fn
                    && a->data.fn_val->userdata == b->data.fn_val->userdata;
            }
            return a->data.fn_val == b->data.fn_val;
        default:
            return false; /* Arrays, objects compared by reference (a == b above) */
    }
}

bool as_value_to_bool(as_value_t* value) {
    if (!value) return false;
    
    switch (value->type) {
        case AS_VALUE_NULL:
            return false;
        case AS_VALUE_BOOL:
            return value->data.bool_val;
        case AS_VALUE_NUM:
            return value->data.num_val != 0.0 && !isnan(value->data.num_val);
        case AS_VALUE_STR:
            return value->data.str_val[0] != '\0';
        case AS_VALUE_ARR:
            return value->data.arr_val->length > 0;
        case AS_VALUE_OBJ:
            return value->data.obj_val->size > 0;
        default:
            return true;
    }
}

/* Format a number the way AiScript (JavaScript) prints it: NaN, Infinity and
 * -Infinity by name, -0 as 0, and otherwise the fewest significant digits
 * (up to 17) that read back as the same double. */
void as_num_to_str(double n, char* out, size_t out_size) {
    if (isnan(n)) {
        snprintf(out, out_size, "NaN");
    } else if (isinf(n)) {
        snprintf(out, out_size, n < 0 ? "-Infinity" : "Infinity");
    } else if (n == 0.0) {
        snprintf(out, out_size, "0");
    } else {
        for (int precision = 15; precision <= 17; precision++) {
            snprintf(out, out_size, "%.*g", precision, n);
            if (strtod(out, NULL) == n) break;
        }
    }
}

/* Nesting limit when displaying arrays/objects; deeper (or cyclic) values
 * are shown as "...", like upstream's reprValue does for repeated objects. */
#define AS_DISPLAY_MAX_DEPTH 32
static int g_display_depth = 0;

char* as_value_to_string(as_value_t* value) {
    if (!value) return as_strdup("null");
    if (g_display_depth >= AS_DISPLAY_MAX_DEPTH && (value->type == AS_VALUE_ARR || value->type == AS_VALUE_OBJ)) {
        return as_strdup("...");
    }
    
    char buffer[256];
    
    switch (value->type) {
        case AS_VALUE_NULL:
            return as_strdup("null");  /* Note: TypeScript uses (null) in arrays, but null elsewhere */
        case AS_VALUE_BOOL:
            return as_strdup(value->data.bool_val ? "true" : "false");
        case AS_VALUE_NUM:
            as_num_to_str(value->data.num_val, buffer, sizeof(buffer));
            return as_strdup(buffer);
        case AS_VALUE_STR: {
            /* Format string with quotes for display */
            size_t len = strlen(value->data.str_val);
            char* result = as_malloc(len + 3);  /* +2 for quotes, +1 for null */
            result[0] = '"';
            memcpy(result + 1, value->data.str_val, len);
            result[len + 1] = '"';
            result[len + 2] = '\0';
            return result;
        }
        case AS_VALUE_ARR: {
            /* Format array as [item1, item2, ...] */
            as_arr_t* arr = value->data.arr_val;
            if (arr->length == 0) {
                return as_strdup("[]");
            }
            
            /* Build array string */
            char* result = as_strdup("[");
            g_display_depth++;
            for (size_t i = 0; i < arr->length; i++) {
                /* Handle null specially in arrays - wrap in parentheses */
                char* item_str;
                if (arr->items[i]->type == AS_VALUE_NULL) {
                    item_str = as_strdup("(null)");
                } else {
                    item_str = as_value_to_string(arr->items[i]);
                }
                
                /* Append item */
                size_t old_len = strlen(result);
                size_t item_len = strlen(item_str);
                result = as_realloc(result, old_len + item_len + 3);  /* +2 for ", ", +1 for null */
                memcpy(result + old_len, item_str, item_len);
                
                if (i < arr->length - 1) {
                    result[old_len + item_len] = ',';
                    result[old_len + item_len + 1] = ' ';
                    result[old_len + item_len + 2] = '\0';
                } else {
                    result[old_len + item_len] = '\0';
                }
                
                as_free(item_str);
            }
            
            g_display_depth--;

            /* Append closing bracket */
            size_t len = strlen(result);
            result = as_realloc(result, len + 2);
            result[len] = ']';
            result[len + 1] = '\0';
            
            return result;
        }
        case AS_VALUE_OBJ: {
            /* Format object as { key: value, ... } in insertion order */
            as_obj_t* obj = value->data.obj_val;
            if (obj->size == 0) {
                return as_strdup("{}");
            }
            char* result = as_strdup("{ ");
            size_t result_len = 2;
            g_display_depth++;
            for (as_obj_entry_t* e = obj->order_head; e; e = e->order_next) {
                char* item_str = as_value_to_string(e->value);
                size_t key_len = strlen(e->key);
                size_t item_len = strlen(item_str);
                result = as_realloc(result, result_len + key_len + item_len + 5);
                memcpy(result + result_len, e->key, key_len);
                result_len += key_len;
                result[result_len++] = ':';
                result[result_len++] = ' ';
                memcpy(result + result_len, item_str, item_len);
                result_len += item_len;
                if (e->order_next) {
                    result[result_len++] = ',';
                    result[result_len++] = ' ';
                }
                result[result_len] = '\0';
                as_free(item_str);
            }
            g_display_depth--;
            result = as_realloc(result, result_len + 3);
            memcpy(result + result_len, " }", 3);
            return result;
        }
        case AS_VALUE_FN:
            return as_strdup("[function]");
        case AS_VALUE_ERROR:
            snprintf(buffer, sizeof(buffer), "Error: %s", value->data.error_val.name);
            return as_strdup(buffer);
        default:
            return as_strdup("[unknown]");
    }
}

/* Array operations */
void as_arr_push(as_arr_t* arr, as_value_t* value) {
    if (arr->length >= arr->capacity) {
        size_t new_capacity = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->items = as_realloc(arr->items, new_capacity * sizeof(as_value_t*));
        arr->capacity = new_capacity;
    }
    arr->items[arr->length++] = as_value_retain(value);
}

as_value_t* as_arr_get(as_arr_t* arr, size_t index) {
    if (index >= arr->length) return NULL;
    return arr->items[index];
}

void as_arr_set(as_arr_t* arr, size_t index, as_value_t* value) {
    if (index >= arr->length) return;
    as_value_release(arr->items[index]);
    arr->items[index] = as_value_retain(value);
}

/* Object operations */
void as_obj_set(as_obj_t* obj, const char* key, as_value_t* value) {
    uint32_t hash = hash_string(key);
    size_t bucket_idx = hash % obj->bucket_count;
    
    /* Check if key already exists */
    as_obj_entry_t* entry = obj->buckets[bucket_idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            as_value_release(entry->value);
            entry->value = as_value_retain(value);
            return;
        }
        entry = entry->next;
    }
    
    /* Add new entry */
    as_obj_entry_t* new_entry = as_malloc(sizeof(as_obj_entry_t));
    new_entry->key = as_strdup(key);
    new_entry->value = as_value_retain(value);
    new_entry->next = obj->buckets[bucket_idx];
    new_entry->order_next = NULL;
    new_entry->order_prev = obj->order_tail;
    if (obj->order_tail) {
        obj->order_tail->order_next = new_entry;
    } else {
        obj->order_head = new_entry;
    }
    obj->order_tail = new_entry;
    obj->buckets[bucket_idx] = new_entry;
    obj->size++;
}

as_value_t* as_obj_get(as_obj_t* obj, const char* key) {
    uint32_t hash = hash_string(key);
    size_t bucket_idx = hash % obj->bucket_count;
    
    as_obj_entry_t* entry = obj->buckets[bucket_idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

bool as_obj_has(as_obj_t* obj, const char* key) {
    return as_obj_get(obj, key) != NULL;
}

void as_obj_delete(as_obj_t* obj, const char* key) {
    uint32_t hash = hash_string(key);
    size_t bucket_idx = hash % obj->bucket_count;
    
    as_obj_entry_t** entry_ptr = &obj->buckets[bucket_idx];
    while (*entry_ptr) {
        as_obj_entry_t* entry = *entry_ptr;
        if (strcmp(entry->key, key) == 0) {
            *entry_ptr = entry->next;
            if (entry->order_prev) {
                entry->order_prev->order_next = entry->order_next;
            } else {
                obj->order_head = entry->order_next;
            }
            if (entry->order_next) {
                entry->order_next->order_prev = entry->order_prev;
            } else {
                obj->order_tail = entry->order_prev;
            }
            as_free(entry->key);
            as_value_release(entry->value);
            as_free(entry);
            obj->size--;
            return;
        }
        entry_ptr = &entry->next;
    }
}



/* Helper functions for native modules to access value data */
as_value_type_t as_value_get_type(as_value_t* value) {
    return value ? value->type : AS_VALUE_NULL;
}

double as_value_get_num(as_value_t* value) {
    return (value && value->type == AS_VALUE_NUM) ? value->data.num_val : 0.0;
}

const char* as_value_get_str(as_value_t* value) {
    return (value && value->type == AS_VALUE_STR) ? value->data.str_val : "";
}

bool as_value_get_bool(as_value_t* value) {
    return (value && value->type == AS_VALUE_BOOL) ? value->data.bool_val : false;
}
