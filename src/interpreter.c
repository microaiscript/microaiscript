/*
 * AiScript - Interpreter implementation
 */

#include "interpreter.h"
#include "memory.h"
#include "error.h"
#include "primitive_props.h"
#include "traceback.h"
#include "std_core.h"
#include "std_date.h"
#include "std_math.h"
#include "std_util.h"
#include "std_arr.h"
#include "std_uri.h"
#include "std_num.h"
#include "std_json.h"
#include "std_error.h"
#include "std_str.h"
#include "std_obj.h"
#include "std_async.h"
#include "aiscript.h"  /* For as_module_metadata_t */
#include "parser.h"
#include "scanner.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h> /* For stat() */

/* Platform-specific includes for module loading */
#ifdef _WIN32
    #include <windows.h>
    #define MODULE_EXT ".dll"
#else
    #include <dlfcn.h>
    #ifdef __APPLE__
        #define MODULE_EXT ".dylib"
    #else
        #define MODULE_EXT ".so"
    #endif
#endif

/* Platform-specific readline headers */
#ifdef HAVE_READLINE
    #include <readline/readline.h>
    #include <readline/history.h>
#endif

/* Forward declaration - exposed for use by primitive_props.c */
as_value_t* eval_node(as_interpreter_t* interp, as_scope_t* scope, as_node_t* node);

/* Recursive helper: bind 'val' to destructuring pattern 'dest' in scope */
static void destructure_def(as_interpreter_t* interp, as_scope_t* scope,
                             as_node_t* dest, as_value_t* val, bool is_mut) {
    if (dest->type == NODE_IDENTIFIER) {
        as_node_identifier_t* id = (as_node_identifier_t*)dest;
        as_scope_add(scope, id->name, val, is_mut);
    } else if (dest->type == NODE_ARR) {
        as_node_arr_t* pattern = (as_node_arr_t*)dest;
        for (size_t i = 0; i < pattern->element_count; i++) {
            as_value_t* elem_val = NULL;
            if (val->type == AS_VALUE_ARR) {
                as_arr_t* arr = val->data.arr_val;
                elem_val = (i < arr->length) ? as_value_retain(arr->items[i]) : as_value_null();
            } else {
                elem_val = as_value_null();
            }
            destructure_def(interp, scope, pattern->elements[i], elem_val, is_mut);
            as_value_release(elem_val);
        }
    } else if (dest->type == NODE_OBJ) {
        as_node_obj_t* pattern = (as_node_obj_t*)dest;
        for (size_t i = 0; i < pattern->entry_count; i++) {
            const char* key = pattern->entries[i].key;
            as_value_t* field_val = NULL;
            if (val->type == AS_VALUE_OBJ) {
                as_value_t* found = as_obj_get(val->data.obj_val, key);
                field_val = found ? as_value_retain(found) : as_value_null();
            } else {
                field_val = as_value_null();
            }
            /* pattern entry value is the destination node (identifier or nested pattern) */
            if (pattern->entries[i].value) {
                destructure_def(interp, scope, pattern->entries[i].value, field_val, is_mut);
            } else {
                /* shorthand {key} - bind to variable named 'key' */
                as_scope_add(scope, key, field_val, is_mut);
            }
            as_value_release(field_val);
        }
    }
    /* Other pattern types are silently ignored */
}

/* Recursive helper: assign 'val' to a destructuring destination in scope */
static void destructure_assign(as_interpreter_t* interp, as_scope_t* scope,
                                as_node_t* dest, as_value_t* val) {
    if (dest->type == NODE_IDENTIFIER) {
        as_node_identifier_t* id = (as_node_identifier_t*)dest;
        as_scope_assign(scope, id->name, val);
    } else if (dest->type == NODE_ARR) {
        as_node_arr_t* pattern = (as_node_arr_t*)dest;
        for (size_t i = 0; i < pattern->element_count; i++) {
            as_value_t* elem_val = NULL;
            if (val->type == AS_VALUE_ARR) {
                as_arr_t* arr = val->data.arr_val;
                elem_val = (i < arr->length) ? as_value_retain(arr->items[i]) : as_value_null();
            } else {
                elem_val = as_value_null();
            }
            destructure_assign(interp, scope, pattern->elements[i], elem_val);
            as_value_release(elem_val);
        }
    } else if (dest->type == NODE_OBJ) {
        as_node_obj_t* pattern = (as_node_obj_t*)dest;
        for (size_t i = 0; i < pattern->entry_count; i++) {
            const char* key = pattern->entries[i].key;
            as_value_t* field_val = NULL;
            if (val->type == AS_VALUE_OBJ) {
                as_value_t* found = as_obj_get(val->data.obj_val, key);
                field_val = found ? as_value_retain(found) : as_value_null();
            } else {
                field_val = as_value_null();
            }
            if (pattern->entries[i].value) {
                destructure_assign(interp, scope, pattern->entries[i].value, field_val);
            } else {
                as_scope_assign(scope, key, field_val);
            }
            as_value_release(field_val);
        }
    }
}

static int continue_targets_here(as_interpreter_t* interp, const char* block_label) {
    if (!interp->continue_flag) {
        return 0;
    }
    if (!interp->continue_label) {
        return 1;
    }
    return block_label && strcmp(interp->continue_label, block_label) == 0;
}

static void clear_continue(as_interpreter_t* interp) {
    interp->continue_flag = false;
    if (interp->continue_label) {
        as_free(interp->continue_label);
        interp->continue_label = NULL;
    }
}

static as_value_t* apply_update(as_value_t* current, as_value_t* operand, int is_sub) {
    if (current && operand &&
        current->type == AS_VALUE_NUM && operand->type == AS_VALUE_NUM) {
        double v = is_sub
            ? current->data.num_val - operand->data.num_val
            : current->data.num_val + operand->data.num_val;
        return as_value_num(v);
    }
    return as_value_null();
}

/* Evaluate assignment / += / -=. Dest is evaluated before the RHS so
 * `a[return 0] = 1` returns 0. Index/prop targets are evaluated once. */
static as_value_t* eval_assign_op(as_interpreter_t* interp, as_scope_t* scope,
                                  as_node_t* dest, as_node_t* expr, int is_update, int is_sub) {
    if (dest->type == NODE_IDENTIFIER) {
        as_value_t* val = eval_node(interp, scope, expr);
        if (interp->aborted) {
            return val ? val : as_value_null();
        }
        as_node_identifier_t* id = (as_node_identifier_t*)dest;
        if (!is_update) {
            as_scope_assign(scope, id->name, val);
        } else {
            as_value_t* current = as_scope_get(scope, id->name);
            as_value_t* result = apply_update(current, val, is_sub);
            as_scope_assign(scope, id->name, result);
            as_value_release(result);
        }
        as_value_release(val);
        return as_value_null();
    }

    if (dest->type == NODE_ARR || dest->type == NODE_OBJ) {
        as_value_t* val = eval_node(interp, scope, expr);
        if (interp->aborted) {
            return val ? val : as_value_null();
        }
        destructure_assign(interp, scope, dest, val);
        as_value_release(val);
        return as_value_null();
    }

    if (dest->type == NODE_INDEX) {
        as_node_index_t* index = (as_node_index_t*)dest;
        as_value_t* target = eval_node(interp, scope, index->target);
        if (interp->aborted) {
            return target ? target : as_value_null();
        }
        as_value_t* idx = eval_node(interp, scope, index->index);
        if (interp->aborted) {
            as_value_release(target);
            return idx ? idx : as_value_null();
        }
        as_value_t* val = eval_node(interp, scope, expr);
        if (interp->aborted) {
            as_value_release(target);
            as_value_release(idx);
            return val ? val : as_value_null();
        }
        if (target && target->type == AS_VALUE_ARR && idx && idx->type == AS_VALUE_NUM) {
            int i = (int)idx->data.num_val;
            as_arr_t* arr = target->data.arr_val;
            if (i < 0 || i >= (int)arr->length) {
                as_traceback_push("<script>", dest->loc.start.line, 0, "<module>");
                char message[256];
                snprintf(message, sizeof(message),
                         "list assignment index out of range (index: %d, length: %zu)",
                         i, arr->length);
                as_error_report(AS_ERROR_PYTHONIC_INDEX, message);
                as_traceback_pop();
            } else if (!is_update) {
                as_value_release(arr->items[i]);
                arr->items[i] = as_value_retain(val);
            } else {
                as_value_t* result = apply_update(arr->items[i], val, is_sub);
                as_value_release(arr->items[i]);
                arr->items[i] = result;
            }
        } else if (target && target->type == AS_VALUE_OBJ && idx) {
            char* key = (idx->type == AS_VALUE_STR)
                ? as_strdup(idx->data.str_val)
                : as_value_to_string(idx);
            if (!is_update) {
                as_obj_set(target->data.obj_val, key, val);
            } else {
                as_value_t* current = as_obj_get(target->data.obj_val, key);
                as_value_t* result = apply_update(current, val, is_sub);
                as_obj_set(target->data.obj_val, key, result);
                as_value_release(result);
            }
            as_free(key);
        }
        as_value_release(target);
        as_value_release(idx);
        as_value_release(val);
        return as_value_null();
    }

    if (dest->type == NODE_PROP) {
        as_node_prop_t* prop = (as_node_prop_t*)dest;
        as_value_t* target = eval_node(interp, scope, prop->target);
        if (interp->aborted) {
            return target ? target : as_value_null();
        }
        as_value_t* val = eval_node(interp, scope, expr);
        if (interp->aborted) {
            as_value_release(target);
            return val ? val : as_value_null();
        }
        if (target && target->type == AS_VALUE_OBJ) {
            if (!is_update) {
                as_obj_set(target->data.obj_val, prop->name, val);
            } else {
                as_value_t* current = as_obj_get(target->data.obj_val, prop->name);
                as_value_t* result = apply_update(current, val, is_sub);
                as_obj_set(target->data.obj_val, prop->name, result);
                as_value_release(result);
            }
        }
        as_value_release(target);
        as_value_release(val);
        return as_value_null();
    }

    {
        as_value_t* val = eval_node(interp, scope, expr);
        if (interp->aborted) {
            return val ? val : as_value_null();
        }
        as_value_release(val);
        return as_value_null();
    }
}

/* Module loading helpers */

