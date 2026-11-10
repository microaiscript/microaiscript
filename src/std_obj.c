/*
 * AiScript Standard Library - Obj Namespace Implementation
 */

#include "std_obj.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

#ifndef NO_OBJ

/* Obj:keys(obj): arr - Get array of object keys */
static as_value_t* std_obj_keys(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        fprintf(stderr, "Obj:keys: Expected object argument\n");
        return as_value_arr();
    }
    
    as_obj_t* obj = args[0]->data.obj_val;
    as_value_t* result = as_value_arr();
    
    for (as_obj_entry_t* entry = obj->order_head; entry != NULL; entry = entry->order_next) {
        as_value_t* key_str = as_value_str(entry->key);
        as_arr_push(result->data.arr_val, key_str);
        as_value_release(key_str);
    }
    
    return result;
}

/* Obj:vals(obj): arr - Get array of object values */
static as_value_t* std_obj_vals(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        fprintf(stderr, "Obj:vals: Expected object argument\n");
        return as_value_arr();
    }
    
    as_obj_t* obj = args[0]->data.obj_val;
    as_value_t* result = as_value_arr();
    
    for (as_obj_entry_t* entry = obj->order_head; entry != NULL; entry = entry->order_next) {
        as_arr_push(result->data.arr_val, as_value_retain(entry->value));
    }
    
    return result;
}

/* Obj:kvs(obj): arr - Get array of [key, value] pairs */
static as_value_t* std_obj_kvs(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        fprintf(stderr, "Obj:kvs: Expected object argument\n");
        return as_value_arr();
    }
    
    as_obj_t* obj = args[0]->data.obj_val;
    as_value_t* result = as_value_arr();
    
    for (as_obj_entry_t* entry = obj->order_head; entry != NULL; entry = entry->order_next) {
        as_value_t* pair = as_value_arr();
        as_arr_push(pair->data.arr_val, as_value_str(entry->key));
        as_arr_push(pair->data.arr_val, as_value_retain(entry->value));
        as_arr_push(result->data.arr_val, pair);
        as_value_release(pair);
    }
    
    return result;
}

/* Obj:get(obj, key): any - Get value by key */
static as_value_t* std_obj_get(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_OBJ || args[1]->type != AS_VALUE_STR) {
        fprintf(stderr, "Obj:get: Expected object and string arguments\n");
        return as_value_null();
    }
    
    as_obj_t* obj = args[0]->data.obj_val;
    const char* key = args[1]->data.str_val;
    
    as_value_t* value = as_obj_get(obj, key);
    
    if (value) {
        return as_value_retain(value);
    } else {
        return as_value_null();
    }
}

/* Obj:set(obj, key, value): null - Set key to value */
static as_value_t* std_obj_set(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 3 || args[0]->type != AS_VALUE_OBJ || args[1]->type != AS_VALUE_STR) {
        fprintf(stderr, "Obj:set: Expected object, string, and value arguments\n");
        return as_value_null();
    }
    
    as_obj_t* obj = args[0]->data.obj_val;
    const char* key = args[1]->data.str_val;
    as_value_t* value = args[2];
    
    as_obj_set(obj, key, value);
    
    return as_value_null();
}

/* Obj:has(obj, key): bool - Check if key exists */
static as_value_t* std_obj_has(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_OBJ || args[1]->type != AS_VALUE_STR) {
        fprintf(stderr, "Obj:has: Expected object and string arguments\n");
        return as_value_bool(false);
    }
    
    as_obj_t* obj = args[0]->data.obj_val;
    const char* key = args[1]->data.str_val;
    
    return as_value_bool(as_obj_has(obj, key));
}

/* Obj:copy(obj): obj - Shallow copy object */
static as_value_t* std_obj_copy(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        fprintf(stderr, "Obj:copy: Expected object argument\n");
        return as_value_obj();
    }
    
    as_obj_t* src = args[0]->data.obj_val;
    as_value_t* result = as_value_obj();
    as_obj_t* dest = result->data.obj_val;
    
    /* Copy all entries in insertion order */
    for (as_obj_entry_t* entry = src->order_head; entry != NULL; entry = entry->order_next) {
        as_obj_set(dest, entry->key, entry->value);
    }
    
    return result;
}

