/* Bytecode Virtual Machine
 * Executes compiled .aic bytecode files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "vm.h"
#include "bytecode.h"
#include "memory.h"
#include "error.h"
#include "primitive_props.h"
#include "interpreter.h"

static void set_global(as_vm_t* vm, uint16_t index, as_value_t* value);
static as_vm_result_t execute_instruction(as_vm_t* vm);

static as_vm_t* g_current_vm = NULL;

as_vm_t* as_vm_current(void)
{
    return g_current_vm;
}

/* Create new VM */
as_vm_t* as_vm_new(void) {
    as_vm_t* vm = malloc(sizeof(as_vm_t));
    if (!vm) return NULL;
    
    /* Initialize stack */
    vm->stack_capacity = AS_VM_STACK_SIZE;
    vm->stack = malloc(sizeof(as_value_t*) * vm->stack_capacity);
    vm->stack_top = 0;
    
    /* Initialize globals */
    vm->globals_capacity = 16;
    vm->globals = malloc(sizeof(as_value_t*) * vm->globals_capacity);
    vm->globals_count = 0;
    if (vm->globals) {
        for (size_t i = 0; i < vm->globals_capacity; i++) {
            vm->globals[i] = NULL;
        }
    }
    
    /* Initialize locals */
    vm->locals_capacity = 256;
    vm->locals = malloc(sizeof(as_value_t*) * vm->locals_capacity);
    vm->locals_count = 0;
    /* Initialize all slots to NULL */
    for (size_t i = 0; i < vm->locals_capacity; i++) {
        vm->locals[i] = NULL;
    }
    
    /* Initialize call frames */
    vm->frame_count = 0;
    vm->max_steps = 0;
    vm->steps = 0;
    
    /* No bytecode loaded yet */
    vm->bytecode = NULL;
    vm->pc = 0;
    
    /* No error yet */
    vm->error = NULL;
    vm->print_fn = NULL;
    vm->print_user = NULL;
    vm->out_fn = NULL;
    vm->out_user = NULL;
    vm->std_interp = NULL;
    
    return vm;
}

void as_vm_set_print(as_vm_t* vm, as_vm_print_fn_t fn, void* user)
{
    if (!vm) {
        return;
    }
    vm->print_fn = fn;
    vm->print_user = user;
}

void as_vm_set_out(as_vm_t* vm, as_vm_out_fn_t fn, void* user)
{
    if (!vm) {
        return;
    }
    vm->out_fn = fn;
    vm->out_user = user;
}

void as_vm_set_max_steps(as_vm_t* vm, size_t max_steps)
{
    if (vm) {
        vm->max_steps = max_steps;
    }
}

static void vm_print_text(as_vm_t* vm, const char* text)
{
    if (vm && vm->print_fn) {
        vm->print_fn(text, vm->print_user);
        return;
    }
    printf("%s\n", text ? text : "");
}

static as_value_t* vm_native_print(as_value_t** args, size_t arg_count, void* userdata)
{
    as_vm_t* vm = (as_vm_t*)userdata;
    size_t i;
    if (vm && vm->out_fn) {
        for (i = 0; i < arg_count; i++) {
            vm->out_fn(args[i], vm->out_user);
        }
        return arg_count > 0 ? as_value_retain(args[0]) : as_value_null();
    }
    for (i = 0; i < arg_count; i++) {
        char* str = as_value_to_string(args[i]);
        vm_print_text(vm, str);
        free(str);
    }
    return as_value_null();
}

static as_value_t* vm_native_peek(as_value_t** args, size_t arg_count, void* userdata)
{
    unsigned int addr;
    volatile unsigned int* p;
    (void)userdata;
    if (arg_count < 1 || !args[0] || args[0]->type != AS_VALUE_NUM) {
        return as_value_null();
    }
    addr = (unsigned int)args[0]->data.num_val;
    p = (volatile unsigned int*)(uintptr_t)addr;
    return as_value_num((double)(*p));
}

static as_value_t* vm_native_poke(as_value_t** args, size_t arg_count, void* userdata)
{
    unsigned int addr;
    unsigned int val;
    volatile unsigned int* p;
    (void)userdata;
    if (arg_count < 2 || !args[0] || !args[1] ||
        args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM) {
        return as_value_null();
    }
    addr = (unsigned int)args[0]->data.num_val;
    val = (unsigned int)args[1]->data.num_val;
    /* Peek XIP is allowed; do not poke SPI/ROM flash windows. */
    if (addr < 0x00100000u || (addr >= 0x08000000u && addr < 0x08800000u)) {
        return as_value_null();
    }
    p = (volatile unsigned int*)(uintptr_t)addr;
    *p = val;
    return as_value_null();
}