/* Check if file exists */
static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Extract module name from path (e.g., "foo.ais" -> "foo", "bar/baz" -> "baz") */
static char* extract_module_name(const char* path) {
    /* Find last slash */
    const char* last_slash = strrchr(path, '/');
    #ifdef _WIN32
    const char* last_backslash = strrchr(path, '\\');
    if (last_backslash && (!last_slash || last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
    #endif
    
    const char* filename = last_slash ? last_slash + 1 : path;
    
    /* Find last dot for extension */
    const char* last_dot = strrchr(filename, '.');
    size_t name_len = last_dot ? (size_t)(last_dot - filename) : strlen(filename);
    
    char* name = (char*)as_malloc(name_len + 1);
    strncpy(name, filename, name_len);
    name[name_len] = '\0';
    
    return name;
}

/* Load native module (.so/.dll) into a specific scope */
/* Load native module metadata (if available) */
static as_value_t* load_native_metadata(void* handle, const char* module_name) {
    #ifdef _WIN32
        char meta_func_name[256];
        snprintf(meta_func_name, sizeof(meta_func_name), "as_modmeta_%s", module_name);
        
        typedef as_module_metadata_t* (*meta_func_t)(void);
        meta_func_t meta_func = (meta_func_t)GetProcAddress((HMODULE)handle, meta_func_name);
        
        if (!meta_func) {
            return NULL;  /* No metadata */
        }
    #else
        char meta_func_name[256];
        snprintf(meta_func_name, sizeof(meta_func_name), "as_modmeta_%s", module_name);
        
        typedef as_module_metadata_t* (*meta_func_t)(void);
        meta_func_t meta_func = (meta_func_t)dlsym(handle, meta_func_name);
        
        if (!meta_func) {
            return NULL;  /* No metadata */
        }
    #endif
    
    /* Get metadata from module */
    as_module_metadata_t* meta = meta_func();
    if (!meta) {
        return NULL;
    }
    
    /* Convert to AiScript object */
    as_value_t* obj = as_value_obj();
    as_obj_t* obj_data = obj->data.obj_val;
    
    if (meta->name) {
        as_obj_set(obj_data, "name", as_value_str(meta->name));
    }
    if (meta->version) {
        as_obj_set(obj_data, "version", as_value_str(meta->version));
    }
    if (meta->author) {
        as_obj_set(obj_data, "author", as_value_str(meta->author));
    }
    if (meta->description) {
        as_obj_set(obj_data, "description", as_value_str(meta->description));
    }
    if (meta->license) {
        as_obj_set(obj_data, "license", as_value_str(meta->license));
    }
    if (meta->url) {
        as_obj_set(obj_data, "url", as_value_str(meta->url));
    }
    
    return obj;
}

static void* load_native_module(as_interpreter_t* interp, const char* path, const char* module_name, as_scope_t* target_scope, as_value_t** metadata_out) {
    #ifdef _WIN32
        HMODULE handle = LoadLibrary(path);
        if (!handle) {
            fprintf(stderr, "Error: Failed to load native module '%s' (error code: %ld)\n", path, GetLastError());
            return NULL;
        }
        
        /* Find initialization function: as_modinit_<module_name> */
        char init_func_name[256];
        snprintf(init_func_name, sizeof(init_func_name), "as_modinit_%s", module_name);
        
        typedef void (*init_func_t)(as_interpreter_t*, as_scope_t*);
        init_func_t init_func = (init_func_t)GetProcAddress(handle, init_func_name);
        
        if (!init_func) {
            fprintf(stderr, "Error: Module init function '%s' not found in '%s'\n", init_func_name, path);
            FreeLibrary(handle);
            return NULL;
        }
        
        /* Call initialization with target scope */
        init_func(interp, target_scope);
        
        /* Load metadata (optional) */
        if (metadata_out) {
            *metadata_out = load_native_metadata((void*)handle, module_name);
        }
        
        return (void*)handle;  /* Return handle for later unloading */
    #else
        void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            fprintf(stderr, "Error: Failed to load native module '%s': %s\n", path, dlerror());
            return NULL;
        }
        
        /* Find initialization function: as_modinit_<module_name> */
        char init_func_name[256];
        snprintf(init_func_name, sizeof(init_func_name), "as_modinit_%s", module_name);
        
        typedef void (*init_func_t)(as_interpreter_t*, as_scope_t*);
        init_func_t init_func = (init_func_t)dlsym(handle, init_func_name);
        
        if (!init_func) {
            fprintf(stderr, "Error: Module init function '%s' not found in '%s'\n", init_func_name, path);
            dlclose(handle);
            return NULL;
        }
        
        /* Call initialization with target scope */
        init_func(interp, target_scope);
        
        /* Load metadata (optional) */
        if (metadata_out) {
            *metadata_out = load_native_metadata(handle, module_name);
        }
        
        return handle;  /* Return handle for later unloading */
    #endif
}

/* Extract metadata from .ais module AST nodes */
static as_value_t* extract_ais_metadata(as_node_t** nodes, size_t node_count) {
    /* Find all metadata nodes */
    size_t meta_count = as_get_metadata(nodes, node_count, NULL);
    if (meta_count == 0) {
        return NULL;
    }
    
    as_node_t** meta_nodes = (as_node_t**)as_malloc(meta_count * sizeof(as_node_t*));
    as_get_metadata(nodes, node_count, meta_nodes);
    
    /* Convert metadata nodes to AiScript object */
    as_value_t* obj = as_value_obj();
    as_obj_t* obj_data = obj->data.obj_val;
    
    for (size_t i = 0; i < meta_count; i++) {
        as_node_meta_t* meta = (as_node_meta_t*)meta_nodes[i];
        
        /* Evaluate metadata value (it's a literal, safe to eval) */
        as_value_t* value = NULL;
        
        switch (meta->value->type) {
            case NODE_STR: {
                as_node_str_t* str = (as_node_str_t*)meta->value;
                value = as_value_str(str->value);
                break;
            }
            case NODE_NUM: {
                as_node_num_t* num = (as_node_num_t*)meta->value;
                value = as_value_num(num->value);
                break;
            }
            case NODE_BOOL: {
                as_node_bool_t* b = (as_node_bool_t*)meta->value;
                value = as_value_bool(b->value);
                break;
            }
            case NODE_NULL:
                value = as_value_null();
                break;
            case NODE_ARR: {
                as_node_arr_t* arr = (as_node_arr_t*)meta->value;
                as_value_t* arr_val = as_value_arr();
                as_arr_t* arr_data = arr_val->data.arr_val;
                
                for (size_t j = 0; j < arr->element_count; j++) {
                    /* Recursively evaluate array elements (only literals allowed) */
                    if (arr->elements[j]->type == NODE_STR) {
                        as_node_str_t* str = (as_node_str_t*)arr->elements[j];
                        as_arr_push(arr_data, as_value_str(str->value));
                    } else if (arr->elements[j]->type == NODE_NUM) {
                        as_node_num_t* num = (as_node_num_t*)arr->elements[j];
                        as_arr_push(arr_data, as_value_num(num->value));
                    }
                    /* Other types can be added as needed */
                }
                value = arr_val;
                break;
            }
            default:
                value = as_value_null();
                break;
        }
        
        if (value) {
            as_obj_set(obj_data, meta->name, value);
        }
    }
    
    as_free(meta_nodes);
    return obj;
}

/* Load .ais file module into a specific scope */
static bool load_ais_module(as_interpreter_t* interp, const char* path, as_scope_t* target_scope, as_value_t** metadata_out) {
    FILE* file = fopen(path, "r");
    if (!file) {
        return false;
    }
    
    /* Read file contents */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* source = (char*)as_malloc(file_size + 1);
    size_t bytes_read = fread(source, 1, file_size, file);
    source[bytes_read] = '\0';
    fclose(file);
    
    /* Parse the module */
    as_parser_t* parser = as_parser_new(source);
    size_t node_count = 0;
    as_node_t** nodes = as_parser_parse(parser, &node_count);
    
    if (parser->error_message) {
        fprintf(stderr, "Parse error in module '%s': %s\n", path, parser->error_message);
        as_parser_free(parser);
        as_free(source);
        return false;
    }
    
    /* Extract metadata before executing */
    if (metadata_out) {
        *metadata_out = extract_ais_metadata(nodes, node_count);
    }
    
    /* Execute the module in the target scope */
    for (size_t i = 0; i < node_count; i++) {
        as_value_t* result = eval_node(interp, target_scope, nodes[i]);
        if (result) as_value_release(result);
        
        /* Check for errors */
        if (interp->error_message || interp->aborted) {
            break;
        }
    }
    
    /* Cleanup */
    for (size_t i = 0; i < node_count; i++) {
        as_node_free(nodes[i]);
    }
    if (nodes) as_free(nodes);
    as_parser_free(parser);
    as_free(source);
    
    return true;
}

/* Search for module in a given directory */
static char* search_module_in_path(const char* base_path, const char* module_name) {
    char path[1024];
    
    /* Try package: <base_path>/<module_name>/__init__.ais */
    snprintf(path, sizeof(path), "%s/%s/__init__.ais", base_path, module_name);
    if (file_exists(path)) {
        return as_strdup(path);
    }
    
    /* Try package native: <base_path>/<module_name>/__init__<MODULE_EXT> */
    snprintf(path, sizeof(path), "%s/%s/__init__%s", base_path, module_name, MODULE_EXT);
    if (file_exists(path)) {
        return as_strdup(path);
    }
    
    /* Try file: <base_path>/<module_name>.ais */
    snprintf(path, sizeof(path), "%s/%s.ais", base_path, module_name);
    if (file_exists(path)) {
        return as_strdup(path);
    }
    
    /* Try native: <base_path>/<module_name><MODULE_EXT> */
    snprintf(path, sizeof(path), "%s/%s%s", base_path, module_name, MODULE_EXT);
    if (file_exists(path)) {
        return as_strdup(path);
    }
    
    return NULL;
}

/* Get module search paths from Core:modpath */
static char** get_modpaths_from_core(as_interpreter_t* interp, size_t* count_out) {
    /* Try to get Core:modpath from global scope */
    as_value_t* modpath = as_scope_get(interp->global_scope, "Core:modpath");
    
    if (!modpath || modpath->type != AS_VALUE_ARR) {
        /* Fallback to current directory if Core:modpath not available */
        *count_out = 1;
        char** paths = (char**)as_malloc(sizeof(char*));
        paths[0] = as_strdup(".");
        return paths;
    }
    
    /* Extract paths from the array */
    as_arr_t* arr = modpath->data.arr_val;
    *count_out = arr->length;
    
    if (*count_out == 0) {
        /* Empty array - use current directory */
        *count_out = 1;
        char** paths = (char**)as_malloc(sizeof(char*));
        paths[0] = as_strdup(".");
        return paths;
    }
    
    char** paths = (char**)as_malloc(sizeof(char*) * (*count_out));
    for (size_t i = 0; i < *count_out; i++) {
        as_value_t* path_val = arr->items[i];
        if (path_val && path_val->type == AS_VALUE_STR) {
            paths[i] = as_strdup(path_val->data.str_val);
        } else {
            /* Skip non-string entries */
            paths[i] = as_strdup(".");
        }
    }
    
    return paths;
}

/* Register all variables from module scope into parent scope with namespace prefix */
static void register_module_namespace(as_scope_t* parent_scope, as_scope_t* module_scope, const char* namespace_name) {
    /* Iterate through all variables in module scope */
    for (size_t i = 0; i < module_scope->var_count; i++) {
        as_variable_t* var = module_scope->variables[i];
        
        /* Create namespaced name: namespace:variable */
        size_t ns_name_len = strlen(namespace_name) + strlen(var->name) + 2;  /* name + : + var + \0 */
        char* ns_name = (char*)as_malloc(ns_name_len);
        snprintf(ns_name, ns_name_len, "%s:%s", namespace_name, var->name);
        
        /* Add to parent scope with namespaced name */
        as_scope_add(parent_scope, ns_name, var->value, var->is_mutable);
        
        as_free(ns_name);
    }
}

/* Standard library functions */
static as_value_t* std_print(as_value_t** args, size_t arg_count, void* userdata) {
    as_interpreter_t* interp = (as_interpreter_t*)userdata;

    if (interp && interp->out_fn) {
        /* Embedding path: pass the actual printed value (mirrors TS `out`). */
        size_t i;
        for (i = 0; i < arg_count; i++) {
            interp->out_fn(args[i], interp->cb_userdata);
        }
        return arg_count > 0 ? as_value_retain(args[0]) : as_value_null();
    }

    /* Default path: write to stdout */
    for (size_t i = 0; i < arg_count; i++) {
        /* For strings, print raw value without quotes */
        if (args[i]->type == AS_VALUE_STR) {
            printf("%s", args[i]->data.str_val);
        } else {
            char* str = as_value_to_string(args[i]);
            printf("%s", str);
            as_free(str);
        }
        if (i < arg_count - 1) printf(" ");
    }
    printf("\n");
    fflush(stdout);  /* Flush immediately like Python to show output before errors */
    /* Return the first argument (or null if no arguments) */
    if (arg_count > 0) {
        return as_value_retain(args[0]);
    }
    return as_value_null();
}

/* readline(message) - Accept string input from user */
static as_value_t* std_readline(as_value_t** args, size_t arg_count, void* userdata) {
    as_interpreter_t* interp = (as_interpreter_t*)userdata;

    /* Get prompt message (default to empty string) */
    const char* prompt = "";
    if (arg_count >= 1 && args[0]->type == AS_VALUE_STR) {
        prompt = args[0]->data.str_val;
    }

    if (interp && interp->in_fn) {
        /* Embedding path: delegate input to the host application. */
        char* line = interp->in_fn(prompt, interp->cb_userdata);
        if (!line) return as_value_str("");
        as_value_t* result = as_value_str(line);
        as_free(line);
        return result;
    }

    /* Default path: read from stdin */
    if (arg_count >= 1 && args[0]->type == AS_VALUE_STR) {
        /* Display prompt with ": " suffix like TypeScript version */
        printf("%s: ", prompt);
        fflush(stdout);
    }

    #ifdef HAVE_READLINE
        /* Use GNU readline for input (without the prompt since we already printed it) */
        char* line = readline("");
        if (line == NULL) {
            /* EOF or error */
            return as_value_str("");
        }
        
        /* Add to history if non-empty */
        if (line[0] != '\0') {
            add_history(line);
        }
        
        /* Create AiScript string value */
        as_value_t* result = as_value_str(line);
        free(line);  /* readline allocates with malloc */
        return result;
    #else
        /* Fallback: use fgets */
        char buffer[4096];
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            /* EOF or error */
            return as_value_str("");
        }
        
        /* Remove trailing newline */
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        
        return as_value_str(buffer);
    #endif
}

