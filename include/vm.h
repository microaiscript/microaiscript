/* Bytecode Virtual Machine for AiScript C implementation
 * Executes compiled bytecode
 */

#ifndef AS_VM_H
#define AS_VM_H

#include <stdint.h>
#include <stdbool.h>
#include "bytecode.h"
#include "types.h"
#include "scope.h"

/* VM configuration */
#define AS_VM_STACK_SIZE 256       /* Max stack depth */
#define AS_VM_CALL_FRAMES 64       /* Max call depth */

/* VM execution result */
typedef enum {
    VM_OK,          /* Execution successful */
    VM_ERROR,       /* Execution error */
    VM_DONE         /* Execution completed */
} as_vm_result_t;

/* Call frame for function calls */
typedef struct {
    size_t pc;             /* Return program counter */
    size_t local_base;     /* Base index for local variables in this frame */
    size_t stack_base;     /* Value-stack height of the caller */
} as_call_frame_t;

/* Virtual Machine state */
typedef struct as_vm {
    /* Bytecode being executed */
    as_bytecode_t* bytecode;
    
    /* Execution state */
    size_t pc;             /* Program counter */
    
    /* Value stack */
    as_value_t** stack;
    size_t stack_top;
    size_t stack_capacity;
    
    /* Call frames */
    as_call_frame_t frames[AS_VM_CALL_FRAMES];
    int frame_count;
    
    /* Global variables (simple array for now) */
    as_value_t** globals;
    size_t globals_count;
    size_t globals_capacity;
    
    /* Local variables (simple array for now, proper frames come with functions) */
    as_value_t** locals;
    size_t locals_count;
    size_t locals_capacity;

    /* Step limit (0 = unlimited) */
    size_t max_steps;
    size_t steps;
    
    /* Error state */
    char* error;           /* Error message */

    /* Optional print sink (OP_PRINT / native print). NULL = stdout */
    void (*print_fn)(const char* text, void* print_user);
    void* print_user;

    /* Optional value-level print sink (<: / print). NULL = use print_fn / stdout */
    void (*out_fn)(as_value_t* value, void* out_user);
    void* out_user;

    /* Owned stdlib interpreter whose globals are copied into VM symbols. */
    struct as_interpreter* std_interp;
} as_vm_t;

typedef void (*as_vm_print_fn_t)(const char* text, void* user);
typedef void (*as_vm_out_fn_t)(as_value_t* value, void* user);

/* VM creation and destruction */
as_vm_t* as_vm_new(void);
void as_vm_free(as_vm_t* vm);

/* Execution */
as_vm_result_t as_vm_execute(as_vm_t* vm, as_bytecode_t* bytecode);
as_vm_result_t as_vm_execute_file(as_vm_t* vm, const char* filename);
as_vm_result_t as_vm_execute_memory(as_vm_t* vm, const uint8_t* data, size_t size);

void as_vm_set_print(as_vm_t* vm, as_vm_print_fn_t fn, void* user);
void as_vm_set_out(as_vm_t* vm, as_vm_out_fn_t fn, void* user);
void as_vm_set_max_steps(as_vm_t* vm, size_t max_steps);
void as_vm_bind_host_ffi(as_vm_t* vm);

/* Call a VM closure or native function from C (e.g. arr.map). */
as_value_t* as_vm_call_value(as_vm_t* vm, as_value_t* fn,
                             as_value_t** args, size_t arg_count);
as_vm_t* as_vm_current(void);

/* VM state management */
void as_vm_reset(as_vm_t* vm);

#endif /* AS_VM_H */