/* Obj:merge(a, b): obj - Merge two objects */
static as_value_t* std_obj_merge(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_OBJ || args[1]->type != AS_VALUE_OBJ) {
        fprintf(stderr, "Obj:merge: Expected two object arguments\n");
        return as_value_obj();
    }
    
    as_obj_t* obj_a = args[0]->data.obj_val;
    as_obj_t* obj_b = args[1]->data.obj_val;
    as_value_t* result = as_value_obj();
    as_obj_t* dest = result->data.obj_val;
    
    /* Copy all entries from a */
    for (as_obj_entry_t* entry = obj_a->order_head; entry != NULL; entry = entry->order_next) {
        as_obj_set(dest, entry->key, entry->value);
    }
    
    /* Copy all entries from b (overwrites a) */
    for (as_obj_entry_t* entry = obj_b->order_head; entry != NULL; entry = entry->order_next) {
        as_obj_set(dest, entry->key, entry->value);
    }
    
    return result;
}

/* Obj:pick(obj, keys): obj - Pick specific keys */
static as_value_t* std_obj_pick(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2 || args[0]->type != AS_VALUE_OBJ || args[1]->type != AS_VALUE_ARR) {
        fprintf(stderr, "Obj:pick: Expected object and array arguments\n");
        return as_value_obj();
    }
    
    as_obj_t* src = args[0]->data.obj_val;
    as_arr_t* keys = args[1]->data.arr_val;
    as_value_t* result = as_value_obj();
    as_obj_t* dest = result->data.obj_val;
    
    /* Iterate through keys array */
    for (size_t i = 0; i < keys->length; i++) {
        if (keys->items[i]->type != AS_VALUE_STR) {
            fprintf(stderr, "Obj:pick: Key at index %zu is not a string\n", i);
            continue;
        }
        
        const char* key = keys->items[i]->data.str_val;
        as_value_t* value = as_obj_get(src, key);
        
        if (value) {
            as_obj_set(dest, key, value);
        } else {
            as_value_t* n = as_value_null();
            as_obj_set(dest, key, n);
            as_value_release(n);
        }
    }
    
    return result;
}

/* Obj:from_kvs(kvs): obj - Create object from [key, value] pairs */
static as_value_t* std_obj_from_kvs(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_ARR) {
        fprintf(stderr, "Obj:from_kvs: Expected array argument\n");
        return as_value_obj();
    }
    
    as_arr_t* kvs = args[0]->data.arr_val;
    as_value_t* result = as_value_obj();
    as_obj_t* dest = result->data.obj_val;
    
    /* Iterate through array of [key, value] pairs */
    for (size_t i = 0; i < kvs->length; i++) {
        if (kvs->items[i]->type != AS_VALUE_ARR) {
            fprintf(stderr, "Obj:from_kvs: Element at index %zu is not an array\n", i);
            continue;
        }
        
        as_arr_t* pair = kvs->items[i]->data.arr_val;
        
        if (pair->length < 2) {
            fprintf(stderr, "Obj:from_kvs: Pair at index %zu has less than 2 elements\n", i);
            continue;
        }
        
        if (pair->items[0]->type != AS_VALUE_STR) {
            fprintf(stderr, "Obj:from_kvs: Key at index %zu is not a string\n", i);
            continue;
        }
        
        const char* key = pair->items[0]->data.str_val;
        as_value_t* value = pair->items[1];
        
        as_obj_set(dest, key, value);
    }
    
    return result;
}

/* Register Obj namespace functions */
void as_std_obj_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_obj_keys, NULL);
    as_scope_add(global, "Obj:keys", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_vals, NULL);
    as_scope_add(global, "Obj:vals", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_kvs, NULL);
    as_scope_add(global, "Obj:kvs", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_get, NULL);
    as_scope_add(global, "Obj:get", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_set, NULL);
    as_scope_add(global, "Obj:set", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_has, NULL);
    as_scope_add(global, "Obj:has", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_copy, NULL);
    as_scope_add(global, "Obj:copy", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_merge, NULL);
    as_scope_add(global, "Obj:merge", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_pick, NULL);
    as_scope_add(global, "Obj:pick", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_obj_from_kvs, NULL);
    as_scope_add(global, "Obj:from_kvs", fn, false);
    as_value_release(fn);
}

#else /* NO_OBJ */

void as_std_obj_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Obj namespace is not available in this build (NO_OBJ)\n");
}

#endif /* NO_OBJ */