/* Evaluate binary operation */
static as_value_t* eval_binary_op(as_interpreter_t* interp, as_scope_t* scope,
                                  as_node_type_t op, as_node_t* left_node, as_node_t* right_node) {
    as_value_t* left = eval_node(interp, scope, left_node);
    if (interp->aborted) return left ? left : as_value_null();
    if (!left) return as_value_null();

    if (op == NODE_AND) {
        if (!as_value_to_bool(left)) {
            as_value_release(left);
            return as_value_bool(false);
        }
        as_value_release(left);
        {
            as_value_t* right = eval_node(interp, scope, right_node);
            bool b;
            if (interp->aborted) return right ? right : as_value_null();
            b = as_value_to_bool(right);
            as_value_release(right);
            return as_value_bool(b);
        }
    }
    if (op == NODE_OR) {
        if (as_value_to_bool(left)) {
            as_value_release(left);
            return as_value_bool(true);
        }
        as_value_release(left);
        {
            as_value_t* right = eval_node(interp, scope, right_node);
            bool b;
            if (interp->aborted) return right ? right : as_value_null();
            b = as_value_to_bool(right);
            as_value_release(right);
            return as_value_bool(b);
        }
    }

    as_value_t* right = eval_node(interp, scope, right_node);
    if (interp->aborted) {
        as_value_release(left);
        return right ? right : as_value_null();
    }
    if (!right) {
        as_value_release(left);
        return as_value_null();
    }
    
    as_value_t* result = NULL;
    
    switch (op) {
        case NODE_ADD:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_num(left->data.num_val + right->data.num_val);
            } else if (left->type == AS_VALUE_STR || right->type == AS_VALUE_STR) {
                char* left_str = as_value_to_string(left);
                char* right_str = as_value_to_string(right);
                char* concat = as_malloc(strlen(left_str) + strlen(right_str) + 1);
                strcpy(concat, left_str);
                strcat(concat, right_str);
                result = as_value_str(concat);
                as_free(concat);
                as_free(left_str);
                as_free(right_str);
            }
            break;
            
        case NODE_SUB:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_num(left->data.num_val - right->data.num_val);
            }
            break;
            
        case NODE_MUL:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_num(left->data.num_val * right->data.num_val);
            }
            break;
            
        case NODE_DIV:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                /* IEEE 754: x/0 is +-Infinity, 0/0 is NaN */
                result = as_value_num(left->data.num_val / right->data.num_val);
            }
            break;
            
        case NODE_REM:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_num(fmod(left->data.num_val, right->data.num_val));
            }
            break;
            
        case NODE_POW:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_num(pow(left->data.num_val, right->data.num_val));
            }
            break;
            
        case NODE_LT:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_bool(left->data.num_val < right->data.num_val);
            }
            break;
            
        case NODE_LTEQ:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_bool(left->data.num_val <= right->data.num_val);
            }
            break;
            
        case NODE_GT:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_bool(left->data.num_val > right->data.num_val);
            }
            break;
            
        case NODE_GTEQ:
            if (left->type == AS_VALUE_NUM && right->type == AS_VALUE_NUM) {
                result = as_value_bool(left->data.num_val >= right->data.num_val);
            }
            break;
            
        case NODE_EQ:
            result = as_value_bool(as_value_equals(left, right));
            break;
            
        case NODE_NEQ:
            result = as_value_bool(!as_value_equals(left, right));
            break;
            
        case NODE_AND:
            if (!as_value_to_bool(left)) {
                result = as_value_bool(false);
            } else {
                result = as_value_bool(as_value_to_bool(right));
            }
            break;
            
        case NODE_OR:
            if (as_value_to_bool(left)) {
                result = as_value_bool(true);
            } else {
                result = as_value_bool(as_value_to_bool(right));
            }
            break;
            
        default:
            result = as_value_null();
            break;
    }
    
    as_value_release(left);
    as_value_release(right);
    
    return result ? result : as_value_null();
}

/* Evaluate unary operation */
static as_value_t* eval_unary_op(as_interpreter_t* interp, as_scope_t* scope,
                                 as_node_type_t op, as_node_t* expr_node) {
    as_value_t* expr = eval_node(interp, scope, expr_node);
    if (interp->aborted) return expr ? expr : as_value_null();
    if (!expr) return as_value_null();
    
    as_value_t* result = NULL;
    
    switch (op) {
        case NODE_PLUS:
            if (expr->type == AS_VALUE_NUM) {
                result = as_value_num(expr->data.num_val);
            }
            break;
            
        case NODE_MINUS:
            if (expr->type == AS_VALUE_NUM) {
                result = as_value_num(-expr->data.num_val);
            }
            break;
            
        case NODE_NOT:
            result = as_value_bool(!as_value_to_bool(expr));
            break;
            
        default:
            result = as_value_null();
            break;
    }
    
    as_value_release(expr);
    
    return result ? result : as_value_null();
}