void as_vm_bind_host_ffi(as_vm_t* vm)
{
    size_t i;
    as_scope_t* globals;
    if (!vm || !vm->bytecode || !vm->bytecode->symbols) {
        return;
    }
    if (!vm->std_interp) {
        vm->std_interp = as_interpreter_new();
    }
    globals = vm->std_interp ? vm->std_interp->global_scope : NULL;
    for (i = 0; i < vm->bytecode->symbol_count; i++) {
        const char* name = vm->bytecode->symbols[i];
        as_value_t* fn = NULL;
        as_value_t* from_std;
        if (!name) {
            continue;
        }
        if (strcmp(name, "print") == 0) {
            fn = as_value_fn_native(vm_native_print, vm);
        } else if (strcmp(name, "peek") == 0) {
            fn = as_value_fn_native(vm_native_peek, NULL);
        } else if (strcmp(name, "poke") == 0) {
            fn = as_value_fn_native(vm_native_poke, NULL);
        } else if (globals) {
            from_std = as_scope_get(globals, name);
            if (from_std) {
                set_global(vm, (uint16_t)i, from_std);
                continue;
            }
        }
        if (fn) {
            set_global(vm, (uint16_t)i, fn);
            as_value_release(fn);
        }
    }
}

/* Free VM */
void as_vm_free(as_vm_t* vm) {
    if (!vm) return;
    
    /* Release all values on stack */
    for (size_t i = 0; i < vm->stack_top; i++) {
        as_value_release(vm->stack[i]);
    }
    free(vm->stack);
    
    /* Release all globals */
    for (size_t i = 0; i < vm->globals_count; i++) {
        if (vm->globals[i]) {
            as_value_release(vm->globals[i]);
        }
    }
    free(vm->globals);
    
    /* Release all locals */
    for (size_t i = 0; i < vm->locals_capacity; i++) {
        if (vm->locals[i]) {
            as_value_release(vm->locals[i]);
        }
    }
    free(vm->locals);
    
    /* Free error message if any */
    if (vm->error) {
        free(vm->error);
    }

    if (vm->std_interp) {
        as_interpreter_free(vm->std_interp);
        vm->std_interp = NULL;
    }
    
    free(vm);
}

/* Stack operations */
static void push(as_vm_t* vm, as_value_t* value) {
    if (vm->stack_top >= vm->stack_capacity) {
        fprintf(stderr, "VM Error: Stack overflow\n");
        vm->error = as_strdup("Stack overflow");
        return;
    }
    vm->stack[vm->stack_top++] = as_value_retain(value);
}

static as_value_t* pop(as_vm_t* vm) {
    if (vm->stack_top == 0) {
        fprintf(stderr, "VM Error: Stack underflow\n");
        vm->error = as_strdup("Stack underflow");
        return as_value_null();
    }
    return vm->stack[--vm->stack_top];
}

static as_value_t* peek(as_vm_t* vm, size_t depth) {
    if (vm->stack_top <= depth) {
        fprintf(stderr, "VM Error: Stack underflow on peek\n");
        vm->error = as_strdup("Stack underflow");
        return as_value_null();
    }
    return vm->stack[vm->stack_top - 1 - depth];
}

static size_t current_local_base(as_vm_t* vm)
{
    if (vm->frame_count > 0) {
        return vm->frames[vm->frame_count - 1].local_base;
    }
    return 0;
}

static void ensure_local_slot(as_vm_t* vm, size_t slot)
{
    if (slot < vm->locals_capacity) {
        return;
    }
    size_t new_cap = slot + 16;
    size_t i;
    vm->locals = realloc(vm->locals, sizeof(as_value_t*) * new_cap);
    for (i = vm->locals_capacity; i < new_cap; i++) {
        vm->locals[i] = NULL;
    }
    vm->locals_capacity = new_cap;
}

static as_value_t* load_local_slot(as_vm_t* vm, size_t idx)
{
    size_t slot = current_local_base(vm) + idx;
    ensure_local_slot(vm, slot);
    if (!vm->locals[slot]) {
        return as_value_null();
    }
    return vm->locals[slot];
}

static void store_local_slot(as_vm_t* vm, size_t idx, as_value_t* val)
{
    size_t slot = current_local_base(vm) + idx;
    ensure_local_slot(vm, slot);
    if (vm->locals[slot]) {
        as_value_release(vm->locals[slot]);
    }
    vm->locals[slot] = as_value_retain(val);
    if (slot >= vm->locals_count) {
        vm->locals_count = slot + 1;
    }
}

/* Enter a bytecode closure: [pc, param_count, ncap, required_mask, ...captures, ...defaults].
 * Returns 1 if a call frame was pushed, 0 if *result_out holds a value (error or invalid). */
