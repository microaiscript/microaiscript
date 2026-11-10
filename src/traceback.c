/* AiScript Traceback System - Python-like error reporting */

#include "traceback.h"
#include "interpreter.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Helper function to read source line from file */
static char* read_source_line(const char* filename, int line_num) {
    if (strcmp(filename, "<script>") == 0 || strcmp(filename, "<unknown>") == 0) {
        return NULL;
    }
    
    FILE* file = fopen(filename, "r");
    if (!file) return NULL;
    
    char buffer[1024];
    int current = 1;
    
    while (fgets(buffer, sizeof(buffer), file)) {
        if (current == line_num) {
            fclose(file);
            /* Trim newline */
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0';
            }
            return as_strdup(buffer);
        }
        current++;
    }
    
    fclose(file);
    return NULL;
}

/* Global error context */
as_error_context_t* g_error_context = NULL;

/* Initialize error system */
void as_error_init(void) {
    if (g_error_context) {
        return;  /* Already initialized */
    }
    
    g_error_context = (as_error_context_t*)as_malloc(sizeof(as_error_context_t));
    g_error_context->type = AS_ERROR_RUNTIME;  /* Default to runtime error */
    g_error_context->message = NULL;
    g_error_context->frames = NULL;
    g_error_context->suggestion = NULL;
    
    /* Enable by default, can be disabled via compile flag or runtime */
#ifdef NO_PYTHONIC_ERRORS
    g_error_context->enabled = false;
#else
    g_error_context->enabled = true;
#endif
}

/* Cleanup error system */
void as_error_cleanup(void) {
    if (!g_error_context) {
        return;
    }
    
    /* Free message */
    if (g_error_context->message) {
        as_free(g_error_context->message);
    }
    
    /* Free traceback frames */
    as_traceback_clear();
    
    as_free(g_error_context);
    g_error_context = NULL;
}

/* Push call stack frame */
void as_traceback_push(const char* file, int line, int column, const char* function) {
    if (!g_error_context) {
        as_error_init();
    }
    
    as_traceback_frame_t* frame = (as_traceback_frame_t*)as_malloc(sizeof(as_traceback_frame_t));
    frame->file = file ? as_strdup(file) : as_strdup("<unknown>");
    frame->line = line;
    frame->column = column;
    frame->function = function ? as_strdup(function) : as_strdup("<module>");
    frame->next = g_error_context->frames;
    
    g_error_context->frames = frame;
}

/* Pop call stack frame */
void as_traceback_pop(void) {
    if (!g_error_context || !g_error_context->frames) {
        return;
    }
    
    as_traceback_frame_t* frame = g_error_context->frames;
    g_error_context->frames = frame->next;
    
    as_free((void*)frame->file);
    as_free((void*)frame->function);
    as_free(frame);
}

/* Clear all traceback frames */
void as_traceback_clear(void) {
    if (!g_error_context) {
        return;
    }
    
    while (g_error_context->frames) {
        as_traceback_pop();
    }
}

/* Get error type name */
const char* as_error_type_name(as_error_type_t type) {
    /* Handle extended Pythonic error types */
    if (type == AS_ERROR_PYTHONIC_NAME) return "NameError";
    if (type == AS_ERROR_PYTHONIC_ATTRIBUTE) return "AttributeError";
    if (type == AS_ERROR_PYTHONIC_INDEX) return "IndexError";
    if (type == AS_ERROR_PYTHONIC_KEY) return "KeyError";
    if (type == AS_ERROR_PYTHONIC_IMPORT) return "ImportError";
    if (type == AS_ERROR_PYTHONIC_OS) return "OSError";
    if (type == AS_ERROR_PYTHONIC_FATAL) return "Fatal AiScript error";
    
    /* Handle standard error types */
    switch (type) {
        case AS_ERROR_RUNTIME: return "RuntimeError";
        case AS_ERROR_TYPE: return "TypeError";
        case AS_ERROR_SYNTAX: return "SyntaxError";
        case AS_ERROR_MEMORY: return "MemoryError";
        default: return "Error";
    }
}

/* Report error with traceback */
void as_error_report(as_error_type_t type, const char* message) {
    as_error_report_with_suggestion(type, message, NULL);
}