/* Evaluate node */
/* Expose eval_node for use by primitive_props.c */
as_value_t* eval_node(as_interpreter_t* interp, as_scope_t* scope, as_node_t* node) {
    if (!node) return as_value_null();
    if (interp->aborted) return as_value_null();
    
    switch (node->type) {
        case NODE_NULL:
            return as_value_null();
            
        case NODE_BOOL: {
            as_node_bool_t* bool_node = (as_node_bool_t*)node;
            return as_value_bool(bool_node->value);
        }
        
        case NODE_META: {
            /* Metadata nodes are parsed but not executed */
            return as_value_null();
        }
        
        case NODE_IMPORT: {
            as_node_import_t* import = (as_node_import_t*)node;
            
            /* Extract module name from path */
            char* module_name = extract_module_name(import->path);
            
            /* Determine namespace name (use alias if provided, otherwise module name) */
            const char* namespace_name = import->alias ? import->alias : module_name;
            
            /* Check if module already loaded */
            as_module_cache_entry_t* cache = interp->module_cache;
            while (cache) {
                if (strcmp(cache->path, module_name) == 0 && cache->loaded) {
                    /* Already loaded: a selective import binds the requested names from the
                     * "module:name" entries registered when the module was first loaded */
                    if (import->imports && import->import_count > 0) {
                        for (size_t i = 0; i < import->import_count; i++) {
                            size_t full_len = strlen(module_name) + strlen(import->imports[i]) + 2;
                            char* full_name = as_malloc(full_len);
                            snprintf(full_name, full_len, "%s:%s", module_name, import->imports[i]);
                            as_value_t* value = as_scope_get(interp->global_scope, full_name);
                            if (value) {
                                as_scope_add(interp->global_scope, import->imports[i], value, false);
                            } else {
                                fprintf(stderr, "Warning: '%s' not found in module '%s'\n",
                                        import->imports[i], module_name);
                            }
                            as_free(full_name);
                        }
                        as_free(module_name);
                        return as_value_null();
                    }
                    /* If an alias was requested, re-register entries under it */
                    if (import->alias) {
                        size_t old_prefix_len = strlen(module_name);
                        size_t new_prefix_len = strlen(import->alias);
                        as_scope_t* gs = interp->global_scope;
                        size_t existing = gs->var_count;
                        for (size_t vi = 0; vi < existing; vi++) {
                            const char* vname = gs->variables[vi]->name;
                            /* Match "module_name:..." */
                            if (strncmp(vname, module_name, old_prefix_len) == 0 &&
                                vname[old_prefix_len] == ':') {
                                const char* suffix = vname + old_prefix_len + 1;
                                size_t alias_name_len = new_prefix_len + 1 + strlen(suffix) + 1;
                                char* alias_name = as_malloc(alias_name_len);
                                snprintf(alias_name, alias_name_len, "%s:%s", import->alias, suffix);
                                as_scope_add(gs, alias_name, gs->variables[vi]->value, false);
                                as_free(alias_name);
                            }
                        }
                    }
                    as_free(module_name);
                    return as_value_null();
                }
                cache = cache->next;
            }
            
            /* Determine if explicit path was given (has extension or path separators) */
            bool is_explicit_path = strchr(import->path, '/') || strchr(import->path, '.') 
                                    #ifdef _WIN32
                                    || strchr(import->path, '\\')
                                    #endif
                                    ;
            
            char* resolved_path = NULL;
            
            if (is_explicit_path) {
                /* Explicit path given - try it directly */
                if (file_exists(import->path)) {
                    /* dlopen() only looks in the current directory for names that
                     * contain a slash, so anchor bare file names with "./" */
                    bool has_separator = strchr(import->path, '/') != NULL;
                    #ifdef _WIN32
                    has_separator = has_separator || strchr(import->path, '\\') != NULL;
                    #endif
                    if (has_separator) {
                        resolved_path = as_strdup(import->path);
                    } else {
                        size_t anchored_len = strlen(import->path) + 3;
                        resolved_path = as_malloc(anchored_len);
                        snprintf(resolved_path, anchored_len, "./%s", import->path);
                    }
                }
            } else {
                /* Search through Core:modpath */
                size_t modpath_count = 0;
                char** modpaths = get_modpaths_from_core(interp, &modpath_count);
                
                for (size_t i = 0; i < modpath_count && !resolved_path; i++) {
                    resolved_path = search_module_in_path(modpaths[i], module_name);
                }
                
                /* Cleanup modpaths */
                for (size_t i = 0; i < modpath_count; i++) {
                    as_free(modpaths[i]);
                }
                as_free(modpaths);
            }
            
            if (!resolved_path) {
                fprintf(stderr, "Error: Module '%s' not found\n", import->path);
                as_free(module_name);
                return as_value_error("import_error", as_value_null());
            }
            
            /* Create a child scope for the module */
            as_scope_t* module_scope = as_scope_create_child(scope, namespace_name);
            
            /* Determine module type by extension */
            size_t path_len = strlen(resolved_path);
            bool is_native = (path_len > 3 && strcmp(resolved_path + path_len - 3, ".so") == 0) ||
                            (path_len > 4 && strcmp(resolved_path + path_len - 4, ".dll") == 0) ||
                            (path_len > 6 && strcmp(resolved_path + path_len - 6, ".dylib") == 0);
            
            bool success = false;
            void* module_handle = NULL;
            as_value_t* metadata = NULL;
            
            if (is_native) {
                /* Load native module into module scope */
                module_handle = load_native_module(interp, resolved_path, module_name, module_scope, &metadata);
                success = (module_handle != NULL);
            } else {
                /* Load .ais module into module scope */
                success = load_ais_module(interp, resolved_path, module_scope, &metadata);
            }
            
            as_free(resolved_path);
            
            if (!success) {
                as_scope_release(module_scope);
                as_free(module_name);
                return as_value_error("import_error", as_value_null());
            }
            
            /* Register module variables in global scope */
            if (import->imports && import->import_count > 0) {
                /* Selective import - register only specified functions WITHOUT namespace prefix */
                for (size_t i = 0; i < import->import_count; i++) {
                    const char* import_name = import->imports[i];
                    as_value_t* value = as_scope_get(module_scope, import_name);
                    
                    if (!value) {
                        fprintf(stderr, "Warning: '%s' not found in module '%s'\n", import_name, module_name);
                        continue;
                    }
                    
                    /* Register directly in global scope without namespace prefix */
                    as_scope_add(interp->global_scope, import_name, value, false);
                }
                /* Also keep the "module:name" entries so that later imports of this
                 * (now cached) module can bind other names from it */
                register_module_namespace(interp->global_scope, module_scope, namespace_name);
            } else {
                /* Full import - register all variables with namespace prefix */
                register_module_namespace(interp->global_scope, module_scope, namespace_name);
            }
            
            /* Release module scope (items are now referenced in global scope) */
            as_scope_release(module_scope);
            
            /* Mark module as loaded */
            as_module_cache_entry_t* new_cache = (as_module_cache_entry_t*)as_malloc(sizeof(as_module_cache_entry_t));
            new_cache->path = module_name;  /* Store module name, not full path */
            new_cache->loaded = true;
            new_cache->handle = module_handle;  /* Store native handle or NULL for .ais */
            new_cache->is_native = is_native;
            new_cache->metadata = metadata;  /* Store metadata or NULL */
            new_cache->next = interp->module_cache;
            interp->module_cache = new_cache;
            
            return as_value_null();
        }
        
        case NODE_NAMESPACE: {
            /* Process namespace: create child scope and evaluate members */
            as_node_namespace_t* ns = (as_node_namespace_t*)node;
            as_scope_t* ns_scope = as_scope_create_child_namespace(scope, ns->name);
            size_t i;

            /* Nested namespaces first so inner names are visible to later defs. */
            for (i = 0; i < ns->member_count; i++) {
                if (ns->members[i]->type == NODE_NAMESPACE) {
                    as_value_t* result = eval_node(interp, ns_scope, ns->members[i]);
                    as_value_release(result);
                    if (interp->aborted) {
                        as_scope_release(ns_scope);
                        return as_value_null();
                    }
                }
            }

            for (i = 0; i < ns->member_count; i++) {
                as_node_t* member = ns->members[i];
                if (member->type != NODE_DEF) {
                    continue;
                }
                as_node_def_t* def = (as_node_def_t*)member;
                if (def->is_mut) {
                    fprintf(stderr, "Runtime error: 'var' not allowed in namespace '%s'\n", ns->name);
                    as_scope_release(ns_scope);
                    return as_value_error("NamespaceError: var not allowed", as_value_null());
                }
                /* Attributes are evaluated in the enclosing scope (AiScript 1.2.1) */
                for (size_t a = 0; a < def->attr_count; a++) {
                    if (def->attrs[a] && def->attrs[a]->value) {
                        as_value_t* attr_val = eval_node(interp, scope, def->attrs[a]->value);
                        int failed = interp->aborted || (attr_val && attr_val->type == AS_VALUE_ERROR);
                        if (failed) {
                            as_scope_release(ns_scope);
                            return attr_val ? attr_val : as_value_null();
                        }
                        as_value_release(attr_val);
                    }
                }
                as_value_t* value = eval_node(interp, ns_scope, def->expr);
                if (interp->aborted) {
                    as_scope_release(ns_scope);
                    return value ? value : as_value_null();
                }
                if (def->dest->type == NODE_IDENTIFIER) {
                    as_node_identifier_t* id = (as_node_identifier_t*)def->dest;
                    as_scope_add(ns_scope, id->name, value, false);
                }
                as_value_release(value);
            }

            as_scope_release(ns_scope);
            return as_value_null();
        }
        
        case NODE_EXISTS: {
            as_node_exists_t* exists = (as_node_exists_t*)node;
            
            /* Check if the identifier exists in scope */
            if (exists->identifier->type != NODE_IDENTIFIER) {
                fprintf(stderr, "Runtime error: exists requires an identifier\n");
                return as_value_bool(false);
            }
            
            as_node_identifier_t* id = (as_node_identifier_t*)exists->identifier;
            /* Use a non-retaining check - just see if the variable exists */
            as_value_t* val = as_scope_get(scope, id->name);
            bool exists_flag = (val != NULL);
            /* Don't release - as_scope_get doesn't retain */
            
            return as_value_bool(exists_flag);
        }
        
        case NODE_MATCH: {
            as_node_match_t* match = (as_node_match_t*)node;
            
            /* Evaluate the expression to match against */
            as_value_t* about_val = eval_node(interp, scope, match->about);
            as_value_t* match_result = NULL;
            if (interp->aborted) {
                return about_val ? about_val : as_value_null();
            }
            
            /* Check each case */
            for (size_t i = 0; i < match->case_count; i++) {
                as_value_t* case_val = eval_node(interp, scope, match->cases[i].q);
                if (interp->aborted) {
                    as_value_release(about_val);
                    return case_val ? case_val : as_value_null();
                }
                
                /* Check for equality */
                bool matches = false;
                if (about_val->type == case_val->type) {
                    switch (about_val->type) {
                        case AS_VALUE_NULL:
                            matches = true;
                            break;
                        case AS_VALUE_BOOL:
                            matches = (about_val->data.bool_val == case_val->data.bool_val);
                            break;
                        case AS_VALUE_NUM:
                            matches = (about_val->data.num_val == case_val->data.num_val);
                            break;
                        case AS_VALUE_STR:
                            matches = (strcmp(about_val->data.str_val, case_val->data.str_val) == 0);
                            break;
                        default:
                            /* For other types, just check pointer equality */
                            matches = (about_val == case_val);
                            break;
                    }
                }
                
                as_value_release(case_val);
                
                if (matches) {
                    as_value_release(about_val);
                    match_result = eval_node(interp, scope, match->cases[i].a);
                    goto match_done;
                }
            }
            
            /* No case matched, execute default if present */
            as_value_release(about_val);
            match_result = match->default_case
                         ? eval_node(interp, scope, match->default_case)
                         : as_value_null();
            
            match_done:
            /* Consume labeled break targeting this match expression */
            if (interp->break_flag && match->label && interp->break_label &&
                strcmp(interp->break_label, match->label) == 0) {
                interp->break_flag = false;
                as_free(interp->break_label);
                interp->break_label = NULL;
                as_value_release(match_result);
                if (interp->break_value) {
                    match_result = interp->break_value;
                    interp->break_value = NULL;
                } else {
                    match_result = as_value_null();
                }
            }
            
            return match_result;
        }
            
        case NODE_NUM: {
            as_node_num_t* num_node = (as_node_num_t*)node;
            return as_value_num(num_node->value);
        }
            
        case NODE_STR: {
            as_node_str_t* str_node = (as_node_str_t*)node;
            return as_value_str(str_node->value);
        }
            
        case NODE_TMPL: {
            as_node_tmpl_t* tmpl_node = (as_node_tmpl_t*)node;
            
            /* Build result string by concatenating all values */
            char* result = as_malloc(1);
            result[0] = '\0';
            size_t result_len = 0;
            
            for (size_t i = 0; i < tmpl_node->value_count; i++) {
                as_node_t* value_node = tmpl_node->values[i];
                
                /* If it's a string node, use its value directly */
                if (value_node->type == NODE_STR) {
                    as_node_str_t* str_node = (as_node_str_t*)value_node;
                    size_t str_len = strlen(str_node->value);
                    result = as_realloc(result, result_len + str_len + 1);
                    strcpy(result + result_len, str_node->value);
                    result_len += str_len;
                } else {
                    /* Evaluate expression and convert to string */
                    as_value_t* val = eval_node(interp, scope, value_node);
                    if (interp->aborted) {
                        as_free(result);
                        return val ? val : as_value_null();
                    }
                    char* val_str = as_value_to_string(val);
                    
                    /* Remove quotes from strings in template context */
                    char* display_str = val_str;
                    if (val->type == AS_VALUE_STR) {
                        /* Use raw string value */
                        display_str = val->data.str_val;
                    }
                    
                    size_t val_len = strlen(display_str);
                    result = as_realloc(result, result_len + val_len + 1);
                    strcpy(result + result_len, display_str);
                    result_len += val_len;
                    
                    if (val->type != AS_VALUE_STR) {
                        as_free(val_str);
                    }
                    as_value_release(val);
                }
            }
            
            as_value_t* result_val = as_value_str(result);
            as_free(result);
            return result_val;
        }
            
        case NODE_ARR: {
            as_node_arr_t* arr_node = (as_node_arr_t*)node;
            as_value_t* arr = as_value_arr();
            for (size_t i = 0; i < arr_node->element_count; i++) {
                as_value_t* elem = eval_node(interp, scope, arr_node->elements[i]);
                if (interp->aborted) {
                    as_value_release(arr);
                    return elem ? elem : as_value_null();
                }
                as_arr_push(arr->data.arr_val, elem);
                as_value_release(elem);
            }
            return arr;
        }
            
        case NODE_OBJ: {
            as_node_obj_t* obj_node = (as_node_obj_t*)node;
            as_value_t* obj = as_value_obj();
            for (size_t i = 0; i < obj_node->entry_count; i++) {
                as_value_t* val = eval_node(interp, scope, obj_node->entries[i].value);
                if (interp->aborted) {
                    as_value_release(obj);
                    return val ? val : as_value_null();
                }
                as_obj_set(obj->data.obj_val, obj_node->entries[i].key, val);
                as_value_release(val);
            }
            return obj;
        }
            
        case NODE_IDENTIFIER: {
            as_node_identifier_t* id = (as_node_identifier_t*)node;
            
            /* Check for namespaced identifier */
            if (id->namespace) {
                /* Look up namespaced identifier in scope (e.g., Math:PI or Ui:C:button) */
                size_t ns_name_len = strlen(id->namespace) + strlen(id->name) + 2;
                char* ns_name = (char*)as_malloc(ns_name_len);
                snprintf(ns_name, ns_name_len, "%s:%s", id->namespace, id->name);
                
                as_value_t* value = as_scope_get(scope, ns_name);
                as_free(ns_name);
                
                if (value) {
                    return as_value_retain(value);
                }
                
                /* Python-like error: AttributeError with suggestions */
                as_traceback_push(interp->current_file, node->loc.start.line, 0, "<module>");
                
                /* Try to suggest similar namespace or function name */
                const char* known_namespaces[] = {"Core", "Math", "Date", "Arr", "Str", "Obj", "Uri", "Num", "Json", "Error", "Util"};
                
                /* First check if namespace is actually valid (exact match) */
                bool namespace_exists = false;
                for (size_t i = 0; i < 11; i++) {
                    if (strcmp(id->namespace, known_namespaces[i]) == 0) {
                        namespace_exists = true;
                        break;
                    }
                }
                
                if (!namespace_exists) {
                    /* Namespace is wrong, suggest a correct one */
                    const char* ns_suggestion = as_suggest_closest(id->namespace, known_namespaces, 11);
                    if (ns_suggestion) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "namespace '%s' has no attribute '%s'", id->namespace, id->name);
                        as_error_report_with_suggestion((as_error_type_t)AS_ERROR_PYTHONIC_ATTRIBUTE, msg, ns_suggestion);
                    } else {
                        /* No good namespace suggestion */
                        as_error_attribute(id->namespace, id->name);
                    }
                } else {
                    /* Namespace is correct, try suggesting function names based on namespace */
                    const char* func_suggestion = NULL;
                    
                    /* Common Core functions */
                    if (strcmp(id->namespace, "Core") == 0) {
                        const char* core_funcs[] = {"type", "sleep", "modpath", "metadata", "unload", "reload"};
                        func_suggestion = as_suggest_closest(id->name, core_funcs, 6);
                    }
                    /* Math functions */
                    else if (strcmp(id->namespace, "Math") == 0) {
                        const char* math_funcs[] = {"sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "abs", "floor", "ceil", "round", "min", "max", "pow", "exp", "log", "rnd", "gen_rng", "PI", "E"};
                        func_suggestion = as_suggest_closest(id->name, math_funcs, 20);
                    }
                    /* Date functions */
                    else if (strcmp(id->namespace, "Date") == 0) {
                        const char* date_funcs[] = {"now", "year", "month", "day", "hour", "minute", "second", "parse", "to_iso_str"};
                        func_suggestion = as_suggest_closest(id->name, date_funcs, 9);
                    }
                    /* Arr functions */
                    else if (strcmp(id->namespace, "Arr") == 0) {
                        const char* arr_funcs[] = {"create"};
                        func_suggestion = as_suggest_closest(id->name, arr_funcs, 1);
                    }
                    /* Str functions */
                    else if (strcmp(id->namespace, "Str") == 0) {
                        const char* str_funcs[] = {"lf", "from_codepoint", "from_unicode_codepoints", "from_utf8_bytes"};
                        func_suggestion = as_suggest_closest(id->name, str_funcs, 4);
                    }
                    /* If no function-level suggestions available, just report the error */
                    
                    if (func_suggestion) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "namespace '%s' has no attribute '%s'", id->namespace, id->name);
                        as_error_report_with_suggestion((as_error_type_t)AS_ERROR_PYTHONIC_ATTRIBUTE, msg, func_suggestion);
                    } else {
                        /* No suggestion found, just report error without suggestion */
                        as_error_attribute(id->namespace, id->name);
                    }
                }
                
                as_traceback_pop();
                
                /* Return error value to prevent cascading errors */
                return as_value_error("AttributeError", as_value_null());
            }
            
            /* Regular identifier lookup */
            as_value_t* val = as_scope_get(scope, id->name);
            if (!val) {
                /* Python-like error: NameError with suggestions for built-in functions */
                as_traceback_push(interp->current_file, node->loc.start.line, 0, "<module>");
                
                /* Try to suggest similar built-in function names */
                const char* builtin_funcs[] = {"print", "readline", "eval", "exists"};
                const char* suggestion = as_suggest_closest(id->name, builtin_funcs, 4);
                
                if (suggestion) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "name '%s' is not defined", id->name);
                    as_error_report_with_suggestion((as_error_type_t)AS_ERROR_PYTHONIC_NAME, msg, suggestion);
                } else {
                    as_error_name(id->name);
                }
                
                as_traceback_pop();
                
                /* Return error value to prevent cascading errors */
                return as_value_error("NameError", as_value_null());
            }
            return as_value_retain(val);
        }
            
        case NODE_DEF: {
            as_node_def_t* def = (as_node_def_t*)node;

            /* Attribute values are expressions and must be evaluated (AiScript 1.2.1),
             * so that errors in them surface even though scripts cannot read them. */
            for (size_t a = 0; a < def->attr_count; a++) {
                if (def->attrs[a] && def->attrs[a]->value) {
                    as_value_t* attr_val = eval_node(interp, scope, def->attrs[a]->value);
                    if (interp->aborted) {
                        return attr_val ? attr_val : as_value_null();
                    }
                    if (attr_val && attr_val->type == AS_VALUE_ERROR) {
                        return attr_val;
                    }
                    as_value_release(attr_val);
                }
            }

            as_value_t* val = eval_node(interp, scope, def->expr);
            if (interp->aborted) {
                return val ? val : as_value_null();
            }
            
            /* Propagate error values immediately */
            if (val && val->type == AS_VALUE_ERROR) {
                return val;
            }
            
            destructure_def(interp, scope, def->dest, val, def->is_mut);
            
            as_value_release(val);
            return as_value_null();
        }
            
        case NODE_ASSIGN: {
            as_node_assign_t* assign = (as_node_assign_t*)node;
            return eval_assign_op(interp, scope, assign->dest, assign->expr, 0, 0);
        }
            
        case NODE_ADD_ASSIGN:
        case NODE_SUB_ASSIGN: {
            as_node_assign_t* assign = (as_node_assign_t*)node;
            return eval_assign_op(interp, scope, assign->dest, assign->expr, 1,
                                  node->type == NODE_SUB_ASSIGN);
        }
            
        case NODE_BLOCK: {
            as_node_block_t* block = (as_node_block_t*)node;
            as_value_t* result = as_value_null();
            
            /* Execute all statements, keeping only the last value */
            for (size_t i = 0; i < block->statement_count; i++) {
                as_value_release(result);
                result = eval_node(interp, scope, block->statements[i]);
                
                /* Check for early exit (break, continue, return) */
                if (interp->break_flag || interp->continue_flag || interp->aborted) {
                    break;
                }
            }
            
            /* If a labeled break targets this block, consume it and return break_value */
            if (interp->break_flag && block->label && interp->break_label &&
                strcmp(interp->break_label, block->label) == 0) {
                interp->break_flag = false;
                as_free(interp->break_label);
                interp->break_label = NULL;
                as_value_release(result);
                if (interp->break_value) {
                    result = interp->break_value;
                    interp->break_value = NULL;
                } else {
                    result = as_value_null();
                }
            }
            
            return result;
        }
            
        case NODE_IF: {
            as_node_if_t* if_node = (as_node_if_t*)node;
            as_value_t* cond = eval_node(interp, scope, if_node->cond);
            bool cond_bool;
            if (interp->aborted) {
                return cond ? cond : as_value_null();
            }
            cond_bool = as_value_to_bool(cond);
            as_value_release(cond);
            
            as_value_t* result = NULL;
            if (cond_bool) {
                result = eval_node(interp, scope, if_node->then_node);
            } else {
                /* Check elif branches */
                bool elif_matched = false;
                for (size_t i = 0; i < if_node->elseif_count; i++) {
                    as_value_t* elif_cond = eval_node(interp, scope, if_node->elseif_conds[i]);
                    bool elif_bool;
                    if (interp->aborted) {
                        return elif_cond ? elif_cond : as_value_null();
                    }
                    elif_bool = as_value_to_bool(elif_cond);
                    as_value_release(elif_cond);
                    if (elif_bool) {
                        result = eval_node(interp, scope, if_node->elseif_then[i]);
                        elif_matched = true;
                        break;
                    }
                }
                if (!elif_matched) {
                    result = if_node->else_node
                           ? eval_node(interp, scope, if_node->else_node)
                           : as_value_null();
                }
            }
            
            /* Consume labeled break targeting this if expression */
            if (interp->break_flag && if_node->label && interp->break_label &&
                strcmp(interp->break_label, if_node->label) == 0) {
                interp->break_flag = false;
                as_free(interp->break_label);
                interp->break_label = NULL;
                as_value_release(result);
                if (interp->break_value) {
                    result = interp->break_value;
                    interp->break_value = NULL;
                } else {
                    result = as_value_null();
                }
            }
            
            return result;
        }
            
        case NODE_FN: {
            as_node_fn_t* fn_node = (as_node_fn_t*)node;
            size_t pi;
            /* Default-parameter expressions are evaluated when the function is created. */
            for (pi = 0; pi < fn_node->param_count; pi++) {
                if (fn_node->params[pi].default_value) {
                    as_value_t* d = eval_node(interp, scope, fn_node->params[pi].default_value);
                    if (interp->aborted) {
                        return d ? d : as_value_null();
                    }
                    as_value_release(d);
                }
            }
            as_scope_retain(scope);
            return as_value_fn_user(fn_node->params, fn_node->param_count,
                                   fn_node->statements, fn_node->statement_count,
                                   scope);
        }
            
        case NODE_RETURN: {
            as_node_return_t* ret_node = (as_node_return_t*)node;
            /* Evaluate return expression first, then mark as aborted */
            as_value_t* ret_val = eval_node(interp, scope, ret_node->expr);
            interp->aborted = true;
            return ret_val;
        }
            
        case NODE_LOOP: {
            as_node_loop_t* loop_node = (as_node_loop_t*)node;
            as_value_t* result = as_value_null();
            
            /* Execute loop until break */
            while (true) {
                /* Execute all statements in loop body */
                for (size_t i = 0; i < loop_node->statement_count; i++) {
                    as_value_release(result);
                    result = eval_node(interp, scope, loop_node->statements[i]);
                    
                    /* Check for break */
                    if (interp->break_flag) {
                        /* Check if this break is for this loop (or unlabeled) */
                        if (!interp->break_label || 
                            (loop_node->label && strcmp(interp->break_label, loop_node->label) == 0)) {
                            interp->break_flag = false;
                            if (interp->break_label) {
                                as_free(interp->break_label);
                                interp->break_label = NULL;
                            }
                            if (interp->break_value) {
                                as_value_release(result);
                                result = interp->break_value;
                                interp->break_value = NULL;
                            }
                            return result;
                        }
                        /* Labeled break for outer loop, propagate upward */
                        return result;
                    }
                    
                    /* Check for continue */
                    if (interp->continue_flag) {
                        if (continue_targets_here(interp, loop_node->label)) {
                            clear_continue(interp);
                            break; /* next iteration of infinite loop */
                        }
                        return result; /* labeled continue for an outer loop */
                    }
                    
                    /* Check for return */
                    if (interp->aborted) {
                        return result;
                    }
                }
            }
            return result;
        }
            
        case NODE_WHILE: {
            as_node_while_t* while_node = (as_node_while_t*)node;
            as_value_t* result = as_value_null();
            
            /* Loop while condition is true */
            while (true) {
                /* Evaluate condition */
                as_value_t* cond = eval_node(interp, scope, while_node->cond);
                bool cond_bool;
                if (interp->aborted) {
                    as_value_release(result);
                    return cond ? cond : as_value_null();
                }
                cond_bool = as_value_to_bool(cond);
                as_value_release(cond);
                
                if (!cond_bool) break;
                
                /* Execute body */
                as_value_release(result);
                result = eval_node(interp, scope, while_node->body);
                
                /* Check for break, continue, return */
                if (interp->break_flag) {
                    if (!interp->break_label || 
                        (while_node->label && strcmp(interp->break_label, while_node->label) == 0)) {
                        interp->break_flag = false;
                        if (interp->break_label) {
                            as_free(interp->break_label);
                            interp->break_label = NULL;
                        }
                        if (interp->break_value) {
                            as_value_release(result);
                            result = interp->break_value;
                            interp->break_value = NULL;
                        }
                    }
                    break;
                }
                if (interp->continue_flag) {
                    if (continue_targets_here(interp, while_node->label)) {
                        clear_continue(interp);
                        continue;
                    }
                    break;
                }
                if (interp->aborted) { break; }
            }
            
            return result;
        }
        
        case NODE_DO_WHILE: {
            as_node_do_while_t* do_while_node = (as_node_do_while_t*)node;
            as_value_t* result = as_value_null();
            
            /* Execute at least once */
            do {
                /* Execute body */
                as_value_release(result);
                result = eval_node(interp, scope, do_while_node->body);
                
                /* Check for break, continue, return */
                if (interp->break_flag) {
                    if (!interp->break_label || 
                        (do_while_node->label && strcmp(interp->break_label, do_while_node->label) == 0)) {
                        interp->break_flag = false;
                        if (interp->break_label) {
                            as_free(interp->break_label);
                            interp->break_label = NULL;
                        }
                        if (interp->break_value) {
                            as_value_release(result);
                            result = interp->break_value;
                            interp->break_value = NULL;
                        }
                    }
                    break;
                }
                if (interp->continue_flag) {
                    if (continue_targets_here(interp, do_while_node->label)) {
                        clear_continue(interp);
                        continue;
                    }
                    break;
                }
                if (interp->aborted) { break; }
                
                /* Evaluate condition */
                as_value_t* cond = eval_node(interp, scope, do_while_node->cond);
                bool cond_bool;
                if (interp->aborted) {
                    as_value_release(result);
                    return cond ? cond : as_value_null();
                }
                cond_bool = as_value_to_bool(cond);
                as_value_release(cond);
                
                if (!cond_bool) break;
            } while (true);
            
            return result;
        }
        
        case NODE_BREAK: {
            as_node_break_t* break_node = (as_node_break_t*)node;
            interp->break_flag = true;
            if (interp->break_label) as_free(interp->break_label);
            interp->break_label = break_node->label ? as_strdup(break_node->label) : NULL;
            /* Evaluate and store the optional value expression */
            if (interp->break_value) {
                as_value_release(interp->break_value);
                interp->break_value = NULL;
            }
            if (break_node->expr) {
                interp->break_value = eval_node(interp, scope, break_node->expr);
            }
            return as_value_null();
        }
        
        case NODE_CONTINUE: {
            as_node_continue_t* continue_node = (as_node_continue_t*)node;
            interp->continue_flag = true;
            if (interp->continue_label) as_free(interp->continue_label);
            interp->continue_label = continue_node->label ? as_strdup(continue_node->label) : NULL;
            return as_value_null();
        }
            
        case NODE_EACH: {
            as_node_each_t* each_node = (as_node_each_t*)node;
            as_value_t* result = as_value_null();
            
            /* Evaluate the array */
            as_value_t* items_val = eval_node(interp, scope, each_node->items);
            if (interp->aborted) {
                as_value_release(result);
                return items_val ? items_val : as_value_null();
            }
            
            if (!items_val || items_val->type != AS_VALUE_ARR) {
                as_value_release(items_val);
                fprintf(stderr, "Runtime error: Each loop requires an array\n");
                return as_value_null();
            }
            
            /* Create scope for loop variable / destructuring */
            as_scope_t* loop_scope = as_scope_create_child(scope, "<each>");
            const char* var_name = NULL;
            if (each_node->var && each_node->var->type == NODE_IDENTIFIER) {
                as_node_identifier_t* var_id = (as_node_identifier_t*)each_node->var;
                var_name = var_id->name;
                as_value_t* init_val = as_value_null();
                as_scope_add(loop_scope, var_name, init_val, true);
            }
            
            /* Iterate over array items */
            for (size_t i = 0; i < items_val->data.arr_val->length; i++) {
                as_value_t* item = items_val->data.arr_val->items[i];
                as_scope_t* iteration_scope;
                
                if (var_name) {
                    as_scope_assign(loop_scope, var_name, item);
                    iteration_scope = as_scope_create_child(loop_scope, "<each_iter>");
                } else {
                    iteration_scope = as_scope_create_child(loop_scope, "<each_iter>");
                    destructure_def(interp, iteration_scope, each_node->var, item, true);
                }
                as_value_release(result);
                result = eval_node(interp, iteration_scope, each_node->body);
                as_scope_release(iteration_scope);
                
                /* Check for break */
                if (interp->break_flag) {
                    if (!interp->break_label || 
                        (each_node->label && strcmp(interp->break_label, each_node->label) == 0)) {
                        interp->break_flag = false;
                        if (interp->break_label) {
                            as_free(interp->break_label);
                            interp->break_label = NULL;
                        }
                        if (interp->break_value) {
                            as_value_release(result);
                            result = interp->break_value;
                            interp->break_value = NULL;
                        }
                    }
                    /* Propagate upward if labeled break for outer loop */
                    break;
                }
                
                /* Check for continue */
                if (interp->continue_flag) {
                    if (continue_targets_here(interp, each_node->label)) {
                        clear_continue(interp);
                        continue;
                    }
                    break;
                }
                
                /* Check for return */
                if (interp->aborted) {
                    break;
                }
            }
            
            as_scope_release(loop_scope);
            as_value_release(items_val);
            return result;
        }
            
        case NODE_FOR: {
            as_node_for_t* for_node = (as_node_for_t*)node;
            as_value_t* result = as_value_null();
            
            /* Check if this is range or times variant */
            if (for_node->var != NULL) {
                /* Range variant: for (let i = from, to) */
                as_value_t* from_val = eval_node(interp, scope, for_node->from);
                as_value_t* to_val;
                if (interp->aborted) {
                    as_value_release(result);
                    return from_val ? from_val : as_value_null();
                }
                to_val = eval_node(interp, scope, for_node->to);
                if (interp->aborted) {
                    as_value_release(from_val);
                    as_value_release(result);
                    return to_val ? to_val : as_value_null();
                }
                
                if (!from_val || !to_val ||
                    from_val->type != AS_VALUE_NUM || to_val->type != AS_VALUE_NUM) {
                    as_value_release(from_val);
                    as_value_release(to_val);
                    fprintf(stderr, "Runtime error: For loop range must be numbers\n");
                    return as_value_null();
                }
                
                double from = from_val->data.num_val;
                double to = to_val->data.num_val;
                as_value_release(from_val);
                as_value_release(to_val);
                
                /* Create scope for loop variable */
                as_scope_t* loop_scope = as_scope_create_child(scope, "<for>");
                
                /* Add loop variable to scope as mutable (var) */
                as_value_t* init_val = as_value_num(from);
                as_scope_add(loop_scope, for_node->var, init_val, true);
                
                /* Execute loop from..from+count (second operand is iteration count) */
                for (double i = from; i < from + to; i++) {
                    /* Update loop variable */
                    as_value_t* i_val = as_value_num(i);
                    as_scope_assign(loop_scope, for_node->var, i_val);
                    
                    /* Execute body */
                    as_scope_t* iteration_scope = as_scope_create_child(loop_scope, "<for_iter>");
                    as_value_release(result);
                    result = eval_node(interp, iteration_scope, for_node->body);
                    as_scope_release(iteration_scope);
                    
                    /* Check for break */
                    if (interp->break_flag) {
                        if (!interp->break_label || 
                            (for_node->label && strcmp(interp->break_label, for_node->label) == 0)) {
                            interp->break_flag = false;
                            if (interp->break_label) {
                                as_free(interp->break_label);
                                interp->break_label = NULL;
                            }
                            if (interp->break_value) {
                                as_value_release(result);
                                result = interp->break_value;
                                interp->break_value = NULL;
                            }
                        }
                        /* Propagate upward if labeled break for outer loop */
                        break;
                    }
                    
                    /* Check for continue */
                    if (interp->continue_flag) {
                        if (continue_targets_here(interp, for_node->label)) {
                            clear_continue(interp);
                            continue;
                        }
                        break;
                    }
                    
                    /* Check for return */
                    if (interp->aborted) {
                        break;
                    }
                }
                
                as_scope_release(loop_scope);
            } else {
                /* Times variant: for (n) */
                as_value_t* times_val = eval_node(interp, scope, for_node->times);
                if (interp->aborted) {
                    as_value_release(result);
                    return times_val ? times_val : as_value_null();
                }
                
                if (!times_val || times_val->type != AS_VALUE_NUM) {
                    as_value_release(times_val);
                    fprintf(stderr, "Runtime error: For loop count must be a number\n");
                    return as_value_null();
                }
                
                double times = times_val->data.num_val;
                as_value_release(times_val);
                
                /* Execute loop times */
                for (double i = 0; i < times; i++) {
                    as_scope_t* iteration_scope = as_scope_create_child(scope, "<for_iter>");
                    as_value_release(result);
                    result = eval_node(interp, iteration_scope, for_node->body);
                    as_scope_release(iteration_scope);
                    
                    /* Check for break */
                    if (interp->break_flag) {
                        if (!interp->break_label || 
                            (for_node->label && strcmp(interp->break_label, for_node->label) == 0)) {
                            interp->break_flag = false;
                            if (interp->break_label) {
                                as_free(interp->break_label);
                                interp->break_label = NULL;
                            }
                            if (interp->break_value) {
                                as_value_release(result);
                                result = interp->break_value;
                                interp->break_value = NULL;
                            }
                        }
                        /* Propagate upward if labeled break for outer loop */
                        break;
                    }
                    
                    /* Check for continue */
                    if (interp->continue_flag) {
                        if (continue_targets_here(interp, for_node->label)) {
                            clear_continue(interp);
                            continue;
                        }
                        break;
                    }
                    
                    /* Check for return */
                    if (interp->aborted) {
                        break;
                    }
                }
            }
            
            return result;
        }
            
        case NODE_CALL: {
            as_node_call_t* call = (as_node_call_t*)node;
            as_value_t* fn = eval_node(interp, scope, call->target);
            if (interp->aborted) {
                return fn ? fn : as_value_null();
            }
            if (!fn) {
                return as_value_null();
            }
            
            if (fn->type != AS_VALUE_FN) {
                /* Propagate error if fn is already an error value (prevents cascading errors) */
                if (fn->type == AS_VALUE_ERROR) {
                    return fn;  /* Return the error, don't convert to null! */
                }
                
                /* Python-like error: TypeError */
                as_traceback_push(interp->current_file, node->loc.start.line, 0, "<module>");
                
                /* Get type name for error message */
                const char* type_names[] = {"null", "bool", "num", "str", "arr", "obj", "fn", "error"};
                const char* actual_type = (fn->type >= 0 && fn->type < 8) ? type_names[fn->type] : "unknown";
                
                char message[256];
                snprintf(message, sizeof(message), "'%s' object is not callable", actual_type);
                as_error_report(AS_ERROR_TYPE, message);
                
                as_traceback_pop();
                as_value_release(fn);
                return as_value_error("TypeError", as_value_null());
            }
            
            /* Evaluate arguments */
            as_value_t** args = as_malloc(sizeof(as_value_t*) * call->arg_count);
            for (size_t i = 0; i < call->arg_count; i++) {
                args[i] = eval_node(interp, scope, call->args[i]);
                if (interp->aborted) {
                    as_value_t* ret = args[i] ? args[i] : as_value_null();
                    size_t j;
                    for (j = 0; j < i; j++) {
                        as_value_release(args[j]);
                    }
                    as_free(args);
                    as_value_release(fn);
                    return ret;
                }
            }
            
            as_value_t* result = NULL;
            
            if (fn->data.fn_val->kind == AS_FN_NATIVE) {
                /* Call native function */
                result = fn->data.fn_val->native_fn(args, call->arg_count,
                                                    fn->data.fn_val->userdata);
            } else {
                /* Call user function */
                as_scope_t* fn_scope = as_scope_create_child(fn->data.fn_val->closure_scope, "<function>");
                
                /* Bind parameters */
                for (size_t i = 0; i < fn->data.fn_val->param_count; i++) {
                    as_value_t* param_value = NULL;
                    if (i < call->arg_count) {
                        param_value = as_value_retain(args[i]);
                    } else if (fn->data.fn_val->params[i].default_value) {
                        param_value = eval_node(interp, fn_scope, fn->data.fn_val->params[i].default_value);
                        if (interp->aborted) {
                            as_scope_release(fn_scope);
                            as_value_release(fn);
                            {
                                size_t j;
                                for (j = 0; j < call->arg_count; j++) {
                                    as_value_release(args[j]);
                                }
                            }
                            as_free(args);
                            return param_value ? param_value : as_value_null();
                        }
                    } else {
                        param_value = as_value_null();
                    }
                    if (fn->data.fn_val->params[i].dest->type == NODE_IDENTIFIER) {
                        as_node_identifier_t* param_id =
                            (as_node_identifier_t*)fn->data.fn_val->params[i].dest;
                        as_scope_add(fn_scope, param_id->name, param_value, false);
                    } else if (fn->data.fn_val->params[i].dest) {
                        destructure_def(interp, fn_scope, fn->data.fn_val->params[i].dest,
                                        param_value, false);
                    }
                    as_value_release(param_value);
                }
                
                /* Save abort state and execute function body */
                bool prev_aborted = interp->aborted;
                interp->aborted = false;
                
                result = as_value_null();
                for (size_t i = 0; i < fn->data.fn_val->statement_count; i++) {
                    as_value_release(result);
                    result = eval_node(interp, fn_scope, fn->data.fn_val->statements[i]);
                    
                    /* Check for early return. `aborted` doubles as the return flag, so
                     * it is reset per function unless a runtime error is pending. */
                    if (interp->aborted) {
                        if (!interp->error_message) {
                            interp->aborted = false;
                        }
                        break;
                    }
                }
                
                /* Restore previous abort state; keep unwinding after a runtime error */
                interp->aborted = prev_aborted || interp->error_message != NULL;
                
                as_scope_release(fn_scope);
            }
            
            /* Release arguments and function */
            for (size_t i = 0; i < call->arg_count; i++) {
                as_value_release(args[i]);
            }
            as_free(args);
            as_value_release(fn);
            
            return result ? result : as_value_null();
        }
            
        case NODE_PROP: {
            as_node_prop_t* prop = (as_node_prop_t*)node;
            as_value_t* target = eval_node(interp, scope, prop->target);
            if (interp->aborted) {
                return target ? target : as_value_null();
            }
            if (!target) {
                return as_value_null();
            }
            
            if (target->type == AS_VALUE_OBJ) {
                /* Object property access */
                as_value_t* val = as_obj_get(target->data.obj_val, prop->name);
                as_value_t* out = val ? as_value_retain(val) : as_value_null();
                as_value_release(target);
                return out;
            } else {
                /* Built-in property access */
                as_value_t* val = as_get_prim_prop(interp, target, prop->name);
                as_value_release(target);
                return val ? val : as_value_null();  /* NULL: error raised, interp->aborted is set */
            }
        }
            
        case NODE_INDEX: {
            as_node_index_t* index = (as_node_index_t*)node;
            as_value_t* target = eval_node(interp, scope, index->target);
            as_value_t* idx;
            if (interp->aborted) {
                return target ? target : as_value_null();
            }
            idx = eval_node(interp, scope, index->index);
            if (interp->aborted) {
                as_value_release(target);
                return idx ? idx : as_value_null();
            }
            if (!target || !idx) {
                as_value_release(target);
                as_value_release(idx);
                return as_value_null();
            }
            
            if (target->type == AS_VALUE_ARR && idx->type == AS_VALUE_NUM) {
                int i = (int)idx->data.num_val;
                as_arr_t* arr = target->data.arr_val;
                
                /* Check for valid index */
                if (i < 0 || i >= (int)arr->length) {
                    /* Python-like error: IndexError */
                    as_traceback_push("<script>", node->loc.start.line, 0, "<module>");
                    
                    char message[256];
                    snprintf(message, sizeof(message), "list index out of range (index: %d, length: %zu)", 
                            i, arr->length);
                    as_error_report(AS_ERROR_PYTHONIC_INDEX, message);
                    
                    as_traceback_pop();
                    as_value_release(target);
                    as_value_release(idx);
                    return as_value_null();
                }
                
                as_value_t* val = as_arr_get(arr, (size_t)i);
                as_value_t* out = val ? as_value_retain(val) : as_value_null();
                as_value_release(target);
                as_value_release(idx);
                return out;
            }

            if (target->type == AS_VALUE_OBJ) {
                char* key = (idx->type == AS_VALUE_STR)
                    ? as_strdup(idx->data.str_val)
                    : as_value_to_string(idx);
                as_value_t* val = as_obj_get(target->data.obj_val, key);
                as_value_t* out = val ? as_value_retain(val) : as_value_null();
                as_free(key);
                as_value_release(target);
                as_value_release(idx);
                return out;
            }
            
            as_value_release(target);
            as_value_release(idx);
            return as_value_null();
        }
            
        /* Binary operations */
        case NODE_ADD:
        case NODE_SUB:
        case NODE_MUL:
        case NODE_DIV:
        case NODE_REM:
        case NODE_POW:
        case NODE_LT:
        case NODE_LTEQ:
        case NODE_GT:
        case NODE_GTEQ:
        case NODE_EQ:
        case NODE_NEQ:
        case NODE_AND:
        case NODE_OR: {
            as_node_binary_t* bin = (as_node_binary_t*)node;
            return eval_binary_op(interp, scope, node->type, bin->left, bin->right);
        }
            
        /* Unary operations */
        case NODE_PLUS:
        case NODE_MINUS:
        case NODE_NOT: {
            as_node_unary_t* unary = (as_node_unary_t*)node;
            return eval_unary_op(interp, scope, node->type, unary->expr);
        }
            
        default:
            return as_value_null();
    }
}