static int vm_enter_arr_closure(as_vm_t* vm, as_value_t* fn_val,
                                as_value_t** args, size_t arg_count,
                                as_value_t** result_out)
{
    as_arr_t* fn_arr;
    size_t fn_pc, expected_params, capture_count, i;
    unsigned required_mask;
    int missing = 0;

    *result_out = NULL;
    if (!fn_val || fn_val->type != AS_VALUE_ARR) {
        fprintf(stderr, "VM Error: Attempt to call non-function value\n");
        vm->error = as_strdup("Attempt to call non-function value");
        *result_out = as_value_null();
        return 0;
    }
    fn_arr = fn_val->data.arr_val;
    if (fn_arr->length < 4 ||
        fn_arr->items[0]->type != AS_VALUE_NUM ||
        fn_arr->items[1]->type != AS_VALUE_NUM ||
        fn_arr->items[2]->type != AS_VALUE_NUM ||
        fn_arr->items[3]->type != AS_VALUE_NUM) {
        fprintf(stderr, "VM Error: Invalid function array format\n");
        vm->error = as_strdup("Invalid function array format");
        *result_out = as_value_null();
        return 0;
    }
    fn_pc = (size_t)fn_arr->items[0]->data.num_val;
    expected_params = (size_t)fn_arr->items[1]->data.num_val;
    capture_count = (size_t)fn_arr->items[2]->data.num_val;
    required_mask = (unsigned)fn_arr->items[3]->data.num_val;
    if (fn_arr->length < 4 + capture_count + expected_params) {
        fprintf(stderr, "VM Error: Function array too short\n");
        vm->error = as_strdup("Invalid function array format");
        *result_out = as_value_null();
        return 0;
    }
    for (i = 0; i < expected_params; i++) {
        if (i >= arg_count && i < 32 && (required_mask & (1u << i))) {
            missing = 1;
            break;
        }
    }
    if (missing) {
        fprintf(stderr, "VM Error: Missing required argument\n");
        vm->error = as_strdup("Missing required argument");
        *result_out = as_value_null();
        return 0;
    }
    if (vm->frame_count >= AS_VM_CALL_FRAMES) {
        fprintf(stderr, "VM Error: Call stack overflow\n");
        vm->error = as_strdup("Call stack overflow");
        *result_out = as_value_null();
        return 0;
    }
    vm->frames[vm->frame_count].pc = vm->pc;
    vm->frames[vm->frame_count].local_base = vm->locals_count;
    vm->frames[vm->frame_count].stack_base = vm->stack_top;
    vm->frame_count++;
    for (i = 0; i < capture_count; i++) {
        store_local_slot(vm, i, fn_arr->items[4 + i]);
    }
    for (i = 0; i < expected_params; i++) {
        if (i < arg_count) {
            store_local_slot(vm, capture_count + i, args[i]);
        } else {
            store_local_slot(vm, capture_count + i, fn_arr->items[4 + capture_count + i]);
        }
    }
    vm->pc = fn_pc;
    return 1;
}

static as_value_t* vm_closure_invoke(as_value_t** args, size_t arg_count, void* userdata)
{
    as_value_t* arr = (as_value_t*)userdata;
    if (!g_current_vm || !arr) {
        return as_value_null();
    }
    return as_vm_call_value(g_current_vm, arr, args, arg_count);
}

/* Read bytecode instructions */
static inline uint8_t read_u8(as_vm_t* vm) {
    return vm->bytecode->code[vm->pc++];
}

static inline uint16_t read_u16(as_vm_t* vm) {
    uint16_t value = (vm->bytecode->code[vm->pc] << 8) | vm->bytecode->code[vm->pc + 1];
    vm->pc += 2;
    return value;
}

static inline int16_t read_i16(as_vm_t* vm) {
    return (int16_t)read_u16(vm);
}

/* Get constant from pool */
static as_value_t* get_constant(as_vm_t* vm, uint16_t index) {
    if (index >= vm->bytecode->const_count) {
        fprintf(stderr, "VM Error: Invalid constant index %u\n", index);
        vm->error = as_strdup("Invalid constant index");
        return as_value_null();
    }
    
    as_constant_t* c = vm->bytecode->constants[index];
    
    switch (c->type) {
        case CONST_NULL:
            return as_value_null();
        case CONST_BOOL:
            return as_value_bool(c->value.bool_val);
        case CONST_NUM:
            return as_value_num(c->value.num_val);
        case CONST_STR:
            return as_value_str(c->value.str.data);
        case CONST_FUNC_CODE:
            /* TODO: Create function value from bytecode */
            fprintf(stderr, "VM Error: Function constants not yet implemented\n");
            return as_value_null();
        default:
            fprintf(stderr, "VM Error: Unknown constant type %d\n", c->type);
            return as_value_null();
    }
}

/* Get/set global variable */
static as_value_t* get_global(as_vm_t* vm, uint16_t index) {
    /* Ensure globals array is large enough */
    if (index >= vm->globals_capacity) {
        size_t new_cap = index + 16;
        vm->globals = realloc(vm->globals, sizeof(as_value_t*) * new_cap);
        /* Initialize new slots to NULL */
        for (size_t i = vm->globals_capacity; i < new_cap; i++) {
            vm->globals[i] = NULL;
        }
        vm->globals_capacity = new_cap;
    }
    
    /* Return the global or error if not set */
    if (index < vm->globals_count && vm->globals[index]) {
        return vm->globals[index];
    }
    {
        const char* name = (vm->bytecode && index < vm->bytecode->symbol_count)
            ? vm->bytecode->symbols[index] : "?";
        char msg[256];
        snprintf(msg, sizeof(msg), "Undefined variable '%s'", name ? name : "?");
        fprintf(stderr, "VM Error: %s\n", msg);
        if (!vm->error) {
            vm->error = as_strdup(msg);
        }
    }
    return as_value_null();
}

