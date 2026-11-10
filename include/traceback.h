/* AiScript Traceback System - Python-like error reporting */

#ifndef AS_TRACEBACK_H
#define AS_TRACEBACK_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration - error types defined in error.h */
#include "error.h"

/* Call stack frame */
typedef struct as_traceback_frame {
    const char* file;                  /* File name */
    int line;                          /* Line number */
    int column;                        /* Column number */
    const char* function;              /* Function/scope name */
    struct as_traceback_frame* next;   /* Next frame in stack */
} as_traceback_frame_t;

/* Error context */
typedef struct {
    as_error_type_t type;              /* Error type */
    char* message;                     /* Error message */
    as_traceback_frame_t* frames;      /* Call stack */
    const char* suggestion;            /* "Did you mean?" suggestion */
    bool enabled;                      /* Whether Python-like errors are enabled */
} as_error_context_t;

/* Global error context */
extern as_error_context_t* g_error_context;

/* Initialize/cleanup error system */
void as_error_init(void);
void as_error_cleanup(void);

/* Push/pop call stack frames */
void as_traceback_push(const char* file, int line, int column, const char* function);
void as_traceback_pop(void);
void as_traceback_clear(void);

/* Report error with traceback */
void as_error_report(as_error_type_t type, const char* message);
void as_error_report_with_suggestion(as_error_type_t type, const char* message, const char* suggestion);

/* Formatted error reporting */
void as_error_name(const char* name);
void as_error_attribute(const char* namespace_name, const char* attr);
void as_error_import(const char* module, const char* reason);
void as_error_os(const char* message);

/* Fuzzy matching for suggestions */
const char* as_suggest_closest(const char* input, const char** candidates, size_t count);

/* Enable/disable Python-like errors (runtime) */
void as_error_set_enabled(bool enabled);
bool as_error_is_enabled(void);

/* Get error type name */
const char* as_error_type_name(as_error_type_t type);

#endif /* AS_TRACEBACK_H */

