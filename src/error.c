/*
 * AiScript - Error handling implementation
 */

#include "error.h"
#include "memory.h"
#include "vm.h"
#include "interpreter.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* Weak so aic-cross can link error.o without the VM/interpreter. */
as_vm_t* as_vm_current(void) __attribute__((weak));
as_interpreter_t* as_interpreter_current(void) __attribute__((weak));

void as_set_error(char** error_out, as_error_type_t type, as_pos_t pos, const char* format, ...) {
    if (!error_out) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    const char* type_str = "";
    switch (type) {
        case AS_ERROR_SYNTAX:   type_str = "Syntax"; break;
        case AS_ERROR_RUNTIME:  type_str = "Runtime"; break;
        case AS_ERROR_TYPE:     type_str = "Type"; break;
        case AS_ERROR_MEMORY:   type_str = "Memory"; break;
        default:                type_str = "Runtime"; break;  /* Pythonic categories are reported via traceback.c */
    }
    
    char full_msg[1536];
    snprintf(full_msg, sizeof(full_msg), "%s error at line %d, column %d: %s",
             type_str, pos.line, pos.column, buffer);
    
    if (*error_out) {
        as_free(*error_out);
    }
    *error_out = as_strdup(full_msg);
}

void as_raise_runtime_error(const char* message)
{
    as_vm_t* vm;
    as_interpreter_t* interp;
    const char* msg = message ? message : "runtime error";

    vm = as_vm_current ? as_vm_current() : NULL;
    if (vm) {
        if (vm->error) {
            free(vm->error);
        }
        vm->error = as_strdup(msg);
        return;
    }

    interp = as_interpreter_current ? as_interpreter_current() : NULL;
    if (interp) {
        if (interp->error_message) {
            as_free(interp->error_message);
        }
        interp->error_message = as_strdup(msg);
        interp->aborted = true;
        if (interp->err_fn) {
            interp->err_fn(msg, interp->cb_userdata);
        }
    }
}


