/*
 * AiScript - Memory management implementation
 */

#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void* as_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "Fatal: Out of memory\n");
        exit(1);
    }
    return ptr;
}

void* as_calloc(size_t count, size_t size) {
    void* ptr = calloc(count, size);
    if (!ptr && count > 0 && size > 0) {
        fprintf(stderr, "Fatal: Out of memory\n");
        exit(1);
    }
    return ptr;
}

void* as_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "Fatal: Out of memory\n");
        exit(1);
    }
    return new_ptr;
}

char* as_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* new_str = as_malloc(len + 1);
    memcpy(new_str, str, len + 1);
    return new_str;
}

void as_free(void* ptr) {
    free(ptr);
}


