/*
 * AiScript Standard Library - Async Namespace Interface
 */

#ifndef AISCRIPT_STD_ASYNC_H
#define AISCRIPT_STD_ASYNC_H

#include "interpreter.h"

/* Register Async: namespace functions (Async:interval, Async:timeout) */
void as_std_async_register(as_interpreter_t* interp);

/* Process any pending async callbacks; call at yield points (e.g. sleep, top-level loop) */
void as_std_async_dispatch(as_interpreter_t* interp);

#endif /* AISCRIPT_STD_ASYNC_H */