static void set_global(as_vm_t* vm, uint16_t index, as_value_t* value) {
    /* Ensure globals array is large enough */
    if (index >= vm->globals_capacity) {
        size_t new_cap = index + 16;
        vm->globals = realloc(vm->globals, sizeof(as_value_t*) * new_cap);
        /* Initialize new slots to NULL */
        for (size_t i = vm->globals_capacity; i < new_cap; i++) {
            vm->globals[i] = NULL;
        }
        vm->globals_capacity = new_cap;
    }
    
    /* Release old value if any */
    if (index < vm->globals_count && vm->globals[index]) {
        as_value_release(vm->globals[index]);
    }
    
    /* Store new value */
    vm->globals[index] = as_value_retain(value);
    
    /* Update count */
    if (index >= vm->globals_count) {
        vm->globals_count = index + 1;
    }
}

/* Execute one instruction */
static as_vm_result_t execute_instruction(as_vm_t* vm) {
    if (vm->pc >= vm->bytecode->code_size) {
        return VM_DONE;
    }
    
    as_opcode_t op = (as_opcode_t)read_u8(vm);
    
    switch (op) {
        /* Stack operations */
        case OP_PUSH_NULL:
            push(vm, as_value_null());
            break;
            
        case OP_PUSH_TRUE:
            push(vm, as_value_bool(true));
            break;
            
        case OP_PUSH_FALSE:
            push(vm, as_value_bool(false));
            break;
            
        case OP_PUSH_CONST: {
            uint16_t idx = read_u16(vm);
            as_value_t* val = get_constant(vm, idx);
            push(vm, val);
            as_value_release(val); /* push retains it */
            break;
        }
            
        case OP_POP: {
            as_value_t* val = pop(vm);
            as_value_release(val);
            break;
        }
            
        case OP_DUP: {
            as_value_t* val = peek(vm, 0);
            push(vm, val);
            break;
        }
            
        /* Variable operations */
        case OP_LOAD_GLOBAL: {
            uint16_t idx = read_u16(vm);
            as_value_t* val = get_global(vm, idx);
            push(vm, val);
            break;
        }
            
        case OP_STORE_GLOBAL: {
            uint16_t idx = read_u16(vm);
            as_value_t* val = pop(vm);
            set_global(vm, idx, val);
            as_value_release(val);
            break;
        }
            
        case OP_LOAD_FAST: {
            uint8_t idx = read_u8(vm);
            push(vm, load_local_slot(vm, idx));
            break;
        }

        case OP_STORE_FAST: {
            uint8_t idx = read_u8(vm);
            as_value_t* val = pop(vm);
            store_local_slot(vm, idx, val);
            as_value_release(val);
            break;
        }

        case OP_LOAD_VAR: {
            uint16_t idx = read_u16(vm);
            push(vm, load_local_slot(vm, idx));
            break;
        }

        case OP_STORE_VAR: {
            uint16_t idx = read_u16(vm);
            as_value_t* val = pop(vm);
            store_local_slot(vm, idx, val);
            as_value_release(val);
            break;
        }

        case OP_SWAP: {
            if (vm->stack_top < 2) {
                fprintf(stderr, "VM Error: Stack underflow\n");
                vm->error = as_strdup("Stack underflow");
                break;
            }
            {
                as_value_t* a = vm->stack[vm->stack_top - 1];
                vm->stack[vm->stack_top - 1] = vm->stack[vm->stack_top - 2];
                vm->stack[vm->stack_top - 2] = a;
            }
            break;
        }
            
        /* Arithmetic operations */
        case OP_ADD: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            as_value_t* result = NULL;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = as_value_num(a->data.num_val + b->data.num_val);
            } else {
                char* sa = (a->type == AS_VALUE_STR) ? strdup(a->data.str_val) : as_value_to_string(a);
                char* sb = (b->type == AS_VALUE_STR) ? strdup(b->data.str_val) : as_value_to_string(b);
                size_t len = strlen(sa) + strlen(sb) + 1;
                char* str = malloc(len);
                strcpy(str, sa);
                strcat(str, sb);
                result = as_value_str(str);
                free(str);
                free(sa);
                free(sb);
            }
            
            push(vm, result);
            as_value_release(result);
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_SUB: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            as_value_t* result = NULL;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = as_value_num(a->data.num_val - b->data.num_val);
            } else {
                fprintf(stderr, "VM Error: Invalid types for SUB\n");
                result = as_value_null();
            }
            
            push(vm, result);
            as_value_release(result);
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_MUL: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            as_value_t* result = NULL;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = as_value_num(a->data.num_val * b->data.num_val);
            } else {
                fprintf(stderr, "VM Error: Invalid types for MUL\n");
                result = as_value_null();
            }
            
            push(vm, result);
            as_value_release(result);
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_DIV: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            as_value_t* result = NULL;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                /* IEEE 754: x/0 is +-Infinity, 0/0 is NaN (same as the interpreter) */
                result = as_value_num(a->data.num_val / b->data.num_val);
            } else {
                fprintf(stderr, "VM Error: Invalid types for DIV\n");
                result = as_value_null();
            }
            
            push(vm, result);
            as_value_release(result);
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_REM: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            as_value_t* result = NULL;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = as_value_num(fmod(a->data.num_val, b->data.num_val));
            } else {
                fprintf(stderr, "VM Error: Invalid types for REM\n");
                result = as_value_null();
            }
            
            push(vm, result);
            as_value_release(result);
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_POW: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            as_value_t* result = NULL;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = as_value_num(pow(a->data.num_val, b->data.num_val));
            } else {
                fprintf(stderr, "VM Error: Invalid types for POW\n");
                result = as_value_null();
            }
            
            push(vm, result);
            as_value_release(result);
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_NEG: {
            as_value_t* a = pop(vm);
            as_value_t* result = NULL;
            
            if (a->type == AS_VALUE_NUM) {
                result = as_value_num(-a->data.num_val);
            } else {
                fprintf(stderr, "VM Error: Invalid type for NEG\n");
                result = as_value_null();
            }
            
            push(vm, result);
            as_value_release(result);
            as_value_release(a);
            break;
        }

        case OP_POS: {
            as_value_t* a = pop(vm);
            if (a->type == AS_VALUE_NUM) {
                push(vm, a);
            } else {
                fprintf(stderr, "VM Error: Invalid type for POS\n");
                push(vm, as_value_null());
            }
            as_value_release(a);
            break;
        }
        case OP_EQ: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool equal = as_value_equals(a, b);
            push(vm, as_value_bool(equal));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_NEQ: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool equal = as_value_equals(a, b);
            push(vm, as_value_bool(!equal));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_LT: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool result = false;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = a->data.num_val < b->data.num_val;
            }
            
            push(vm, as_value_bool(result));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_LTE: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool result = false;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = a->data.num_val <= b->data.num_val;
            }
            
            push(vm, as_value_bool(result));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_GT: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool result = false;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = a->data.num_val > b->data.num_val;
            }
            
            push(vm, as_value_bool(result));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_GTE: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool result = false;
            
            if (a->type == AS_VALUE_NUM && b->type == AS_VALUE_NUM) {
                result = a->data.num_val >= b->data.num_val;
            }
            
            push(vm, as_value_bool(result));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        /* Logical operations */
        case OP_AND: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool result = as_value_to_bool(a) && as_value_to_bool(b);
            push(vm, as_value_bool(result));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_OR: {
            as_value_t* b = pop(vm);
            as_value_t* a = pop(vm);
            bool result = as_value_to_bool(a) || as_value_to_bool(b);
            push(vm, as_value_bool(result));
            as_value_release(a);
            as_value_release(b);
            break;
        }
            
        case OP_NOT: {
            as_value_t* a = pop(vm);
            bool result = !as_value_to_bool(a);
            push(vm, as_value_bool(result));
            as_value_release(a);
            break;
        }
            
        /* Control flow */
        case OP_JUMP: {
            int16_t offset = read_i16(vm);
            vm->pc += offset;
            break;
        }
            
        case OP_JUMP_IF_FALSE: {
            int16_t offset = read_i16(vm);
            as_value_t* cond = peek(vm, 0);
            if (!as_value_to_bool(cond)) {
                vm->pc += offset;
            }
            break;
        }
            
        case OP_JUMP_IF_TRUE: {
            int16_t offset = read_i16(vm);
            as_value_t* cond = peek(vm, 0);
            if (as_value_to_bool(cond)) {
                vm->pc += offset;
            }
            break;
        }
            
        case OP_CALL: {
            uint8_t arg_count = read_u8(vm);
            
            /* Pop arguments in reverse order */
            as_value_t** args = malloc(sizeof(as_value_t*) * arg_count);
            for (int i = arg_count - 1; i >= 0; i--) {
                args[i] = pop(vm);
            }
            
            /* Pop function */
            as_value_t* fn_val = pop(vm);
            
            as_value_t* result = NULL;
            
            if (fn_val->type == AS_VALUE_FN) {
                as_fn_t* fn = fn_val->data.fn_val;
                
                if (fn->kind == AS_FN_NATIVE) {
                    /* Call native function */
                    result = fn->native_fn(args, arg_count, fn->userdata);
                    if (!result) {
                        result = as_value_null();
                    }
                } else {
                    /* User-defined function - not yet implemented */
                    fprintf(stderr, "VM Error: User-defined functions not yet fully implemented\n");
                    result = as_value_null();
                }
            } else if (fn_val->type == AS_VALUE_ARR) {
                /* Unwrapped closure array (older bytecode). */
                if (!vm_enter_arr_closure(vm, fn_val, args, arg_count, &result)) {
                    if (!result) {
                        result = as_value_null();
                    }
                }
            } else {
                fprintf(stderr, "VM Error: Attempt to call non-function value\n");
                vm->error = as_strdup("Attempt to call non-function value");
                result = as_value_null();
            }
            
            /* Cleanup arguments */
            for (size_t i = 0; i < arg_count; i++) {
                as_value_release(args[i]);
            }
            free(args);
            as_value_release(fn_val);
            
            /* Only push result if we have one (non-function-call path) */
            if (result) {
                push(vm, result);
                as_value_release(result);
            }
            break;
        }
            
        case OP_RETURN:
        case OP_RETURN_VALUE: {
            /* Pop return value if OP_RETURN_VALUE, otherwise use null */
            as_value_t* return_val = (op == OP_RETURN_VALUE && vm->stack_top > 0) ? pop(vm) : as_value_null();
            
            /* Check if we're in a function call */
            if (vm->frame_count > 0) {
                /* Pop call frame */
                vm->frame_count--;
                as_call_frame_t* frame = &vm->frames[vm->frame_count];
                
                /* Clean up local variables from this frame */
                while (vm->locals_count > frame->local_base) {
                    vm->locals_count--;
                    if (vm->locals[vm->locals_count]) {
                        as_value_release(vm->locals[vm->locals_count]);
                        vm->locals[vm->locals_count] = NULL;
                    }
                }
                
                /* Discard leftover operand stack from the callee. */
                while (vm->stack_top > frame->stack_base) {
                    as_value_t* leftover = pop(vm);
                    as_value_release(leftover);
                }

                /* Restore PC to return address */
                vm->pc = frame->pc;
                
                /* Push return value */
                push(vm, return_val);
                as_value_release(return_val);
                
                /* Continue execution */
            } else {
                /* Top-level return - end execution */
                if (return_val) {
                    as_value_release(return_val);
                }
                return VM_DONE;
            }
            break;
        }
            
        /* Array operations */
        case OP_BUILD_ARR: {
            uint16_t count = read_u16(vm);
            as_value_t* arr = as_value_arr();
            
            /* Pop elements in reverse order and push to array */
            for (int i = count - 1; i >= 0; i--) {
                as_value_t* elem = pop(vm);
                as_arr_push(arr->data.arr_val, elem);
                as_value_release(elem);
            }
            
            /* Reverse the array to maintain correct order */
            if (count > 1) {
                as_arr_t* a = arr->data.arr_val;
                for (size_t i = 0; i < count / 2; i++) {
                    as_value_t* temp = a->items[i];
                    a->items[i] = a->items[count - 1 - i];
                    a->items[count - 1 - i] = temp;
                }
            }
            
            push(vm, arr);
            as_value_release(arr);
            break;
        }
            
        case OP_INDEX_GET: {
            as_value_t* index_val = pop(vm);
            as_value_t* target = pop(vm);
            as_value_t* result = NULL;

            if (target->type == AS_VALUE_ARR && index_val->type == AS_VALUE_NUM) {
                int index = (int)index_val->data.num_val;
                as_arr_t* arr = target->data.arr_val;
                if (index < 0 || index >= (int)arr->length) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "list index out of range (index: %d, length: %zu)",
                             index, arr->length);
                    fprintf(stderr, "VM Error: %s\n", msg);
                    vm->error = as_strdup(msg);
                    push(vm, as_value_null());
                } else {
                    result = as_arr_get(arr, (size_t)index);
                    push(vm, result ? result : as_value_null());
                }
            } else if (target->type == AS_VALUE_STR && index_val->type == AS_VALUE_NUM) {
                int index = (int)index_val->data.num_val;
                const char* str = target->data.str_val;
                size_t len = strlen(str);
                if (index < 0 || (size_t)index >= len) {
                    fprintf(stderr, "VM Error: String index %d out of bounds\n", index);
                    vm->error = as_strdup("String index out of bounds");
                    push(vm, as_value_null());
                } else {
                    char ch[2] = {str[index], '\0'};
                    result = as_value_str(ch);
                    push(vm, result);
                    as_value_release(result);
                }
            } else if (target->type == AS_VALUE_OBJ) {
                char* key = (index_val->type == AS_VALUE_STR)
                    ? as_strdup(index_val->data.str_val)
                    : as_value_to_string(index_val);
                result = as_obj_get(target->data.obj_val, key);
                push(vm, result ? result : as_value_null());
                as_free(key);
            } else {
                fprintf(stderr, "VM Error: Invalid types for INDEX_GET\n");
                vm->error = as_strdup("Invalid types for INDEX_GET");
                push(vm, as_value_null());
            }

            as_value_release(index_val);
            as_value_release(target);
            break;
        }

        case OP_INDEX_SET: {
            as_value_t* value = pop(vm);
            as_value_t* index_val = pop(vm);
            as_value_t* target = pop(vm);

            if (target->type == AS_VALUE_ARR && index_val->type == AS_VALUE_NUM) {
                int index = (int)index_val->data.num_val;
                as_arr_t* arr = target->data.arr_val;
                if (index < 0 || index >= (int)arr->length) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "list assignment index out of range (index: %d, length: %zu)",
                             index, arr->length);
                    fprintf(stderr, "VM Error: %s\n", msg);
                    vm->error = as_strdup(msg);
                } else {
                    as_arr_set(arr, (size_t)index, value);
                }
            } else if (target->type == AS_VALUE_OBJ) {
                char* key = (index_val->type == AS_VALUE_STR)
                    ? as_strdup(index_val->data.str_val)
                    : as_value_to_string(index_val);
                as_obj_set(target->data.obj_val, key, value);
                as_free(key);
            } else {
                fprintf(stderr, "VM Error: Invalid types for INDEX_SET\n");
                vm->error = as_strdup("Invalid types for INDEX_SET");
            }

            as_value_release(value);
            as_value_release(index_val);
            as_value_release(target);
            break;
        }
            
        /* Object operations */
        case OP_BUILD_OBJ: {
            uint16_t pair_count = read_u16(vm);
            as_value_t* obj = as_value_obj();
            
            /* Pop key-value pairs (stack top is last pair) then insert in source order. */
            {
                as_value_t** keys = malloc(sizeof(as_value_t*) * pair_count);
                as_value_t** vals = malloc(sizeof(as_value_t*) * pair_count);
                int i;
                for (i = pair_count - 1; i >= 0; i--) {
                    vals[i] = pop(vm);
                    keys[i] = pop(vm);
                }
                for (i = 0; i < pair_count; i++) {
                    if (keys[i] && keys[i]->type == AS_VALUE_STR) {
                        as_obj_set(obj->data.obj_val, keys[i]->data.str_val, vals[i]);
                    } else {
                        fprintf(stderr, "VM Error: Object key must be a string\n");
                    }
                    as_value_release(vals[i]);
                    as_value_release(keys[i]);
                }
                free(keys);
                free(vals);
            }
            
            push(vm, obj);
            as_value_release(obj);
            break;
        }
            
        case OP_PROP_GET: {
            uint16_t name_idx = read_u16(vm);
            as_value_t* target = pop(vm);
            const char* key;

            if (name_idx >= vm->bytecode->symbol_count) {
                fprintf(stderr, "VM Error: Invalid symbol index %u\n", name_idx);
                vm->error = as_strdup("Invalid symbol index");
                push(vm, as_value_null());
            } else {
                key = vm->bytecode->symbols[name_idx];
                if (target->type == AS_VALUE_OBJ) {
                    as_value_t* result = as_obj_get(target->data.obj_val, key);
                    push(vm, result ? result : as_value_null());
                } else {
                    as_value_t* result = as_get_prim_prop(NULL, target, key);
                    if (result) {
                        push(vm, result);
                        as_value_release(result);
                    } else {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "No such property '%s'", key);
                        fprintf(stderr, "VM Error: %s\n", msg);
                        vm->error = as_strdup(msg);
                        push(vm, as_value_null());
                    }
                }
            }

            as_value_release(target);
            break;
        }
            
        case OP_PROP_SET: {
            uint16_t name_idx = read_u16(vm);
            as_value_t* value = pop(vm);
            as_value_t* target = pop(vm);
            
            if (name_idx >= vm->bytecode->symbol_count) {
                fprintf(stderr, "VM Error: Invalid symbol index %u\n", name_idx);
            } else if (target->type == AS_VALUE_OBJ) {
                const char* key = vm->bytecode->symbols[name_idx];
                as_obj_set(target->data.obj_val, key, value);
            } else {
                fprintf(stderr, "VM Error: Cannot set property of non-object\n");
            }
            
            as_value_release(value);
            as_value_release(target);
            break;
        }
            
        /* Special operations */
        case OP_PRINT: {
            as_value_t* val = peek(vm, 0);
            if (vm->out_fn) {
                vm->out_fn(val, vm->out_user);
            } else {
                char* str = as_value_to_string(val);
                vm_print_text(vm, str);
                free(str);
            }
            break;
        }
            
        case OP_TYPE_CHECK: {
            as_value_t* v = peek(vm, 0);
            if (!v || v->type != AS_VALUE_BOOL) {
                fprintf(stderr, "VM Error: Logical operand must be boolean\n");
                vm->error = as_strdup("Logical operand must be boolean");
            }
            break;
        }

        case OP_MAKE_FUNCTION: {
            /* Wrap TOS closure array as a callable function. Operands unused. */
            as_value_t* arr;
            as_value_t* wrapped;
            (void)read_u16(vm);
            (void)read_u8(vm);
            arr = pop(vm);
            wrapped = as_value_fn_native(vm_closure_invoke, arr);
            push(vm, wrapped);
            as_value_release(wrapped);
            break;
        }

        case OP_MAKE_CLOSURE:
            (void)read_u16(vm);
            (void)read_u8(vm);
            break;

        case OP_NOP:
            /* Do nothing */
            break;
            
        default:
            fprintf(stderr, "VM Error: Unknown opcode 0x%02X at pc=%zu\n", op, vm->pc - 1);
            vm->error = as_strdup("Unknown opcode");
            return VM_ERROR;
    }
    
    if (vm->error) {
        return VM_ERROR;
    }
    
    return VM_OK;
}