/* Public API */
as_interpreter_t* as_interpreter_new_ex(const as_interp_opts_t* opts) {
    as_interpreter_t* interp = as_malloc(sizeof(as_interpreter_t));
    interp->global_scope = as_scope_new(NULL, "<global>");
    interp->error_message = NULL;
    interp->aborted = false;
    interp->break_flag = false;
    interp->continue_flag = false;
    interp->break_label = NULL;
    interp->continue_label = NULL;
    interp->break_value = NULL;
    interp->module_cache = NULL;
    interp->current_file = "<script>";

    /* Copy embedding callbacks (NULL opts = use defaults) */
    if (opts) {
        interp->out_fn      = opts->out;
        interp->err_fn      = opts->err;
        interp->in_fn       = opts->in;
        interp->cb_userdata = opts->userdata;
    } else {
        interp->out_fn      = NULL;
        interp->err_fn      = NULL;
        interp->in_fn       = NULL;
        interp->cb_userdata = NULL;
    }

    /* Register standard library */
    as_interpreter_register_std(interp);

    return interp;
}

as_interpreter_t* as_interpreter_new(void) {
    return as_interpreter_new_ex(NULL);
}

/* Call an AiScript function value directly from C.
 * This replicates the NODE_CALL dispatch logic without needing an AST node. */
