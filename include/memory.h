/*
 * AiScript - Memory management utilities
 */

#ifndef AISCRIPT_MEMORY_H
#define AISCRIPT_MEMORY_H

#include <stddef.h>

/* Memory allocation wrappers with error checking */
void* as_malloc(size_t size);
void* as_calloc(size_t count, size_t size);
void* as_realloc(void* ptr, size_t size);
char* as_strdup(const char* str);
void as_free(void* ptr);

/* Dynamic array helpers */
#define AS_ARRAY_GROW(arr, count, capacity, type) \
    do { \
        if ((count) >= (capacity)) { \
            (capacity) = (capacity) == 0 ? 8 : (capacity) * 2; \
            (arr) = as_realloc((arr), (capacity) * sizeof(type)); \
        } \
    } while (0)

#endif /* AISCRIPT_MEMORY_H */


