/*
 * AiScript Standard Library - Json Namespace Implementation
 * 
 * Simple JSON serialization and parsing
 */

#include "std_json.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef NO_JSON

/* Forward declarations for JSON parsing */
typedef struct {
    const char* json;
    size_t pos;
    size_t len;
} json_parser_t;

static as_value_t* parse_json_value(json_parser_t* parser);

/* Helper: Skip whitespace */
static void skip_whitespace(json_parser_t* parser) {
    while (parser->pos < parser->len && isspace((unsigned char)parser->json[parser->pos])) {
        parser->pos++;
    }
}

/* Helper: Escape string for JSON */
static char* escape_json_string(const char* str) {
    size_t len = strlen(str);
    /* Worst case: every char becomes \uXXXX (6 bytes) */
    char* result = (char*)as_malloc(len * 6 + 3); /* +3 for quotes and null */
    size_t out_pos = 0;
    
    result[out_pos++] = '"';
    
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        
        switch (c) {
            case '"':  result[out_pos++] = '\\'; result[out_pos++] = '"'; break;
            case '\\': result[out_pos++] = '\\'; result[out_pos++] = '\\'; break;
            case '\b': result[out_pos++] = '\\'; result[out_pos++] = 'b'; break;
            case '\f': result[out_pos++] = '\\'; result[out_pos++] = 'f'; break;
            case '\n': result[out_pos++] = '\\'; result[out_pos++] = 'n'; break;
            case '\r': result[out_pos++] = '\\'; result[out_pos++] = 'r'; break;
            case '\t': result[out_pos++] = '\\'; result[out_pos++] = 't'; break;
            default:
                if (c < 32) {
                    /* Control character - escape as \uXXXX */
                    sprintf(&result[out_pos], "\\u%04x", c);
                    out_pos += 6;
                } else {
                    result[out_pos++] = c;
                }
                break;
        }
    }
    
    result[out_pos++] = '"';
    result[out_pos] = '\0';
    
    return result;
}

/* Helper: Stringify value recursively */
static char* stringify_value(as_value_t* val) {
    char* result;
    
    switch (val->type) {
        case AS_VALUE_NULL:
            result = (char*)as_malloc(5);
            strcpy(result, "null");
            return result;
            
        case AS_VALUE_BOOL:
            if (val->data.bool_val) {
                result = (char*)as_malloc(5);
                strcpy(result, "true");
            } else {
                result = (char*)as_malloc(6);
                strcpy(result, "false");
            }
            return result;
            
        case AS_VALUE_NUM: {
            result = (char*)as_malloc(64);
            if (isnan(val->data.num_val)) {
                strcpy(result, "null");
            } else if (isinf(val->data.num_val)) {
                strcpy(result, "null");
            } else {
                sprintf(result, "%.15g", val->data.num_val);
            }
            return result;
        }
            
        case AS_VALUE_STR:
            return escape_json_string(val->data.str_val);
            
        case AS_VALUE_ARR: {
            /* Build array JSON */
            char** parts = (char**)as_malloc(val->data.arr_val->length * sizeof(char*));
            size_t total_len = 2; /* [] */
            
            for (size_t i = 0; i < val->data.arr_val->length; i++) {
                parts[i] = stringify_value(val->data.arr_val->items[i]);
                total_len += strlen(parts[i]);
                if (i > 0) total_len++; /* comma */
            }
            
            result = (char*)as_malloc(total_len + 1);
            size_t pos = 0;
            result[pos++] = '[';
            
            for (size_t i = 0; i < val->data.arr_val->length; i++) {
                if (i > 0) result[pos++] = ',';
                strcpy(&result[pos], parts[i]);
                pos += strlen(parts[i]);
                as_free(parts[i]);
            }
            
            result[pos++] = ']';
            result[pos] = '\0';
            as_free(parts);
            return result;
        }
            
        case AS_VALUE_OBJ: {
            /* Build object JSON */
            as_obj_t* obj = val->data.obj_val;
            
            if (obj->size == 0) {
                result = (char*)as_malloc(3);
                strcpy(result, "{}");
                return result;
            }
            
            /* Count and collect all entries */
            size_t count = obj->size;
            char** keys = (char**)as_malloc(count * sizeof(char*));
            char** values = (char**)as_malloc(count * sizeof(char*));
            size_t total_len = 2; /* {} */
            size_t i = 0;
            
            /* Iterate in insertion order */
            for (as_obj_entry_t* entry = obj->order_head; entry != NULL; entry = entry->order_next) {
                    keys[i] = escape_json_string(entry->key);
                    values[i] = stringify_value(entry->value);
                    total_len += strlen(keys[i]) + strlen(values[i]) + 1; /* :  */
                    if (i > 0) total_len++; /* comma */
                    i++;
            }
            
            result = (char*)as_malloc(total_len + 1);
            size_t pos = 0;
            result[pos++] = '{';
            
            for (size_t j = 0; j < count; j++) {
                if (j > 0) result[pos++] = ',';
                strcpy(&result[pos], keys[j]);
                pos += strlen(keys[j]);
                result[pos++] = ':';
                strcpy(&result[pos], values[j]);
                pos += strlen(values[j]);
                as_free(keys[j]);
                as_free(values[j]);
            }
            
            result[pos++] = '}';
            result[pos] = '\0';
            as_free(keys);
            as_free(values);
            return result;
        }
            
        default:
            /* Functions serialize as the JSON string "<function>" */
            if (val->type == AS_VALUE_FN) {
                result = (char*)as_malloc(13);
                strcpy(result, "\"<function>\"");
                return result;
            }
            result = (char*)as_malloc(5);
            strcpy(result, "null");
            return result;
    }
}

