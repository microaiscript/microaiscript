/*
 * AiScript Standard Library - Core Namespace
 * 
 * Core utilities and operator functions.
 * 
 * Opt-out system:
 * - Core utilities (type, to_str, range) CANNOT be disabled
 * - Operator functions CAN be disabled via compiler flags:
 *   -DNO_CORE_LOGICAL, -DNO_CORE_COMPARISON, -DNO_CORE_ARITHMETIC
 *   Or individual functions: -DNO_CORE_NOT, -DNO_CORE_EQ, etc.
 */

#ifndef AISCRIPT_STD_CORE_H
#define AISCRIPT_STD_CORE_H

#include "interpreter.h"

/* Register all Core namespace functions with the interpreter */
void as_std_core_register(as_interpreter_t* interp);

/* Check if a Core function is available (for debugging/introspection) */
bool as_std_core_is_available(const char* function_name);

/* Get list of disabled Core functions (for error messages) */
const char* as_std_core_get_disabled_list(void);

#endif /* AISCRIPT_STD_CORE_H */