as_value_t* as_interpreter_call_value(as_interpreter_t* interp,
                                       as_value_t* fn_val,
                                       as_value_t** args,
                                       size_t arg_count) {
    if (!interp || !fn_val || fn_val->type != AS_VALUE_FN) {
        if (interp && !interp->error_message) {
            interp->error_message = as_strdup("as_interpreter_call_value: not a function");
        }
        return NULL;
    }

    as_fn_t* fn = fn_val->data.fn_val;
    as_value_t* result = NULL;

    if (fn->kind == AS_FN_NATIVE) {
        result = fn->native_fn(args, arg_count, fn->userdata);
    } else {
        as_scope_t* fn_scope = as_scope_create_child(fn->closure_scope, "<function>");

        for (size_t i = 0; i < fn->param_count; i++) {
            if (fn->params[i].dest->type == NODE_IDENTIFIER) {
                as_node_identifier_t* param_id = (as_node_identifier_t*)fn->params[i].dest;
                as_value_t* param_value = NULL;

                if (i < arg_count) {
                    param_value = as_value_retain(args[i]);
                } else if (fn->params[i].default_value) {
                    param_value = eval_node(interp, fn_scope, fn->params[i].default_value);
                } else {
                    param_value = as_value_null();
                }

                as_scope_add(fn_scope, param_id->name, param_value, false);
                as_value_release(param_value);
            }
        }

        bool prev_aborted = interp->aborted;
        interp->aborted = false;

        result = as_value_null();
        for (size_t i = 0; i < fn->statement_count; i++) {
            as_value_release(result);
            result = eval_node(interp, fn_scope, fn->statements[i]);
            if (interp->aborted) {
                if (!interp->error_message) {
                    interp->aborted = false;
                }
                break;
            }
        }

        interp->aborted = prev_aborted || interp->error_message != NULL;
        as_scope_release(fn_scope);
    }

    return result ? result : as_value_null();
}