as_value_t* as_vm_call_value(as_vm_t* vm, as_value_t* fn,
                             as_value_t** args, size_t arg_count)
{
    as_value_t* result = NULL;
    int saved_frames;

    if (!vm || !fn) {
        return as_value_null();
    }
    if (fn->type == AS_VALUE_FN) {
        as_fn_t* f = fn->data.fn_val;
        if (f->kind == AS_FN_NATIVE && f->native_fn) {
            result = f->native_fn(args, arg_count, f->userdata);
            return result ? result : as_value_null();
        }
        return as_value_null();
    }
    if (fn->type != AS_VALUE_ARR) {
        return as_value_null();
    }

    saved_frames = vm->frame_count;
    if (!vm_enter_arr_closure(vm, fn, args, arg_count, &result)) {
        return result ? result : as_value_null();
    }
    while (vm->frame_count > saved_frames && !vm->error) {
        as_vm_result_t vr;
        if (vm->pc >= vm->bytecode->code_size) {
            break;
        }
        if (vm->max_steps > 0) {
            vm->steps++;
            if (vm->steps > vm->max_steps) {
                fprintf(stderr, "VM Error: Max steps exceeded\n");
                vm->error = as_strdup("Max steps exceeded");
                break;
            }
        }
        vr = execute_instruction(vm);
        if (vr == VM_ERROR || vr == VM_DONE) {
            break;
        }
    }
    if (vm->error) {
        return as_value_null();
    }
    if (vm->stack_top > 0) {
        return pop(vm);
    }
    return as_value_null();
}

