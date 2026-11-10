/*
 * AiScript - Error handling
 */

#ifndef AISCRIPT_ERROR_H
#define AISCRIPT_ERROR_H

#include "types.h"

/* Error categories. The AS_ERROR_PYTHONIC_* values are used by the
 * Python-style traceback reporter (see traceback.h). */
typedef enum {
    AS_ERROR_SYNTAX,
    AS_ERROR_RUNTIME,
    AS_ERROR_TYPE,
    AS_ERROR_MEMORY,
    AS_ERROR_PYTHONIC_NAME = 100,       /* NameError: name not defined */
    AS_ERROR_PYTHONIC_ATTRIBUTE,        /* AttributeError: namespace has no attribute */
    AS_ERROR_PYTHONIC_INDEX,            /* IndexError: index out of range */
    AS_ERROR_PYTHONIC_KEY,              /* KeyError: key not found */
    AS_ERROR_PYTHONIC_IMPORT,           /* ImportError: module not found */
    AS_ERROR_PYTHONIC_OS,               /* OSError: system error */
    AS_ERROR_PYTHONIC_FATAL             /* Fatal error: crash/segfault */
} as_error_type_t;

/* Error creation */
void as_set_error(char** error_out, as_error_type_t type, as_pos_t pos, const char* format, ...);

/* Raise a runtime error on the current VM or interpreter (native stdlib). */
void as_raise_runtime_error(const char* message);

#endif /* AISCRIPT_ERROR_H */