void as_interpreter_free(as_interpreter_t* interp) {
    if (!interp) return;
    if (interp->global_scope) as_scope_release(interp->global_scope);
    if (interp->error_message) as_free(interp->error_message);
    if (interp->break_value) { as_value_release(interp->break_value); interp->break_value = NULL; }
    if (interp->break_label) { as_free(interp->break_label); interp->break_label = NULL; }
    if (interp->continue_label) { as_free(interp->continue_label); interp->continue_label = NULL; }
    
    /* Free module cache */
    as_module_cache_entry_t* cache = interp->module_cache;
    while (cache) {
        as_module_cache_entry_t* next = cache->next;
        if (cache->path) as_free(cache->path);
        as_free(cache);
        cache = next;
    }
    
    as_free(interp);
}

static as_interpreter_t* g_current_interp = NULL;

as_interpreter_t* as_interpreter_current(void)
{
    return g_current_interp;
}

as_value_t* as_interpreter_exec_nodes(as_interpreter_t* interp, as_node_t** nodes, size_t count) {
    as_value_t* result = as_value_null();
    as_interpreter_t* prev_interp = g_current_interp;
    size_t i;
    g_current_interp = interp;

    /* Hoist namespace declarations so `Foo:bar` works before `:: Foo { ... }`. */
    for (i = 0; i < count; i++) {
        if (nodes[i] && nodes[i]->type == NODE_NAMESPACE) {
            as_value_release(eval_node(interp, interp->global_scope, nodes[i]));
            if (interp->aborted) {
                g_current_interp = prev_interp;
                return NULL;
            }
        }
    }
    
    for (i = 0; i < count; i++) {
        if (nodes[i] && nodes[i]->type == NODE_NAMESPACE) {
            continue;
        }
        /* Reset break/continue flags between top-level statements */
        interp->break_flag = false;
        interp->continue_flag = false;
        if (interp->break_label) {
            as_free(interp->break_label);
            interp->break_label = NULL;
        }
        if (interp->continue_label) {
            as_free(interp->continue_label);
            interp->continue_label = NULL;
        }
        if (interp->break_value) {
            as_value_release(interp->break_value);
            interp->break_value = NULL;
        }
        
        /* Dispatch any pending async callbacks between top-level statements */
        as_std_async_dispatch(interp);
        
        as_value_release(result);
        result = eval_node(interp, interp->global_scope, nodes[i]);
        if (!result || interp->aborted) {
            g_current_interp = prev_interp;
            return NULL;
        }
        
        /* Stop execution if an error occurred */
        if (result && result->type == AS_VALUE_ERROR) {
            g_current_interp = prev_interp;
            return result;
        }
    }
    
    g_current_interp = prev_interp;
    return result;
}