void as_error_report_with_suggestion(as_error_type_t type, const char* message, const char* suggestion) {
    if (!g_error_context) {
        as_error_init();
    }

    /* Embedders that installed an error callback get the message instead of stderr output */
    {
        as_interpreter_t* interp = as_interpreter_current();
        if (interp && interp->err_fn) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "%s: %s%s%s%s", as_error_type_name(type), message,
                     suggestion ? ". Did you mean: '" : "", suggestion ? suggestion : "",
                     suggestion ? "'?" : "");
            interp->err_fn(buffer, interp->cb_userdata);
            return;
        }
    }
    
    /* If Python-like errors disabled, use simple format */
    if (!g_error_context->enabled) {
        fprintf(stderr, "Runtime error: %s\n", message);
        return;
    }

    if (type == (as_error_type_t)AS_ERROR_PYTHONIC_FATAL) {
        fprintf(stderr, "%s: %s\n", as_error_type_name(type), message);
        return;
    }
    
    /* Print traceback header */
    fprintf(stderr, "Traceback (most recent call last):\n");
    
    /* Print call stack (reverse order - oldest first) */
    /* First, count frames */
    int frame_count = 0;
    as_traceback_frame_t* f = g_error_context->frames;
    while (f) {
        frame_count++;
        f = f->next;
    }
    
    /* Print frames in reverse */
    if (frame_count > 0) {
        as_traceback_frame_t** frames = (as_traceback_frame_t**)as_malloc(sizeof(as_traceback_frame_t*) * frame_count);
        f = g_error_context->frames;
        for (int i = 0; i < frame_count; i++) {
            frames[i] = f;
            f = f->next;
        }
        
        for (int i = frame_count - 1; i >= 0; i--) {
            fprintf(stderr, "  File \"%s\", line %d", frames[i]->file, frames[i]->line);
            if (strcmp(frames[i]->function, "<module>") != 0) {
                fprintf(stderr, ", in %s", frames[i]->function);
            }
            fprintf(stderr, "\n");
            
            /* Try to show source line */
            char* source = read_source_line(frames[i]->file, frames[i]->line);
            if (source) {
                /* Trim leading whitespace */
                char* trimmed = source;
                while (*trimmed == ' ' || *trimmed == '\t') {
                    trimmed++;
                }
                fprintf(stderr, "    %s\n", trimmed);
                as_free(source);
            }
        }
        
        as_free(frames);
    }
    
    /* Print error type and message */
    fprintf(stderr, "%s: %s", as_error_type_name(type), message);
    
    /* Print suggestion if available */
    if (suggestion) {
        fprintf(stderr, ". Did you mean: '%s'?", suggestion);
    }
    
    fprintf(stderr, "\n");
}

/* Calculate Levenshtein distance for fuzzy matching */
static int levenshtein_distance(const char* s1, const char* s2) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    size_t* prev = (size_t*)as_malloc(sizeof(size_t) * (len2 + 1));
    size_t* curr = (size_t*)as_malloc(sizeof(size_t) * (len2 + 1));
    
    for (size_t i = 0; i <= len2; i++) {
        prev[i] = i;
    }
    
    for (size_t i = 0; i < len1; i++) {
        curr[0] = i + 1;
        
        for (size_t j = 0; j < len2; j++) {
            size_t cost = (tolower(s1[i]) == tolower(s2[j])) ? 0 : 1;
            size_t del = prev[j + 1] + 1;
            size_t ins = curr[j] + 1;
            size_t sub = prev[j] + cost;
            
            curr[j + 1] = del < ins ? del : ins;
            if (sub < curr[j + 1]) {
                curr[j + 1] = sub;
            }
        }
        
        size_t* temp = prev;
        prev = curr;
        curr = temp;
    }
    
    int result = prev[len2];
    as_free(prev);
    as_free(curr);
    
    return result;
}

/* Find closest match using fuzzy matching */
const char* as_suggest_closest(const char* input, const char** candidates, size_t count) {
    if (!input || !candidates || count == 0) {
        return NULL;
    }
    
    int min_distance = 999999;
    const char* closest = NULL;
    
    for (size_t i = 0; i < count; i++) {
        int distance = levenshtein_distance(input, candidates[i]);
        
        /* Only suggest if distance is reasonable (> 0, < 3, or < half the length) */
        /* Exclude exact matches (distance == 0) */
        if (distance > 0 && distance < min_distance && distance < 3 && distance < (int)strlen(input) / 2 + 1) {
            min_distance = distance;
            closest = candidates[i];
        }
    }
    
    return closest;
}

/* Formatted error reporting */
void as_error_name(const char* name) {
    char message[256];
    snprintf(message, sizeof(message), "name '%s' is not defined", name);
    as_error_report((as_error_type_t)AS_ERROR_PYTHONIC_NAME, message);
}

void as_error_attribute(const char* namespace_name, const char* attr) {
    char message[256];
    snprintf(message, sizeof(message), "namespace '%s' has no attribute '%s'", namespace_name, attr);
    as_error_report((as_error_type_t)AS_ERROR_PYTHONIC_ATTRIBUTE, message);
}

void as_error_import(const char* module, const char* reason) {
    char message[512];
    snprintf(message, sizeof(message), "Failed to import module '%s': %s", module, reason);
    as_error_report((as_error_type_t)AS_ERROR_PYTHONIC_IMPORT, message);
}

void as_error_os(const char* message) {
    as_error_report((as_error_type_t)AS_ERROR_PYTHONIC_OS, message);
}

/* Enable/disable Python-like errors */
void as_error_set_enabled(bool enabled) {
    if (!g_error_context) {
        as_error_init();
    }
    g_error_context->enabled = enabled;
}

bool as_error_is_enabled(void) {
    if (!g_error_context) {
        return false;
    }
    return g_error_context->enabled;
}