/* Execute bytecode */
as_vm_result_t as_vm_execute(as_vm_t* vm, as_bytecode_t* bytecode) {
    as_vm_t* prev_vm = g_current_vm;
    vm->bytecode = bytecode;
    vm->pc = 0;
    vm->steps = 0;
    g_current_vm = vm;
    as_vm_bind_host_ffi(vm);
    {
        while (vm->pc < bytecode->code_size) {
            as_vm_result_t result;
            if (vm->max_steps > 0) {
                vm->steps++;
                if (vm->steps > vm->max_steps) {
                    fprintf(stderr, "VM Error: Max steps exceeded\n");
                    vm->error = as_strdup("Max steps exceeded");
                    g_current_vm = prev_vm;
                    return VM_ERROR;
                }
            }
            result = execute_instruction(vm);

            if (result == VM_ERROR) {
                g_current_vm = prev_vm;
                return VM_ERROR;
            }

            if (result == VM_DONE) {
                g_current_vm = prev_vm;
                return VM_OK;
            }
        }
    }

    g_current_vm = prev_vm;
    return VM_OK;
}

as_vm_result_t as_vm_execute_memory(as_vm_t* vm, const uint8_t* data, size_t size) {
    as_bytecode_t* bc;
    as_vm_result_t result;

    bc = as_bytecode_read_memory(data, size);
    if (!bc) {
        vm->error = as_strdup("Failed to load bytecode");
        return VM_ERROR;
    }
    if (!as_bytecode_validate(bc)) {
        vm->error = as_strdup("Invalid bytecode");
        as_bytecode_free(bc);
        return VM_ERROR;
    }
    result = as_vm_execute(vm, bc);
    as_bytecode_free(bc);
    return result;
}