/* Parse JSON string literal */
static as_value_t* parse_json_string(json_parser_t* parser) {
    if (parser->json[parser->pos] != '"') {
        return NULL;
    }
    parser->pos++; /* Skip opening quote */
    
    size_t start = parser->pos;
    char* buffer = (char*)as_malloc(parser->len - start + 1);
    size_t buf_pos = 0;
    
    while (parser->pos < parser->len && parser->json[parser->pos] != '"') {
        if (parser->json[parser->pos] == '\\') {
            parser->pos++;
            if (parser->pos >= parser->len) {
                as_free(buffer);
                return NULL;
            }
            
            switch (parser->json[parser->pos]) {
                case '"':  buffer[buf_pos++] = '"'; break;
                case '\\': buffer[buf_pos++] = '\\'; break;
                case '/':  buffer[buf_pos++] = '/'; break;
                case 'b':  buffer[buf_pos++] = '\b'; break;
                case 'f':  buffer[buf_pos++] = '\f'; break;
                case 'n':  buffer[buf_pos++] = '\n'; break;
                case 'r':  buffer[buf_pos++] = '\r'; break;
                case 't':  buffer[buf_pos++] = '\t'; break;
                case 'u': {
                    /* Unicode escape - simplified: just copy for now */
                    parser->pos++;
                    if (parser->pos + 3 >= parser->len) {
                        as_free(buffer);
                        return NULL;
                    }
                    /* Skip unicode for now, just use replacement character */
                    buffer[buf_pos++] = '?';
                    parser->pos += 3;
                    break;
                }
                default:
                    as_free(buffer);
                    return NULL;
            }
        } else {
            buffer[buf_pos++] = parser->json[parser->pos];
        }
        parser->pos++;
    }
    
    if (parser->pos >= parser->len || parser->json[parser->pos] != '"') {
        as_free(buffer);
        return NULL;
    }
    
    parser->pos++; /* Skip closing quote */
    buffer[buf_pos] = '\0';
    
    as_value_t* result = as_value_str(buffer);
    as_free(buffer);
    return result;
}

/* Parse JSON number */
static as_value_t* parse_json_number(json_parser_t* parser) {
    size_t start = parser->pos;
    
    if (parser->json[parser->pos] == '-') parser->pos++;
    
    if (!isdigit((unsigned char)parser->json[parser->pos])) {
        return NULL;
    }
    
    while (parser->pos < parser->len && isdigit((unsigned char)parser->json[parser->pos])) {
        parser->pos++;
    }
    
    if (parser->pos < parser->len && parser->json[parser->pos] == '.') {
        parser->pos++;
        while (parser->pos < parser->len && isdigit((unsigned char)parser->json[parser->pos])) {
            parser->pos++;
        }
    }
    
    if (parser->pos < parser->len && (parser->json[parser->pos] == 'e' || parser->json[parser->pos] == 'E')) {
        parser->pos++;
        if (parser->pos < parser->len && (parser->json[parser->pos] == '+' || parser->json[parser->pos] == '-')) {
            parser->pos++;
        }
        while (parser->pos < parser->len && isdigit((unsigned char)parser->json[parser->pos])) {
            parser->pos++;
        }
    }
    
    char* num_str = (char*)as_malloc(parser->pos - start + 1);
    memcpy(num_str, &parser->json[start], parser->pos - start);
    num_str[parser->pos - start] = '\0';
    
    double value = strtod(num_str, NULL);
    as_free(num_str);
    
    return as_value_num(value);
}

/* Parse JSON array */
static as_value_t* parse_json_array(json_parser_t* parser) {
    if (parser->json[parser->pos] != '[') {
        return NULL;
    }
    parser->pos++; /* Skip [ */
    
    skip_whitespace(parser);
    
    as_value_t* arr = as_value_arr();
    
    if (parser->pos < parser->len && parser->json[parser->pos] == ']') {
        parser->pos++; /* Empty array */
        return arr;
    }
    
    while (parser->pos < parser->len) {
        as_value_t* elem = parse_json_value(parser);
        if (!elem) {
            as_value_release(arr);
            return NULL;
        }
        
        as_arr_push(arr->data.arr_val, elem);
        
        skip_whitespace(parser);
        
        if (parser->pos < parser->len && parser->json[parser->pos] == ',') {
            parser->pos++;
            skip_whitespace(parser);
        } else if (parser->pos < parser->len && parser->json[parser->pos] == ']') {
            parser->pos++;
            return arr;
        } else {
            as_value_release(arr);
            return NULL;
        }
    }
    
    as_value_release(arr);
    return NULL;
}

