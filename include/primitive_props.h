/*
 * AiScript - Built-in Properties
 * 
 * Implements built-in properties for primitive types (strings, arrays, numbers, errors)
 * Based on the official AiScript specification.
 */

#ifndef AS_PRIMITIVE_PROPS_H
#define AS_PRIMITIVE_PROPS_H

#include "types.h"

/* Forward declarations */
struct as_interpreter;

/* Get built-in property from a primitive value
 * Returns the property value or NULL if not found
 * Caller is responsible for releasing the returned value
 */
as_value_t* as_get_prim_prop(struct as_interpreter* interp, as_value_t* target, const char* name);

/* Helper to call a user function from native code */
as_value_t* as_call_user_fn(struct as_interpreter* interp, as_value_t* fn, 
                            as_value_t** args, size_t arg_count);

#endif /* AS_PRIMITIVE_PROPS_H */