/* Load and execute bytecode file */
as_vm_result_t as_vm_execute_file(as_vm_t* vm, const char* filename) {
    /* Load bytecode */
    as_bytecode_t* bc = as_bytecode_read_file(filename);
    if (!bc) {
        fprintf(stderr, "VM Error: Failed to load bytecode from '%s'\n", filename);
        vm->error = as_strdup("Failed to load bytecode");
        return VM_ERROR;
    }
    
    /* Validate bytecode */
    if (!as_bytecode_validate(bc)) {
        fprintf(stderr, "VM Error: Invalid bytecode in '%s'\n", filename);
        vm->error = as_strdup("Invalid bytecode");
        as_bytecode_free(bc);
        return VM_ERROR;
    }
    
    /* Execute */
    as_vm_result_t result = as_vm_execute(vm, bc);
    
    /* Cleanup */
    as_bytecode_free(bc);
    
    return result;
}

/* Reset VM state */
void as_vm_reset(as_vm_t* vm) {
    /* Clear stack */
    for (size_t i = 0; i < vm->stack_top; i++) {
        as_value_release(vm->stack[i]);
    }
    vm->stack_top = 0;
    
    /* Clear locals */
    for (size_t i = 0; i < vm->locals_count; i++) {
        if (vm->locals[i]) {
            as_value_release(vm->locals[i]);
            vm->locals[i] = NULL;
        }
    }
    vm->locals_count = 0;
    
    /* Reset PC */
    vm->pc = 0;
    
    /* Clear error */
    if (vm->error) {
        free(vm->error);
        vm->error = NULL;
    }
}