/* Parse JSON object */
static as_value_t* parse_json_object(json_parser_t* parser) {
    if (parser->json[parser->pos] != '{') {
        return NULL;
    }
    parser->pos++; /* Skip { */
    
    skip_whitespace(parser);
    
    as_value_t* obj = as_value_obj();
    
    if (parser->pos < parser->len && parser->json[parser->pos] == '}') {
        parser->pos++; /* Empty object */
        return obj;
    }
    
    while (parser->pos < parser->len) {
        /* Parse key (must be string) */
        as_value_t* key_val = parse_json_string(parser);
        if (!key_val || key_val->type != AS_VALUE_STR) {
            if (key_val) as_value_release(key_val);
            as_value_release(obj);
            return NULL;
        }
        
        char* key = as_strdup(key_val->data.str_val);
        as_value_release(key_val);
        
        skip_whitespace(parser);
        
        if (parser->pos >= parser->len || parser->json[parser->pos] != ':') {
            as_free(key);
            as_value_release(obj);
            return NULL;
        }
        parser->pos++; /* Skip : */
        
        skip_whitespace(parser);
        
        as_value_t* value = parse_json_value(parser);
        if (!value) {
            as_free(key);
            as_value_release(obj);
            return NULL;
        }
        
        as_obj_set(obj->data.obj_val, key, value);
        as_free(key);
        
        skip_whitespace(parser);
        
        if (parser->pos < parser->len && parser->json[parser->pos] == ',') {
            parser->pos++;
            skip_whitespace(parser);
        } else if (parser->pos < parser->len && parser->json[parser->pos] == '}') {
            parser->pos++;
            return obj;
        } else {
            as_value_release(obj);
            return NULL;
        }
    }
    
    as_value_release(obj);
    return NULL;
}

/* Parse JSON value (entry point for recursive parsing) */
static as_value_t* parse_json_value(json_parser_t* parser) {
    skip_whitespace(parser);
    
    if (parser->pos >= parser->len) {
        return NULL;
    }
    
    char c = parser->json[parser->pos];
    
    if (c == '"') {
        return parse_json_string(parser);
    } else if (c == '[') {
        return parse_json_array(parser);
    } else if (c == '{') {
        return parse_json_object(parser);
    } else if (c == '-' || isdigit((unsigned char)c)) {
        return parse_json_number(parser);
    } else if (strncmp(&parser->json[parser->pos], "null", 4) == 0) {
        parser->pos += 4;
        return as_value_null();
    } else if (strncmp(&parser->json[parser->pos], "true", 4) == 0) {
        parser->pos += 4;
        return as_value_bool(true);
    } else if (strncmp(&parser->json[parser->pos], "false", 5) == 0) {
        parser->pos += 5;
        return as_value_bool(false);
    }
    
    return NULL;
}

/* Json:stringify(v): str */
static as_value_t* std_json_stringify(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1) {
        return as_value_str("null");
    }
    
    char* json = stringify_value(args[0]);
    as_value_t* result = as_value_str(json);
    as_free(json);
    
    return result;
}

/* Json:parse(json): any */
static as_value_t* std_json_parse(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        return as_value_error("not_json", NULL);
    }
    
    json_parser_t parser = {
        .json = args[0]->data.str_val,
        .pos = 0,
        .len = strlen(args[0]->data.str_val)
    };
    
    as_value_t* result = parse_json_value(&parser);
    
    if (!result) {
        return as_value_error("not_json", NULL);
    }
    
    skip_whitespace(&parser);
    if (parser.pos != parser.len) {
        /* Trailing garbage */
        as_value_release(result);
        return as_value_error("not_json", NULL);
    }
    
    return result;
}

/* Json:parsable(str): bool */
static as_value_t* std_json_parsable(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        return as_value_bool(false);
    }
    
    json_parser_t parser = {
        .json = args[0]->data.str_val,
        .pos = 0,
        .len = strlen(args[0]->data.str_val)
    };
    
    as_value_t* result = parse_json_value(&parser);
    
    if (!result) {
        return as_value_bool(false);
    }
    
    skip_whitespace(&parser);
    bool success = (parser.pos == parser.len);
    
    as_value_release(result);
    return as_value_bool(success);
}

/* Register Json namespace functions */
void as_std_json_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_json_stringify, NULL);
    as_scope_add(global, "Json:stringify", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_json_parse, NULL);
    as_scope_add(global, "Json:parse", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_json_parsable, NULL);
    as_scope_add(global, "Json:parsable", fn, false);
    as_value_release(fn);
}

#else /* NO_JSON */

void as_std_json_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Json namespace is not available in this build (NO_JSON)\n");
}

#endif /* NO_JSON */