as_value_t* as_interpreter_eval_node(as_interpreter_t* interp, as_scope_t* scope, as_node_t* node) {
    return eval_node(interp, scope, node);
}

void as_interpreter_register_std(as_interpreter_t* interp) {
    /* Register built-in functions — pass interp as userdata so I/O callbacks work */
    as_value_t* print_fn = as_value_fn_native(std_print, interp);
    as_scope_add(interp->global_scope, "print", print_fn, false);
    as_value_release(print_fn);
    
    /* Register readline function */
    as_value_t* readline_fn = as_value_fn_native(std_readline, interp);
    as_scope_add(interp->global_scope, "readline", readline_fn, false);
    as_value_release(readline_fn);
    
    /* Register Core namespace functions */
    as_std_core_register(interp);
    
    /* Register Date namespace functions */
    as_std_date_register(interp);
    
    /* Register Math namespace functions */
    as_std_math_register(interp);
    
    /* Register Util namespace functions */
    as_std_util_register(interp);
    
    /* Register Arr namespace functions */
    as_std_arr_register(interp);
    
    /* Register Uri namespace functions */
    as_std_uri_register(interp);
    
    /* Register Num namespace functions */
    as_std_num_register(interp);
    
    /* Register Json namespace functions */
    as_std_json_register(interp);
    
    /* Register Error namespace functions */
    as_std_error_register(interp);
    
    /* Register Str namespace functions */
    as_std_str_register(interp);
    
    /* Register Obj namespace functions */
    as_std_obj_register(interp);
    
    /* Register Async namespace functions */
    as_std_async_register(interp);
}


/* Helper function for native modules to get global scope */
as_scope_t* as_interpreter_get_global_scope(as_interpreter_t* interp) {
    return interp ? interp->global_scope : NULL;
}
