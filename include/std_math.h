/*
 * AiScript Standard Library - Math Namespace
 * 
 * Mathematical constants and functions.
 * Most are thin wrappers around C's libm functions.
 * 
 * Opt-out system:
 * - Math functions CAN be disabled via compiler flags:
 *   -DNO_MATH (disables entire namespace)
 *   -DNO_MATH_RANDOM (disables random functions)
 *   -DNO_MATH_HYPERBOLIC (disables sinh, cosh, tanh, etc.)
 *   -DNO_MATH_MISC (disables clz32, fround, imul)
 */

#ifndef AISCRIPT_STD_MATH_H
#define AISCRIPT_STD_MATH_H

#include "interpreter.h"

/* Register all Math namespace constants and functions with the interpreter */
void as_std_math_register(as_interpreter_t* interp);

/* Check if Math namespace is available */
bool as_std_math_is_available(void);

#endif /* AISCRIPT_STD_MATH_H */

