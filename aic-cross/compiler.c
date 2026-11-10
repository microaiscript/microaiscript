/* Bytecode compiler implementation
 * Compiles AiScript AST to bytecode
 * 
 * This is a minimal initial implementation that handles basic operations.
 * More node types and optimizations will be added in later phases.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "compiler.h"
#include "memory.h"
#include "bytecode.h"
#include "node.h"
#include "token.h"

/* Helper macros for memory management */
#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)

/* Constant pool helper - free a constant */
static void free_constant(as_constant_t* c) {
    if (!c) return;
    if (c->type == CONST_STR && c->value.str.data) {
        free(c->value.str.data);
    }
    free(c);
}

/* Create new compiler */
as_compiler_t* as_compiler_new(as_compiler_config_t* config) {
    as_compiler_t* comp = malloc(sizeof(as_compiler_t));
    if (!comp) return NULL;
    
    /* Initialize code buffer */
    comp->code_capacity = 256;
    comp->code = malloc(comp->code_capacity);
    comp->code_size = 0;
    
    /* Initialize constant pool */
    comp->const_capacity = 16;
    comp->constants = malloc(sizeof(as_constant_t*) * comp->const_capacity);
    comp->const_count = 0;
    
    /* Initialize symbol table */
    comp->symbol_capacity = 16;
    comp->symbols = malloc(sizeof(char*) * comp->symbol_capacity);
    comp->symbol_mutable = calloc(comp->symbol_capacity, sizeof(bool));
    comp->symbol_count = 0;
    comp->ns_prefix = NULL;
    comp->temp_id = 0;
    
    /* Initialize locals */
    comp->local_capacity = 16;
    comp->locals = malloc(sizeof(as_local_t) * comp->local_capacity);
    comp->local_count = 0;
    
    /* Initialize captured variables */
    comp->captured_vars = NULL;
    comp->captured_count = 0;
    comp->captured_capacity = 0;
    comp->cell_names = NULL;
    comp->cell_count = 0;
    comp->cell_capacity = 0;
    comp->parent = NULL;
    
    /* Initialize natives */
    comp->native_capacity = 8;
    comp->natives = malloc(sizeof(as_native_ref_t) * comp->native_capacity);
    comp->native_count = 0;
    
    /* Initialize debug info */
    comp->debug_capacity = 64;
    comp->debug_info = malloc(sizeof(as_debug_info_t) * comp->debug_capacity);
    comp->debug_count = 0;
    
    /* Set configuration */
    if (config) {
        comp->config = *config;
    } else {
        /* Default configuration */
        comp->config.opt_level = OPT_BASIC;
        comp->config.debug_info = false;
        comp->config.inline_functions = false;
        comp->config.const_propagation = true;
        comp->config.target_arch = NULL;
    }
    
    /* Initialize scope */
    comp->scope_depth = 0;
    comp->loop_depth = 0;
    
    /* No errors yet */
    comp->error_message = NULL;
    
    return comp;
}

/* Free compiler */
void as_compiler_free(as_compiler_t* comp) {
    if (!comp) return;
    
    free(comp->code);
    
    for (size_t i = 0; i < comp->const_count; i++) {
        free_constant(comp->constants[i]);
    }
    free(comp->constants);
    
    for (size_t i = 0; i < comp->symbol_count; i++) {
        free(comp->symbols[i]);
    }
    free(comp->symbols);
    free(comp->symbol_mutable);
    free(comp->ns_prefix);
    
    for (size_t i = 0; i < comp->local_count; i++) {
        free(comp->locals[i].name);
    }
    free(comp->locals);
    
    for (size_t i = 0; i < comp->native_count; i++) {
        free(comp->natives[i].module);
        free(comp->natives[i].name);
    }
    free(comp->natives);
    
    free(comp->debug_info);
    
    if (comp->error_message) {
        free(comp->error_message);
    }
    if (comp->cell_names) {
        size_t i;
        for (i = 0; i < comp->cell_count; i++) {
            free(comp->cell_names[i]);
        }
        free(comp->cell_names);
    }
    
    free(comp);
}

/* Emit a single byte */
static void emit_byte(as_compiler_t* comp, uint8_t byte) {
    if (comp->code_size >= comp->code_capacity) {
        comp->code_capacity = GROW_CAPACITY(comp->code_capacity);
        comp->code = realloc(comp->code, comp->code_capacity);
    }
    comp->code[comp->code_size++] = byte;
}

/* Emit opcode */
void as_emit_op(as_compiler_t* comp, as_opcode_t op) {
    emit_byte(comp, (uint8_t)op);
}

/* Emit u8 */
void as_emit_u8(as_compiler_t* comp, uint8_t value) {
    emit_byte(comp, value);
}

/* Emit u16 (big-endian) */
void as_emit_u16(as_compiler_t* comp, uint16_t value) {
    emit_byte(comp, (value >> 8) & 0xFF);
    emit_byte(comp, value & 0xFF);
}

/* Emit i16 (big-endian) */
void as_emit_i16(as_compiler_t* comp, int16_t value) {
    as_emit_u16(comp, (uint16_t)value);
}

/* Emit a jump instruction, returns offset to patch */
size_t as_emit_jump(as_compiler_t* comp, as_opcode_t op) {
    as_emit_op(comp, op);
    as_emit_u16(comp, 0xFFFF); /* Placeholder */
    return comp->code_size - 2;
}

/* Patch a jump instruction */
void as_patch_jump(as_compiler_t* comp, size_t offset) {
    int16_t jump = comp->code_size - offset - 2;
    comp->code[offset] = (jump >> 8) & 0xFF;
    comp->code[offset + 1] = jump & 0xFF;
}

/* Add constant to pool */
uint16_t as_add_const(as_compiler_t* comp, as_constant_t* constant) {
    /* Check if constant already exists (for deduplication) */
    for (size_t i = 0; i < comp->const_count; i++) {
        as_constant_t* existing = comp->constants[i];
        if (existing->type == constant->type) {
            bool match = false;
            switch (constant->type) {
                case CONST_NUM:
                    match = (existing->value.num_val == constant->value.num_val);
                    break;
                case CONST_STR:
                    match = (strcmp(existing->value.str.data, constant->value.str.data) == 0);
                    break;
                case CONST_BOOL:
                    match = (existing->value.bool_val == constant->value.bool_val);
                    break;
                case CONST_NULL:
                    match = true;
                    break;
                case CONST_FUNC_CODE:
                    match = false;  /* Don't deduplicate function code */
                    break;
            }
            if (match) {
                free_constant(constant);
                return (uint16_t)i;
            }
        }
    }
    
    /* Add new constant */
    if (comp->const_count >= comp->const_capacity) {
        comp->const_capacity = GROW_CAPACITY(comp->const_capacity);
        comp->constants = realloc(comp->constants, 
                                   sizeof(as_constant_t*) * comp->const_capacity);
    }
    
    comp->constants[comp->const_count] = constant;
    return (uint16_t)comp->const_count++;
}

/* Add number constant */
uint16_t as_add_const_num(as_compiler_t* comp, double value) {
    as_constant_t* c = malloc(sizeof(as_constant_t));
    c->type = CONST_NUM;
    c->value.num_val = value;
    return as_add_const(comp, c);
}

/* Add string constant */
uint16_t as_add_const_str(as_compiler_t* comp, const char* value) {
    as_constant_t* c = malloc(sizeof(as_constant_t));
    c->type = CONST_STR;
    c->value.str.length = strlen(value);
    c->value.str.data = strdup(value);
    return as_add_const(comp, c);
}

/* Add boolean constant */
uint16_t as_add_const_bool(as_compiler_t* comp, bool value) {
    as_constant_t* c = malloc(sizeof(as_constant_t));
    c->type = CONST_BOOL;
    c->value.bool_val = value;
    return as_add_const(comp, c);
}

/* Add null constant */
static uint16_t as_add_const_null(as_compiler_t* comp) {
    as_constant_t* c = malloc(sizeof(as_constant_t));
    c->type = CONST_NULL;
    return as_add_const(comp, c);
}

/* Add symbol to table */
uint16_t as_add_symbol(as_compiler_t* comp, const char* name) {
    /* Check if symbol already exists */
    for (size_t i = 0; i < comp->symbol_count; i++) {
        if (strcmp(comp->symbols[i], name) == 0) {
            return (uint16_t)i;
        }
    }
    
    /* Add new symbol */
    if (comp->symbol_count >= comp->symbol_capacity) {
        size_t old_cap = comp->symbol_capacity;
        size_t i;
        comp->symbol_capacity = GROW_CAPACITY(comp->symbol_capacity);
        comp->symbols = realloc(comp->symbols, sizeof(char*) * comp->symbol_capacity);
        comp->symbol_mutable = realloc(comp->symbol_mutable,
                                       sizeof(bool) * comp->symbol_capacity);
        for (i = old_cap; i < comp->symbol_capacity; i++) {
            comp->symbol_mutable[i] = true;
        }
    }
    
    comp->symbols[comp->symbol_count] = strdup(name);
    if (comp->symbol_mutable) {
        comp->symbol_mutable[comp->symbol_count] = true;
    }
    return (uint16_t)comp->symbol_count++;
}

static void emit_load_local(as_compiler_t* comp, int local_idx)
{
    if (local_idx < 256) {
        as_emit_op(comp, OP_LOAD_FAST);
        as_emit_u8(comp, (uint8_t)local_idx);
    } else {
        as_emit_op(comp, OP_LOAD_VAR);
        as_emit_u16(comp, (uint16_t)local_idx);
    }
}

static void emit_store_local(as_compiler_t* comp, int local_idx)
{
    if (local_idx < 256) {
        as_emit_op(comp, OP_STORE_FAST);
        as_emit_u8(comp, (uint8_t)local_idx);
    } else {
        as_emit_op(comp, OP_STORE_VAR);
        as_emit_u16(comp, (uint16_t)local_idx);
    }
}

static void emit_push_num(as_compiler_t* comp, double value)
{
    uint16_t idx = as_add_const_num(comp, value);
    as_emit_op(comp, OP_PUSH_CONST);
    as_emit_u16(comp, idx);
}

static void emit_push_null(as_compiler_t* comp)
{
    uint16_t idx = as_add_const_null(comp);
    as_emit_op(comp, OP_PUSH_CONST);
    as_emit_u16(comp, idx);
}

static void emit_box_local(as_compiler_t* comp, int local_idx)
{
    emit_load_local(comp, local_idx);
    as_emit_op(comp, OP_BUILD_ARR);
    as_emit_u16(comp, 1);
    emit_store_local(comp, local_idx);
}

static void compile_error(as_compiler_t* comp, const char* msg)
{
    if (comp && !comp->error_message) {
        comp->error_message = strdup(msg);
    }
}

static char* join_ns(const char* prefix, const char* name)
{
    size_t len;
    char* out;
    if (!name) {
        return NULL;
    }
    if (!prefix || !prefix[0]) {
        return strdup(name);
    }
    len = strlen(prefix) + strlen(name) + 2;
    out = malloc(len);
    snprintf(out, len, "%s:%s", prefix, name);
    return out;
}

static void unique_temp(as_compiler_t* comp, char* buf, size_t buflen, const char* tag)
{
    snprintf(buf, buflen, "__%s%u", tag, comp->temp_id++);
}

static void as_set_continue_target(as_compiler_t* comp)
{
    if (comp->loop_depth > 0) {
        comp->loop_stack[comp->loop_depth - 1].start = comp->code_size;
    }
}

static void emit_store_ident(as_compiler_t* comp, const char* name)
{
    int local_idx = as_resolve_local(comp, name);
    if (local_idx >= 0) {
        if (comp->locals[local_idx].is_cell) {
            emit_load_local(comp, local_idx);
            as_emit_op(comp, OP_SWAP);
            emit_push_num(comp, 0);
            as_emit_op(comp, OP_SWAP);
            as_emit_op(comp, OP_INDEX_SET);
        } else {
            emit_store_local(comp, local_idx);
        }
        return;
    }
    {
        uint16_t idx = as_add_symbol(comp, name);
        as_emit_op(comp, OP_STORE_GLOBAL);
        as_emit_u16(comp, idx);
    }
}

static int name_in_list(char** names, size_t count, const char* name)
{
    size_t i;
    for (i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_name_list(char*** names, size_t* count, size_t* cap, const char* name)
{
    if (name_in_list(*names, *count, name)) {
        return;
    }
    if (*count >= *cap) {
        *cap = GROW_CAPACITY(*cap);
        *names = realloc(*names, sizeof(char*) * (*cap));
    }
    (*names)[(*count)++] = strdup(name);
}

/* Resolve local variable */
int as_resolve_local(as_compiler_t* comp, const char* name) {
    for (int i = comp->local_count - 1; i >= 0; i--) {
        if (strcmp(comp->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Check if variable is in captured list */
static int find_captured(as_compiler_t* comp, const char* name) {
    for (size_t i = 0; i < comp->captured_count; i++) {
        if (strcmp(comp->captured_vars[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Add variable to captured list */
static int add_captured(as_compiler_t* comp, const char* name) {
    /* Check if already captured */
    int existing = find_captured(comp, name);
    if (existing >= 0) return existing;
    
    /* Grow if needed */
    if (comp->captured_count >= comp->captured_capacity) {
        comp->captured_capacity = GROW_CAPACITY(comp->captured_capacity);
        comp->captured_vars = realloc(comp->captured_vars, 
                                      sizeof(char*) * comp->captured_capacity);
    }
    
    /* Add new capture */
    comp->captured_vars[comp->captured_count] = strdup(name);
    return comp->captured_count++;
}

/* Forward declarations for AST scanning */
static void scan_expr_for_captures(as_compiler_t* comp, as_local_t* parent_locals, size_t parent_count,
                                   as_node_t* node, size_t param_count, as_fn_param_t* params);
static void scan_stmt_for_captures(as_compiler_t* comp, as_local_t* parent_locals, size_t parent_count,
                                   as_node_t* node, size_t param_count, as_fn_param_t* params);

/* Check if name is a parameter */
static bool is_parameter(const char* name, size_t param_count, as_fn_param_t* params) {
    for (size_t i = 0; i < param_count; i++) {
        if (params[i].dest && params[i].dest->type == NODE_IDENTIFIER) {
            as_node_identifier_t* param_id = (as_node_identifier_t*)params[i].dest;
            if (strcmp(param_id->name, name) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* Helper to resolve variable in parent locals */
static int resolve_in_parent_locals(as_local_t* parent_locals, size_t parent_count, const char* name) {
    for (int i = (int)parent_count - 1; i >= 0; i--) {
        if (strcmp(parent_locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Scan expression for captured variables */
static void scan_expr_for_captures(as_compiler_t* comp, as_local_t* parent_locals, size_t parent_count,
                                   as_node_t* node, size_t param_count, as_fn_param_t* params) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_IDENTIFIER: {
            as_node_identifier_t* id = (as_node_identifier_t*)node;
            const char* name = id->name;
            
            /* Skip if it's a parameter of the inner function */
            if (is_parameter(name, param_count, params)) {
                break;
            }
            
            /* Check if it exists in parent scope */
            int local_idx = resolve_in_parent_locals(parent_locals, parent_count, name);
            
            /* If it's a local in the outer scope, capture it (not globals) */
            if (local_idx >= 0) {
                add_captured(comp, name);
            }
            break;
        }
        
        case NODE_CALL: {
            as_node_call_t* call = (as_node_call_t*)node;
            scan_expr_for_captures(comp, parent_locals, parent_count, call->target, param_count, params);
            for (size_t i = 0; i < call->arg_count; i++) {
                scan_expr_for_captures(comp, parent_locals, parent_count, call->args[i], param_count, params);
            }
            break;
        }
        
        case NODE_PROP: {
            as_node_prop_t* prop = (as_node_prop_t*)node;
            scan_expr_for_captures(comp, parent_locals, parent_count, prop->target, param_count, params);
            break;
        }
        
        case NODE_INDEX: {
            as_node_index_t* idx = (as_node_index_t*)node;
            scan_expr_for_captures(comp, parent_locals, parent_count, idx->target, param_count, params);
            scan_expr_for_captures(comp, parent_locals, parent_count, idx->index, param_count, params);
            break;
        }
        
        /* Binary operations */
        case NODE_ADD: case NODE_SUB: case NODE_MUL: case NODE_DIV:
        case NODE_REM: case NODE_POW: case NODE_EQ: case NODE_NEQ:
        case NODE_LT: case NODE_LTEQ: case NODE_GT: case NODE_GTEQ:
        case NODE_AND: case NODE_OR: {
            as_node_binary_t* bin = (as_node_binary_t*)node;
            scan_expr_for_captures(comp, parent_locals, parent_count, bin->left, param_count, params);
            scan_expr_for_captures(comp, parent_locals, parent_count, bin->right, param_count, params);
            break;
        }
        
        /* Unary operations */
        case NODE_NOT: case NODE_MINUS: case NODE_PLUS: {
            as_node_unary_t* unary = (as_node_unary_t*)node;
            scan_expr_for_captures(comp, parent_locals, parent_count, unary->expr, param_count, params);
            break;
        }
        
        /* Literals - no captures */
        case NODE_NULL: case NODE_BOOL: case NODE_NUM: case NODE_STR:
            break;

        case NODE_FN: {
            as_node_fn_t* fn = (as_node_fn_t*)node;
            size_t i;
            for (i = 0; i < fn->statement_count; i++) {
                scan_stmt_for_captures(comp, parent_locals, parent_count,
                                       fn->statements[i], fn->param_count, fn->params);
            }
            break;
        }
        case NODE_OBJ: {
            as_node_obj_t* obj = (as_node_obj_t*)node;
            size_t i;
            for (i = 0; i < obj->entry_count; i++) {
                scan_expr_for_captures(comp, parent_locals, parent_count,
                                       obj->entries[i].value, param_count, params);
            }
            break;
        }
        case NODE_ARR: {
            as_node_arr_t* arr = (as_node_arr_t*)node;
            size_t i;
            for (i = 0; i < arr->element_count; i++) {
                scan_expr_for_captures(comp, parent_locals, parent_count,
                                       arr->elements[i], param_count, params);
            }
            break;
        }
        case NODE_BLOCK: {
            as_node_block_t* block = (as_node_block_t*)node;
            size_t i;
            for (i = 0; i < block->statement_count; i++) {
                scan_stmt_for_captures(comp, parent_locals, parent_count,
                                       block->statements[i], param_count, params);
            }
            break;
        }
        case NODE_TMPL: {
            as_node_tmpl_t* tmpl = (as_node_tmpl_t*)node;
            size_t i;
            for (i = 0; i < tmpl->value_count; i++) {
                scan_expr_for_captures(comp, parent_locals, parent_count,
                                       tmpl->values[i], param_count, params);
            }
            break;
        }
        case NODE_IF: {
            as_node_if_t* if_node = (as_node_if_t*)node;
            size_t i;
            scan_expr_for_captures(comp, parent_locals, parent_count, if_node->cond, param_count, params);
            scan_stmt_for_captures(comp, parent_locals, parent_count, if_node->then_node, param_count, params);
            for (i = 0; i < if_node->elseif_count; i++) {
                scan_expr_for_captures(comp, parent_locals, parent_count, if_node->elseif_conds[i], param_count, params);
                scan_stmt_for_captures(comp, parent_locals, parent_count, if_node->elseif_then[i], param_count, params);
            }
            if (if_node->else_node) {
                scan_stmt_for_captures(comp, parent_locals, parent_count, if_node->else_node, param_count, params);
            }
            break;
        }
        
        default:
            /* For other node types, don't scan (safe default) */
            break;
    }
}

/* Scan statement for captured variables */
static void scan_stmt_for_captures(as_compiler_t* comp, as_local_t* parent_locals, size_t parent_count,
                                   as_node_t* node, size_t param_count, as_fn_param_t* params) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_RETURN: {
            as_node_return_t* ret = (as_node_return_t*)node;
            if (ret->expr) {
                scan_expr_for_captures(comp, parent_locals, parent_count, ret->expr, param_count, params);
            }
            break;
        }
        
        case NODE_IF: {
            as_node_if_t* if_node = (as_node_if_t*)node;
            size_t ei;
            scan_expr_for_captures(comp, parent_locals, parent_count, if_node->cond, param_count, params);
            scan_stmt_for_captures(comp, parent_locals, parent_count, if_node->then_node, param_count, params);
            for (ei = 0; ei < if_node->elseif_count; ei++) {
                scan_expr_for_captures(comp, parent_locals, parent_count, if_node->elseif_conds[ei], param_count, params);
                scan_stmt_for_captures(comp, parent_locals, parent_count, if_node->elseif_then[ei], param_count, params);
            }
            if (if_node->else_node) {
                scan_stmt_for_captures(comp, parent_locals, parent_count, if_node->else_node, param_count, params);
            }
            break;
        }

        case NODE_DEF: {
            as_node_def_t* def = (as_node_def_t*)node;
            scan_expr_for_captures(comp, parent_locals, parent_count, def->expr, param_count, params);
            break;
        }
        
        case NODE_BLOCK: {
            as_node_block_t* block = (as_node_block_t*)node;
            for (size_t i = 0; i < block->statement_count; i++) {
                scan_stmt_for_captures(comp, parent_locals, parent_count, block->statements[i], param_count, params);
            }
            break;
        }
        
        default:
            /* Try as expression */
            scan_expr_for_captures(comp, parent_locals, parent_count, node, param_count, params);
            break;
    }
}

static void walk_for_cells(as_node_t* node, char*** names, size_t* count, size_t* cap, int in_nested)
{
    size_t i;
    if (!node) {
        return;
    }
    if (node->type == NODE_FN) {
        as_node_fn_t* fn = (as_node_fn_t*)node;
        for (i = 0; i < fn->statement_count; i++) {
            walk_for_cells(fn->statements[i], names, count, cap, 1);
        }
        return;
    }
    if (in_nested && node->type == NODE_IDENTIFIER) {
        as_node_identifier_t* id = (as_node_identifier_t*)node;
        if (id->name) {
            add_name_list(names, count, cap, id->name);
        }
        return;
    }
    switch (node->type) {
        case NODE_BLOCK: {
            as_node_block_t* b = (as_node_block_t*)node;
            for (i = 0; i < b->statement_count; i++) {
                walk_for_cells(b->statements[i], names, count, cap, in_nested);
            }
            break;
        }
        case NODE_IF: {
            as_node_if_t* n = (as_node_if_t*)node;
            walk_for_cells(n->cond, names, count, cap, in_nested);
            walk_for_cells(n->then_node, names, count, cap, in_nested);
            for (i = 0; i < n->elseif_count; i++) {
                walk_for_cells(n->elseif_conds[i], names, count, cap, in_nested);
                walk_for_cells(n->elseif_then[i], names, count, cap, in_nested);
            }
            walk_for_cells(n->else_node, names, count, cap, in_nested);
            break;
        }
        case NODE_DEF: {
            as_node_def_t* d = (as_node_def_t*)node;
            walk_for_cells(d->expr, names, count, cap, in_nested);
            break;
        }
        case NODE_ASSIGN:
        case NODE_ADD_ASSIGN:
        case NODE_SUB_ASSIGN: {
            as_node_assign_t* a = (as_node_assign_t*)node;
            walk_for_cells(a->dest, names, count, cap, in_nested);
            walk_for_cells(a->expr, names, count, cap, in_nested);
            break;
        }
        case NODE_RETURN: {
            walk_for_cells(((as_node_return_t*)node)->expr, names, count, cap, in_nested);
            break;
        }
        case NODE_CALL: {
            as_node_call_t* c = (as_node_call_t*)node;
            walk_for_cells(c->target, names, count, cap, in_nested);
            for (i = 0; i < c->arg_count; i++) {
                walk_for_cells(c->args[i], names, count, cap, in_nested);
            }
            break;
        }
        case NODE_OBJ: {
            as_node_obj_t* o = (as_node_obj_t*)node;
            for (i = 0; i < o->entry_count; i++) {
                walk_for_cells(o->entries[i].value, names, count, cap, in_nested);
            }
            break;
        }
        case NODE_ARR: {
            as_node_arr_t* a = (as_node_arr_t*)node;
            for (i = 0; i < a->element_count; i++) {
                walk_for_cells(a->elements[i], names, count, cap, in_nested);
            }
            break;
        }
        case NODE_PROP:
            walk_for_cells(((as_node_prop_t*)node)->target, names, count, cap, in_nested);
            break;
        case NODE_INDEX: {
            as_node_index_t* x = (as_node_index_t*)node;
            walk_for_cells(x->target, names, count, cap, in_nested);
            walk_for_cells(x->index, names, count, cap, in_nested);
            break;
        }
        case NODE_ADD: case NODE_SUB: case NODE_MUL: case NODE_DIV:
        case NODE_REM: case NODE_POW: case NODE_EQ: case NODE_NEQ:
        case NODE_LT: case NODE_LTEQ: case NODE_GT: case NODE_GTEQ:
        case NODE_AND: case NODE_OR: {
            as_node_binary_t* b = (as_node_binary_t*)node;
            walk_for_cells(b->left, names, count, cap, in_nested);
            walk_for_cells(b->right, names, count, cap, in_nested);
            break;
        }
        case NODE_NOT: case NODE_MINUS: case NODE_PLUS:
            walk_for_cells(((as_node_unary_t*)node)->expr, names, count, cap, in_nested);
            break;
        case NODE_TMPL: {
            as_node_tmpl_t* t = (as_node_tmpl_t*)node;
            for (i = 0; i < t->value_count; i++) {
                walk_for_cells(t->values[i], names, count, cap, in_nested);
            }
            break;
        }
        case NODE_WHILE:
            walk_for_cells(((as_node_while_t*)node)->cond, names, count, cap, in_nested);
            walk_for_cells(((as_node_while_t*)node)->body, names, count, cap, in_nested);
            break;
        case NODE_FOR: {
            as_node_for_t* f = (as_node_for_t*)node;
            walk_for_cells(f->from, names, count, cap, in_nested);
            walk_for_cells(f->to, names, count, cap, in_nested);
            walk_for_cells(f->times, names, count, cap, in_nested);
            walk_for_cells(f->body, names, count, cap, in_nested);
            break;
        }
        case NODE_EACH: {
            as_node_each_t* e = (as_node_each_t*)node;
            walk_for_cells(e->items, names, count, cap, in_nested);
            walk_for_cells(e->body, names, count, cap, in_nested);
            break;
        }
        case NODE_LOOP: {
            as_node_loop_t* l = (as_node_loop_t*)node;
            for (i = 0; i < l->statement_count; i++) {
                walk_for_cells(l->statements[i], names, count, cap, in_nested);
            }
            break;
        }
        case NODE_MATCH: {
            as_node_match_t* m = (as_node_match_t*)node;
            walk_for_cells(m->about, names, count, cap, in_nested);
            for (i = 0; i < m->case_count; i++) {
                walk_for_cells(m->cases[i].q, names, count, cap, in_nested);
                walk_for_cells(m->cases[i].a, names, count, cap, in_nested);
            }
            walk_for_cells(m->default_case, names, count, cap, in_nested);
            break;
        }
        default:
            break;
    }
}

/* Resolve global symbol */
int as_resolve_symbol(as_compiler_t* comp, const char* name) {
    for (size_t i = 0; i < comp->symbol_count; i++) {
        if (strcmp(comp->symbols[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Add local variable */
void add_local(as_compiler_t* comp, const char* name, bool is_mutable) {
    if (comp->local_count >= comp->local_capacity) {
        comp->local_capacity = GROW_CAPACITY(comp->local_capacity);
        comp->locals = realloc(comp->locals, sizeof(as_local_t) * comp->local_capacity);
    }
    
    as_local_t* local = &comp->locals[comp->local_count++];
    local->name = strdup(name);
    local->depth = comp->scope_depth;
    local->is_mutable = is_mutable;
    local->is_cell = name_in_list(comp->cell_names, comp->cell_count, name);
}

/* Begin scope */
void as_begin_scope(as_compiler_t* comp) {
    comp->scope_depth++;
}

/* End scope */
void as_end_scope(as_compiler_t* comp) {
    comp->scope_depth--;
    
    /* Drop locals from this scope (values live in the VM locals array) */
    while (comp->local_count > 0 && 
           comp->locals[comp->local_count - 1].depth > comp->scope_depth) {
        free(comp->locals[comp->local_count - 1].name);
        comp->local_count--;
    }
}

/* Emit debug info */
void as_emit_debug_info(as_compiler_t* comp, int line, int column) {
    if (!comp->config.debug_info) return;
    
    if (comp->debug_count >= comp->debug_capacity) {
        comp->debug_capacity = GROW_CAPACITY(comp->debug_capacity);
        comp->debug_info = realloc(comp->debug_info, 
                                    sizeof(as_debug_info_t) * comp->debug_capacity);
    }
    
    as_debug_info_t* info = &comp->debug_info[comp->debug_count++];
    info->pc = comp->code_size;
    info->line = line;
    info->column = column;
}

/* Forward declarations */
static void compile_expression(as_compiler_t* comp, as_node_t* node);
static void compile_statement(as_compiler_t* comp, as_node_t* node);

/* Bind TOS to a destructuring pattern (`let [a,{value:b}] = ...`). Consumes TOS. */
static void compile_destructure_def(as_compiler_t* comp, as_node_t* dest, bool is_mut)
{
    if (!dest) {
        as_emit_op(comp, OP_POP);
        return;
    }
    if (dest->type == NODE_IDENTIFIER) {
        as_node_identifier_t* id = (as_node_identifier_t*)dest;
        if (comp->scope_depth > 0) {
            add_local(comp, id->name, is_mut);
            emit_store_local(comp, (int)comp->local_count - 1);
            if (comp->locals[comp->local_count - 1].is_cell) {
                emit_box_local(comp, (int)comp->local_count - 1);
            }
        } else {
            uint16_t idx = as_add_symbol(comp, id->name);
            if (comp->symbol_mutable) {
                comp->symbol_mutable[idx] = is_mut;
            }
            as_emit_op(comp, OP_STORE_GLOBAL);
            as_emit_u16(comp, idx);
        }
        return;
    }
    if (dest->type == NODE_ARR) {
        as_node_arr_t* pattern = (as_node_arr_t*)dest;
        size_t i;
        for (i = 0; i < pattern->element_count; i++) {
            as_emit_op(comp, OP_DUP);
            emit_push_num(comp, (double)i);
            as_emit_op(comp, OP_INDEX_GET);
            compile_destructure_def(comp, pattern->elements[i], is_mut);
        }
        as_emit_op(comp, OP_POP);
        return;
    }
    if (dest->type == NODE_OBJ) {
        as_node_obj_t* pattern = (as_node_obj_t*)dest;
        size_t i;
        for (i = 0; i < pattern->entry_count; i++) {
            const char* key = pattern->entries[i].key;
            as_emit_op(comp, OP_DUP);
            {
                uint16_t idx = as_add_symbol(comp, key);
                as_emit_op(comp, OP_PROP_GET);
                as_emit_u16(comp, idx);
            }
            if (pattern->entries[i].value) {
                compile_destructure_def(comp, pattern->entries[i].value, is_mut);
            } else {
                if (comp->scope_depth > 0) {
                    add_local(comp, key, is_mut);
                    emit_store_local(comp, (int)comp->local_count - 1);
                } else {
                    uint16_t idx = as_add_symbol(comp, key);
                    if (comp->symbol_mutable) {
                        comp->symbol_mutable[idx] = is_mut;
                    }
                    as_emit_op(comp, OP_STORE_GLOBAL);
                    as_emit_u16(comp, idx);
                }
            }
        }
        as_emit_op(comp, OP_POP);
        return;
    }
    as_emit_op(comp, OP_POP);
}

/* Assign TOS into an existing destructuring destination. Consumes TOS. */
static void compile_destructure_assign(as_compiler_t* comp, as_node_t* dest)
{
    if (!dest) {
        as_emit_op(comp, OP_POP);
        return;
    }
    if (dest->type == NODE_IDENTIFIER) {
        as_node_identifier_t* id = (as_node_identifier_t*)dest;
        emit_store_ident(comp, id->name);
        return;
    }
    if (dest->type == NODE_ARR) {
        as_node_arr_t* pattern = (as_node_arr_t*)dest;
        size_t i;
        for (i = 0; i < pattern->element_count; i++) {
            as_emit_op(comp, OP_DUP);
            emit_push_num(comp, (double)i);
            as_emit_op(comp, OP_INDEX_GET);
            compile_destructure_assign(comp, pattern->elements[i]);
        }
        as_emit_op(comp, OP_POP);
        return;
    }
    if (dest->type == NODE_OBJ) {
        as_node_obj_t* pattern = (as_node_obj_t*)dest;
        size_t i;
        for (i = 0; i < pattern->entry_count; i++) {
            const char* key = pattern->entries[i].key;
            as_emit_op(comp, OP_DUP);
            {
                uint16_t idx = as_add_symbol(comp, key);
                as_emit_op(comp, OP_PROP_GET);
                as_emit_u16(comp, idx);
            }
            if (pattern->entries[i].value) {
                compile_destructure_assign(comp, pattern->entries[i].value);
            } else {
                emit_store_ident(comp, key);
            }
        }
        as_emit_op(comp, OP_POP);
        return;
    }
    as_emit_op(comp, OP_POP);
}

/* Compile a literal node */
static void compile_literal(as_compiler_t* comp, as_node_t* node) {
    switch (node->type) {
        case NODE_NUM: {
            as_node_num_t* num = (as_node_num_t*)node;
            uint16_t idx = as_add_const_num(comp, num->value);
            as_emit_op(comp, OP_PUSH_CONST);
            as_emit_u16(comp, idx);
            break;
        }
        case NODE_STR: {
            as_node_str_t* str = (as_node_str_t*)node;
            uint16_t idx = as_add_const_str(comp, str->value);
            as_emit_op(comp, OP_PUSH_CONST);
            as_emit_u16(comp, idx);
            break;
        }
        case NODE_BOOL: {
            as_node_bool_t* boolean = (as_node_bool_t*)node;
            uint16_t idx = as_add_const_bool(comp, boolean->value);
            as_emit_op(comp, OP_PUSH_CONST);
            as_emit_u16(comp, idx);
            break;
        }
        case NODE_NULL: {
            uint16_t idx = as_add_const_null(comp);
            as_emit_op(comp, OP_PUSH_CONST);
            as_emit_u16(comp, idx);
            break;
        }
        default:
            fprintf(stderr, "Warning: Unsupported literal type: %d\n", node->type);
            break;
    }
}

/* Compile an identifier */
static void compile_identifier(as_compiler_t* comp, as_node_t* node) {
    as_node_identifier_t* id = (as_node_identifier_t*)node;
    const char* name = id->name;
    
    /* Try local first */
    int local_idx = as_resolve_local(comp, name);
    if (local_idx >= 0) {
        emit_load_local(comp, local_idx);
        if (comp->locals[local_idx].is_cell) {
            emit_push_num(comp, 0);
            as_emit_op(comp, OP_INDEX_GET);
        }
        return;
    }
    
    /* Namespace-qualified name */
    if (id->namespace) {
        char* ns_name;
        if (comp->ns_prefix) {
            char* mid = join_ns(comp->ns_prefix, id->namespace);
            ns_name = join_ns(mid, name);
            free(mid);
        } else {
            ns_name = join_ns(id->namespace, name);
        }
        {
            uint16_t idx = as_add_symbol(comp, ns_name);
            as_emit_op(comp, OP_LOAD_GLOBAL);
            as_emit_u16(comp, idx);
        }
        free(ns_name);
        return;
    }

    /* Unqualified name inside a namespace — `ai` in `:: Foo` is `Foo:ai`. */
    if (comp->ns_prefix) {
        char* q = join_ns(comp->ns_prefix, name);
        {
            uint16_t idx = as_add_symbol(comp, q);
            as_emit_op(comp, OP_LOAD_GLOBAL);
            as_emit_u16(comp, idx);
        }
        free(q);
        return;
    }

    /* Try global */
    int sym_idx = as_resolve_symbol(comp, name);
    if (sym_idx >= 0) {
        as_emit_op(comp, OP_LOAD_GLOBAL);
        as_emit_u16(comp, (uint16_t)sym_idx);
        return;
    }
    
    /* Undefined - add as global and load */
    {
        uint16_t idx = as_add_symbol(comp, name);
        as_emit_op(comp, OP_LOAD_GLOBAL);
        as_emit_u16(comp, idx);
    }
}

/* Compile binary operation - handles arithmetic, comparison, logical */
static void compile_binary_op(as_compiler_t* comp, as_node_t* node) {
    as_node_binary_t* bin = (as_node_binary_t*)node;

    if (node->type == NODE_AND) {
        compile_expression(comp, bin->left);
        as_emit_op(comp, OP_TYPE_CHECK);
        {
            size_t jf = as_emit_jump(comp, OP_JUMP_IF_FALSE);
            as_emit_op(comp, OP_POP);
            compile_expression(comp, bin->right);
            as_emit_op(comp, OP_TYPE_CHECK);
            as_emit_op(comp, OP_NOT);
            as_emit_op(comp, OP_NOT);
            {
                size_t end = as_emit_jump(comp, OP_JUMP);
                as_patch_jump(comp, jf);
                as_emit_op(comp, OP_POP);
                {
                    uint16_t idx = as_add_const_bool(comp, false);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, idx);
                }
                as_patch_jump(comp, end);
            }
        }
        return;
    }
    if (node->type == NODE_OR) {
        compile_expression(comp, bin->left);
        as_emit_op(comp, OP_TYPE_CHECK);
        {
            size_t jt = as_emit_jump(comp, OP_JUMP_IF_TRUE);
            as_emit_op(comp, OP_POP);
            compile_expression(comp, bin->right);
            as_emit_op(comp, OP_TYPE_CHECK);
            as_emit_op(comp, OP_NOT);
            as_emit_op(comp, OP_NOT);
            {
                size_t end = as_emit_jump(comp, OP_JUMP);
                as_patch_jump(comp, jt);
                as_emit_op(comp, OP_POP);
                {
                    uint16_t idx = as_add_const_bool(comp, true);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, idx);
                }
                as_patch_jump(comp, end);
            }
        }
        return;
    }
    
    /* Compile operands */
    compile_expression(comp, bin->left);
    compile_expression(comp, bin->right);
    
    /* Emit operation */
    switch (node->type) {
        case NODE_ADD:   as_emit_op(comp, OP_ADD); break;
        case NODE_SUB:   as_emit_op(comp, OP_SUB); break;
        case NODE_MUL:   as_emit_op(comp, OP_MUL); break;
        case NODE_DIV:   as_emit_op(comp, OP_DIV); break;
        case NODE_REM:   as_emit_op(comp, OP_REM); break;
        case NODE_POW:   as_emit_op(comp, OP_POW); break;
        case NODE_EQ:    as_emit_op(comp, OP_EQ); break;
        case NODE_NEQ:   as_emit_op(comp, OP_NEQ); break;
        case NODE_LT:    as_emit_op(comp, OP_LT); break;
        case NODE_LTEQ:  as_emit_op(comp, OP_LTE); break;
        case NODE_GT:    as_emit_op(comp, OP_GT); break;
        case NODE_GTEQ:  as_emit_op(comp, OP_GTE); break;
        case NODE_AND:   as_emit_op(comp, OP_AND); break;
        case NODE_OR:    as_emit_op(comp, OP_OR); break;
        default:
            fprintf(stderr, "Warning: Unsupported binary operator: %d\n", node->type);
            break;
    }
}

/* Compile unary operation */
static void compile_unary_op(as_compiler_t* comp, as_node_t* node) {
    as_node_unary_t* unary = (as_node_unary_t*)node;
    
    /* Compile operand */
    compile_expression(comp, unary->expr);
    
    /* Emit operation */
    switch (node->type) {
        case NODE_MINUS:  as_emit_op(comp, OP_NEG); break;
        case NODE_PLUS:   /* Unary plus is no-op */ break;
        case NODE_NOT:    as_emit_op(comp, OP_NOT); break;
        default:
            fprintf(stderr, "Warning: Unsupported unary operator: %d\n", node->type);
            break;
    }
}

/* Compile expression */
static void compile_expression(as_compiler_t* comp, as_node_t* node) {
    if (!node) return;
    
    /* Emit debug info if available */
    if (comp->config.debug_info) {
        as_emit_debug_info(comp, node->loc.start.line, node->loc.start.column);
    }
    
    switch (node->type) {
        /* Literals */
        case NODE_NUM:
        case NODE_STR:
        case NODE_BOOL:
        case NODE_NULL:
            compile_literal(comp, node);
            break;
            
        case NODE_IDENTIFIER:
            compile_identifier(comp, node);
            break;
            
        /* Binary operations */
        case NODE_ADD:
        case NODE_SUB:
        case NODE_MUL:
        case NODE_DIV:
        case NODE_REM:
        case NODE_POW:
        case NODE_EQ:
        case NODE_NEQ:
        case NODE_LT:
        case NODE_LTEQ:
        case NODE_GT:
        case NODE_GTEQ:
        case NODE_AND:
        case NODE_OR:
            compile_binary_op(comp, node);
            break;
            
        /* Unary operations */
        case NODE_PLUS:
        case NODE_MINUS:
        case NODE_NOT:
            compile_unary_op(comp, node);
            break;
            
        /* Function call */
        case NODE_CALL: {
            as_node_call_t* call = (as_node_call_t*)node;
            compile_expression(comp, call->target);
            for (size_t i = 0; i < call->arg_count; i++) {
                compile_expression(comp, call->args[i]);
            }
            as_emit_op(comp, OP_CALL);
            as_emit_u8(comp, (uint8_t)call->arg_count);
            break;
        }
            
        /* Property access */
        case NODE_PROP: {
            as_node_prop_t* prop = (as_node_prop_t*)node;
            compile_expression(comp, prop->target);
            uint16_t idx = as_add_symbol(comp, prop->name);
            as_emit_op(comp, OP_PROP_GET);
            as_emit_u16(comp, idx);
            break;
        }
            
        /* Index access */
        case NODE_INDEX: {
            as_node_index_t* index = (as_node_index_t*)node;
            compile_expression(comp, index->target);
            compile_expression(comp, index->index);
            as_emit_op(comp, OP_INDEX_GET);
            break;
        }
            
        /* Array literal */
        case NODE_ARR: {
            as_node_arr_t* arr = (as_node_arr_t*)node;
            for (size_t i = 0; i < arr->element_count; i++) {
                compile_expression(comp, arr->elements[i]);
            }
            as_emit_op(comp, OP_BUILD_ARR);
            as_emit_u16(comp, (uint16_t)arr->element_count);
            break;
        }
            
        /* Object literal */
        case NODE_OBJ: {
            as_node_obj_t* obj = (as_node_obj_t*)node;
            for (size_t i = 0; i < obj->entry_count; i++) {
                /* Key - always a string */
                uint16_t idx = as_add_const_str(comp, obj->entries[i].key);
                as_emit_op(comp, OP_PUSH_CONST);
                as_emit_u16(comp, idx);
                /* Value */
                compile_expression(comp, obj->entries[i].value);
            }
            as_emit_op(comp, OP_BUILD_OBJ);
            as_emit_u16(comp, (uint16_t)obj->entry_count);
            break;
        }
            
        /* Template string — always produce a string (even for a single number). */
        case NODE_TMPL: {
            as_node_tmpl_t* tmpl = (as_node_tmpl_t*)node;
            size_t i;
            {
                uint16_t idx = as_add_const_str(comp, "");
                as_emit_op(comp, OP_PUSH_CONST);
                as_emit_u16(comp, idx);
            }
            for (i = 0; i < tmpl->value_count; i++) {
                compile_expression(comp, tmpl->values[i]);
                as_emit_op(comp, OP_ADD);
            }
            break;
        }
            
        /* Function definition */
        case NODE_FN: {
            as_node_fn_t* fn = (as_node_fn_t*)node;
            
            /* Jump over function body */
            size_t jump_over = as_emit_jump(comp, OP_JUMP);
            
            /* Mark function entry point (current PC) */
            size_t fn_start_pc = comp->code_size;
            
            /* Save outer function's local state */
            as_local_t* saved_locals = comp->locals;
            size_t saved_local_count = comp->local_count;
            size_t saved_local_capacity = comp->local_capacity;
            int saved_scope_depth = comp->scope_depth;
            
            /* Create new locals array for inner function */
            comp->locals = NULL;
            comp->local_count = 0;
            comp->local_capacity = 0;
            comp->scope_depth = 0;
            
            /* Compile function body */
            as_begin_scope(comp);
            
            /* Initialize captured variables for this function */
            size_t saved_capture_count = comp->captured_count;
            size_t saved_capture_capacity = comp->captured_capacity;
            char** saved_captured_vars = comp->captured_vars;
            
            comp->captured_count = 0;
            comp->captured_capacity = 0;
            comp->captured_vars = NULL;
            
            /* Scan function body to detect captured variables (with parent's locals) */
            for (size_t i = 0; i < fn->statement_count; i++) {
                scan_stmt_for_captures(comp, saved_locals, saved_local_count,
                                      fn->statements[i], fn->param_count, fn->params);
            }
            
            size_t capture_count = comp->captured_count;
            char** saved_cells = comp->cell_names;
            size_t saved_cell_count = comp->cell_count;
            size_t saved_cell_cap = comp->cell_capacity;
            comp->cell_names = NULL;
            comp->cell_count = 0;
            comp->cell_capacity = 0;
            {
                size_t si;
                for (si = 0; si < fn->statement_count; si++) {
                    walk_for_cells(fn->statements[si], &comp->cell_names,
                                   &comp->cell_count, &comp->cell_capacity, 0);
                }
            }
            
            /* Register captured variables as locals FIRST (they come before parameters) */
            for (size_t i = 0; i < capture_count; i++) {
                add_local(comp, comp->captured_vars[i], false);
                comp->locals[comp->local_count - 1].is_cell = true;
            }
            
            /* Register parameters as local variables (after captures) */
            {
                as_node_t* pattern_dest[32];
                int pattern_local[32];
                size_t npat = 0;
                for (size_t i = 0; i < fn->param_count; i++) {
                    if (fn->params[i].dest && fn->params[i].dest->type == NODE_IDENTIFIER) {
                        as_node_identifier_t* param_id = (as_node_identifier_t*)fn->params[i].dest;
                        add_local(comp, param_id->name, true);
                    } else if (fn->params[i].dest) {
                        char hid[32];
                        unique_temp(comp, hid, sizeof(hid), "p");
                        add_local(comp, hid, false);
                        if (npat < 32) {
                            pattern_dest[npat] = fn->params[i].dest;
                            pattern_local[npat] = (int)comp->local_count - 1;
                            npat++;
                        }
                    } else {
                        char hid[32];
                        unique_temp(comp, hid, sizeof(hid), "p");
                        add_local(comp, hid, false);
                    }
                }
                for (size_t i = 0; i < npat; i++) {
                    emit_load_local(comp, pattern_local[i]);
                    compile_destructure_def(comp, pattern_dest[i], false);
                }
                /* Box parameters captured by nested functions (captures are already boxes). */
                {
                    size_t li;
                    for (li = capture_count; li < comp->local_count; li++) {
                        if (comp->locals[li].is_cell) {
                            emit_box_local(comp, (int)li);
                        }
                    }
                }
            }
            
            /* Compile function statements; last expression is the return value */
            for (size_t i = 0; i < fn->statement_count; i++) {
                int last = (i + 1 == fn->statement_count);
                as_node_t* st = fn->statements[i];
                if (last && st->type != NODE_RETURN && st->type != NODE_DEF &&
                    st->type != NODE_ASSIGN && st->type != NODE_ADD_ASSIGN &&
                    st->type != NODE_SUB_ASSIGN && st->type != NODE_WHILE &&
                    st->type != NODE_FOR && st->type != NODE_EACH &&
                    st->type != NODE_LOOP && st->type != NODE_BREAK &&
                    st->type != NODE_CONTINUE && st->type != NODE_NAMESPACE) {
                    compile_expression(comp, st);
                    as_emit_op(comp, OP_RETURN_VALUE);
                } else {
                    compile_statement(comp, st);
                }
            }
            as_end_scope(comp);

            {
                size_t ci;
                for (ci = 0; ci < comp->cell_count; ci++) {
                    free(comp->cell_names[ci]);
                }
                free(comp->cell_names);
                comp->cell_names = saved_cells;
                comp->cell_count = saved_cell_count;
                comp->cell_capacity = saved_cell_cap;
            }
            
            /* Clean up inner function's locals */
            for (size_t i = 0; i < comp->local_count; i++) {
                free(comp->locals[i].name);
            }
            free(comp->locals);
            
            /* Restore outer function's local state */
            comp->locals = saved_locals;
            comp->local_count = saved_local_count;
            comp->local_capacity = saved_local_capacity;
            comp->scope_depth = saved_scope_depth;
            
            /* If no explicit return, add return null */
            as_emit_op(comp, OP_PUSH_NULL);
            as_emit_op(comp, OP_RETURN_VALUE);
            
            /* Patch jump to here */
            as_patch_jump(comp, jump_over);
            
            /* Closure: [pc, param_count, ncap, required_mask, ...captures, ...defaults] */
            {
                uint32_t required_mask = 0;
                size_t pi;
                uint16_t fn_pc_idx = as_add_const_num(comp, (double)fn_start_pc);
                as_emit_op(comp, OP_PUSH_CONST);
                as_emit_u16(comp, fn_pc_idx);
                {
                    uint16_t param_count_idx = as_add_const_num(comp, (double)fn->param_count);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, param_count_idx);
                }
                {
                    uint16_t ncap_idx = as_add_const_num(comp, (double)capture_count);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, ncap_idx);
                }
                for (pi = 0; pi < fn->param_count && pi < 32; pi++) {
                    if (!fn->params[pi].default_value && !fn->params[pi].optional) {
                        required_mask |= (1u << pi);
                    }
                }
                {
                    uint16_t mask_idx = as_add_const_num(comp, (double)required_mask);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, mask_idx);
                }
                for (size_t i = 0; i < capture_count; i++) {
                    const char* var_name = comp->captured_vars[i];
                    int local_idx = as_resolve_local(comp, var_name);
                    if (local_idx >= 0) {
                        emit_load_local(comp, local_idx);
                        if (!comp->locals[local_idx].is_cell) {
                            as_emit_op(comp, OP_BUILD_ARR);
                            as_emit_u16(comp, 1);
                        }
                    } else {
                        uint16_t idx = as_add_symbol(comp, var_name);
                        as_emit_op(comp, OP_LOAD_GLOBAL);
                        as_emit_u16(comp, idx);
                    }
                }
                for (pi = 0; pi < fn->param_count; pi++) {
                    if (fn->params[pi].default_value) {
                        compile_expression(comp, fn->params[pi].default_value);
                    } else {
                        emit_push_null(comp);
                    }
                }
                for (pi = 0; pi < fn->param_count; pi++) {
                    const char* pname = "?";
                    uint16_t sidx;
                    if (fn->params[pi].dest && fn->params[pi].dest->type == NODE_IDENTIFIER) {
                        as_node_identifier_t* id = (as_node_identifier_t*)fn->params[pi].dest;
                        if (id->name) {
                            pname = id->name;
                        }
                    }
                    sidx = as_add_const_str(comp, pname);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, sidx);
                }
                as_emit_op(comp, OP_BUILD_ARR);
                as_emit_u16(comp, (uint16_t)(4 + capture_count + fn->param_count + fn->param_count));
                /* Wrap the closure array as a function value. */
                as_emit_op(comp, OP_MAKE_FUNCTION);
                as_emit_u16(comp, 0);
                as_emit_u8(comp, 0);
            }
            
            /* Clean up captured vars list */
            for (size_t i = 0; i < capture_count; i++) {
                free(comp->captured_vars[i]);
            }
            free(comp->captured_vars);
            
            /* Restore parent's capture state */
            comp->captured_count = saved_capture_count;
            comp->captured_capacity = saved_capture_capacity;
            comp->captured_vars = saved_captured_vars;
            
            break;
        }
            
        /* Block expression */
        case NODE_BLOCK: {
            as_node_block_t* block = (as_node_block_t*)node;
            as_begin_breakable(comp, block->label, false);
            as_begin_scope(comp);
            if (block->statement_count == 0) {
                emit_push_null(comp);
            } else {
                size_t i;
                for (i = 0; i + 1 < block->statement_count; i++) {
                    compile_statement(comp, block->statements[i]);
                }
                compile_expression(comp, block->statements[block->statement_count - 1]);
            }
            as_end_scope(comp);
            as_end_loop(comp);
            break;
        }
            
        /* If expression */
        case NODE_IF: {
            as_node_if_t* if_node = (as_node_if_t*)node;
            size_t i;
            size_t end_jumps[32];
            size_t end_count = 0;
            as_begin_breakable(comp, if_node->label, false);
            compile_expression(comp, if_node->cond);
            {
                size_t then_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                as_emit_op(comp, OP_POP);
                compile_expression(comp, if_node->then_node);
                if (end_count < 32) {
                    end_jumps[end_count++] = as_emit_jump(comp, OP_JUMP);
                }
                as_patch_jump(comp, then_jump);
                as_emit_op(comp, OP_POP);
            }
            for (i = 0; i < if_node->elseif_count; i++) {
                compile_expression(comp, if_node->elseif_conds[i]);
                {
                    size_t elif_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                    as_emit_op(comp, OP_POP);
                    compile_expression(comp, if_node->elseif_then[i]);
                    if (end_count < 32) {
                        end_jumps[end_count++] = as_emit_jump(comp, OP_JUMP);
                    }
                    as_patch_jump(comp, elif_jump);
                    as_emit_op(comp, OP_POP);
                }
            }
            if (if_node->else_node) {
                compile_expression(comp, if_node->else_node);
            } else {
                emit_push_null(comp);
            }
            for (i = 0; i < end_count; i++) {
                as_patch_jump(comp, end_jumps[i]);
            }
            as_end_loop(comp);
            break;
        }

        case NODE_EXISTS: {
            as_node_exists_t* exists = (as_node_exists_t*)node;
            if (exists->identifier && exists->identifier->type == NODE_IDENTIFIER) {
                as_node_identifier_t* id = (as_node_identifier_t*)exists->identifier;
                int local_idx = as_resolve_local(comp, id->name);
                if (local_idx >= 0) {
                    uint16_t t = as_add_const_bool(comp, true);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, t);
                } else {
                    int sym = as_resolve_symbol(comp, id->name);
                    uint16_t t = as_add_const_bool(comp, sym >= 0);
                    as_emit_op(comp, OP_PUSH_CONST);
                    as_emit_u16(comp, t);
                }
            } else {
                uint16_t t = as_add_const_bool(comp, false);
                as_emit_op(comp, OP_PUSH_CONST);
                as_emit_u16(comp, t);
            }
            break;
        }

        case NODE_MATCH: {
            as_node_match_t* match = (as_node_match_t*)node;
            size_t i;
            size_t end_jumps[32];
            size_t end_count = 0;
            as_begin_breakable(comp, match->label, false);
            compile_expression(comp, match->about);
            for (i = 0; i < match->case_count; i++) {
                as_emit_op(comp, OP_DUP);
                compile_expression(comp, match->cases[i].q);
                as_emit_op(comp, OP_EQ);
                {
                    size_t miss = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                    as_emit_op(comp, OP_POP); /* bool */
                    as_emit_op(comp, OP_POP); /* about */
                    as_begin_scope(comp);
                    compile_expression(comp, match->cases[i].a);
                    as_end_scope(comp);
                    if (end_count < 32) {
                        end_jumps[end_count++] = as_emit_jump(comp, OP_JUMP);
                    }
                    as_patch_jump(comp, miss);
                    as_emit_op(comp, OP_POP); /* bool */
                }
            }
            as_emit_op(comp, OP_POP); /* about */
            if (match->default_case) {
                as_begin_scope(comp);
                compile_expression(comp, match->default_case);
                as_end_scope(comp);
            } else {
                emit_push_null(comp);
            }
            for (i = 0; i < end_count; i++) {
                as_patch_jump(comp, end_jumps[i]);
            }
            as_end_loop(comp);
            break;
        }

        case NODE_RETURN:
        case NODE_DEF:
        case NODE_ASSIGN:
        case NODE_ADD_ASSIGN:
        case NODE_SUB_ASSIGN:
        case NODE_WHILE:
        case NODE_FOR:
        case NODE_EACH:
        case NODE_LOOP:
        case NODE_DO_WHILE:
        case NODE_BREAK:
        case NODE_CONTINUE:
        case NODE_NAMESPACE:
            compile_statement(comp, node);
            if (node->type != NODE_RETURN && node->type != NODE_BREAK) {
                emit_push_null(comp);
            }
            break;
            
        default:
            fprintf(stderr, "Warning: Unsupported expression type: %d\n", node->type);
            as_emit_op(comp, OP_PUSH_NULL);  /* Push something to maintain stack balance */
            break;
    }
}

/* Compile statement */
static void compile_statement(as_compiler_t* comp, as_node_t* node) {
    if (!node) return;
    
    /* Emit debug info if available */
    if (comp->config.debug_info) {
        as_emit_debug_info(comp, node->loc.start.line, node->loc.start.column);
    }
    
    switch (node->type) {
        /* Variable definition */
        case NODE_DEF: {
            as_node_def_t* def = (as_node_def_t*)node;
            int rec_local = -1;

            /* Pre-bind named functions so bodies can recurse via a cell capture. */
            if (def->dest && def->dest->type == NODE_IDENTIFIER &&
                def->expr && def->expr->type == NODE_FN &&
                comp->scope_depth > 0) {
                as_node_identifier_t* id = (as_node_identifier_t*)def->dest;
                add_local(comp, id->name, def->is_mut);
                rec_local = (int)comp->local_count - 1;
                emit_push_null(comp);
                emit_store_local(comp, rec_local);
                comp->locals[rec_local].is_cell = true;
                emit_box_local(comp, rec_local);
            }
            
            if (def->expr) {
                compile_expression(comp, def->expr);
            } else {
                uint16_t idx = as_add_const_null(comp);
                as_emit_op(comp, OP_PUSH_CONST);
                as_emit_u16(comp, idx);
            }

            if (rec_local >= 0) {
                emit_store_ident(comp, ((as_node_identifier_t*)def->dest)->name);
                break;
            }
            
            if (def->dest && def->dest->type == NODE_IDENTIFIER) {
                as_node_identifier_t* id = (as_node_identifier_t*)def->dest;
                
                if (comp->scope_depth > 0) {
                    add_local(comp, id->name, def->is_mut);
                    emit_store_local(comp, (int)comp->local_count - 1);
                    if (comp->locals[comp->local_count - 1].is_cell) {
                        emit_box_local(comp, (int)comp->local_count - 1);
                    }
                } else {
                    uint16_t idx = as_add_symbol(comp, id->name);
                    if (comp->symbol_mutable) {
                        comp->symbol_mutable[idx] = def->is_mut;
                    }
                    as_emit_op(comp, OP_STORE_GLOBAL);
                    as_emit_u16(comp, idx);
                }
            } else if (def->dest) {
                compile_destructure_def(comp, def->dest, def->is_mut);
            }
            break;
        }
            
        /* Assignment */
        case NODE_ASSIGN: {
            as_node_assign_t* assign = (as_node_assign_t*)node;

            if (assign->dest->type == NODE_IDENTIFIER) {
                as_node_identifier_t* id = (as_node_identifier_t*)assign->dest;
                int local_idx = as_resolve_local(comp, id->name);
                if (local_idx >= 0 && !comp->locals[local_idx].is_mutable) {
                    compile_error(comp, "Cannot assign to immutable variable");
                } else {
                    int sym = as_resolve_symbol(comp, id->name);
                    if (sym >= 0 && comp->symbol_mutable && !comp->symbol_mutable[sym]) {
                        compile_error(comp, "Cannot assign to immutable variable");
                    }
                }
                compile_expression(comp, assign->expr);
                emit_store_ident(comp, id->name);
            } else if (assign->dest->type == NODE_INDEX) {
                as_node_index_t* index = (as_node_index_t*)assign->dest;
                compile_expression(comp, index->target);
                compile_expression(comp, index->index);
                compile_expression(comp, assign->expr);
                as_emit_op(comp, OP_INDEX_SET);
            } else if (assign->dest->type == NODE_PROP) {
                as_node_prop_t* prop = (as_node_prop_t*)assign->dest;
                compile_expression(comp, prop->target);
                compile_expression(comp, assign->expr);
                {
                    uint16_t idx = as_add_symbol(comp, prop->name);
                    as_emit_op(comp, OP_PROP_SET);
                    as_emit_u16(comp, idx);
                }
            } else if (assign->dest->type == NODE_ARR || assign->dest->type == NODE_OBJ) {
                compile_expression(comp, assign->expr);
                compile_destructure_assign(comp, assign->dest);
            }
            break;
        }
            
        /* Compound assignment */
        case NODE_ADD_ASSIGN:
        case NODE_SUB_ASSIGN: {
            as_node_assign_t* assign = (as_node_assign_t*)node;
            as_opcode_t arith = node->type == NODE_ADD_ASSIGN ? OP_ADD : OP_SUB;

            if (assign->dest->type == NODE_IDENTIFIER) {
                as_node_identifier_t* id = (as_node_identifier_t*)assign->dest;
                compile_identifier(comp, assign->dest);
                compile_expression(comp, assign->expr);
                as_emit_op(comp, arith);
                emit_store_ident(comp, id->name);
            } else if (assign->dest->type == NODE_INDEX) {
                as_node_index_t* index = (as_node_index_t*)assign->dest;
                char tname[32], iname[32], vname[32];
                unique_temp(comp, tname, sizeof(tname), "t");
                unique_temp(comp, iname, sizeof(iname), "i");
                unique_temp(comp, vname, sizeof(vname), "v");
                compile_expression(comp, index->target);
                add_local(comp, tname, true);
                emit_store_local(comp, (int)comp->local_count - 1);
                compile_expression(comp, index->index);
                add_local(comp, iname, true);
                emit_store_local(comp, (int)comp->local_count - 1);
                emit_load_local(comp, as_resolve_local(comp, tname));
                emit_load_local(comp, as_resolve_local(comp, iname));
                as_emit_op(comp, OP_INDEX_GET);
                compile_expression(comp, assign->expr);
                as_emit_op(comp, arith);
                add_local(comp, vname, true);
                emit_store_local(comp, (int)comp->local_count - 1);
                emit_load_local(comp, as_resolve_local(comp, tname));
                emit_load_local(comp, as_resolve_local(comp, iname));
                emit_load_local(comp, as_resolve_local(comp, vname));
                as_emit_op(comp, OP_INDEX_SET);
            } else if (assign->dest->type == NODE_PROP) {
                as_node_prop_t* prop = (as_node_prop_t*)assign->dest;
                char tname[32];
                unique_temp(comp, tname, sizeof(tname), "t");
                compile_expression(comp, prop->target);
                add_local(comp, tname, true);
                emit_store_local(comp, (int)comp->local_count - 1);
                emit_load_local(comp, as_resolve_local(comp, tname));
                {
                    uint16_t idx = as_add_symbol(comp, prop->name);
                    as_emit_op(comp, OP_PROP_GET);
                    as_emit_u16(comp, idx);
                }
                compile_expression(comp, assign->expr);
                as_emit_op(comp, arith);
                emit_load_local(comp, as_resolve_local(comp, tname));
                as_emit_op(comp, OP_SWAP);
                {
                    uint16_t idx = as_add_symbol(comp, prop->name);
                    as_emit_op(comp, OP_PROP_SET);
                    as_emit_u16(comp, idx);
                }
            }
            break;
        }
            
        /* Return */
        case NODE_RETURN: {
            as_node_return_t* ret = (as_node_return_t*)node;
            if (ret->expr) {
                compile_expression(comp, ret->expr);
                as_emit_op(comp, OP_RETURN_VALUE);
            } else {
                as_emit_op(comp, OP_RETURN);
            }
            break;
        }
            
        /* While loop */
        case NODE_WHILE: {
            as_node_while_t* while_node = (as_node_while_t*)node;
            as_begin_breakable(comp, while_node->label, true);
            size_t loop_start = comp->code_size;
            
            compile_expression(comp, while_node->cond);
            size_t exit_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
            as_emit_op(comp, OP_POP);
            
            compile_statement(comp, while_node->body);
            
            as_emit_op(comp, OP_JUMP);
            as_emit_i16(comp, (int16_t)(loop_start - (int)comp->code_size - 2));
            
            as_patch_jump(comp, exit_jump);
            as_emit_op(comp, OP_POP);
            as_end_loop(comp);
            break;
        }

        case NODE_DO_WHILE: {
            as_node_do_while_t* dw = (as_node_do_while_t*)node;
            as_begin_breakable(comp, dw->label, true);
            size_t loop_start = comp->code_size;
            compile_statement(comp, dw->body);
            as_set_continue_target(comp);
            compile_expression(comp, dw->cond);
            as_emit_op(comp, OP_JUMP_IF_TRUE);
            as_emit_i16(comp, (int16_t)(loop_start - (int)comp->code_size - 2));
            as_emit_op(comp, OP_POP);
            as_end_loop(comp);
            break;
        }
            
        /* For loop: `for (let i = from, count)` runs `count` times from `from`. */
        case NODE_FOR: {
            as_node_for_t* for_node = (as_node_for_t*)node;
            as_begin_scope(comp);
            if (for_node->var != NULL) {
                char end_name[32];
                unique_temp(comp, end_name, sizeof(end_name), "end");
                if (for_node->from) {
                    compile_expression(comp, for_node->from);
                } else {
                    emit_push_num(comp, 0.0);
                }
                add_local(comp, for_node->var, true);
                emit_store_local(comp, (int)comp->local_count - 1);
                emit_load_local(comp, as_resolve_local(comp, for_node->var));
                compile_expression(comp, for_node->to);
                as_emit_op(comp, OP_ADD);
                add_local(comp, end_name, false);
                emit_store_local(comp, (int)comp->local_count - 1);
                as_begin_breakable(comp, for_node->label, true);
                {
                    size_t loop_start = comp->code_size;
                    int local_idx = as_resolve_local(comp, for_node->var);
                    int end_idx = as_resolve_local(comp, end_name);
                    emit_load_local(comp, local_idx);
                    emit_load_local(comp, end_idx);
                    as_emit_op(comp, OP_LT);
                    {
                        size_t exit_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                        as_emit_op(comp, OP_POP);
                        compile_statement(comp, for_node->body);
                        as_set_continue_target(comp);
                        emit_load_local(comp, local_idx);
                        emit_push_num(comp, 1.0);
                        as_emit_op(comp, OP_ADD);
                        emit_store_local(comp, local_idx);
                        as_emit_op(comp, OP_JUMP);
                        as_emit_i16(comp, (int16_t)(loop_start - (int)comp->code_size - 2));
                        as_patch_jump(comp, exit_jump);
                        as_emit_op(comp, OP_POP);
                    }
                }
                as_end_loop(comp);
            } else {
                char iname[32];
                unique_temp(comp, iname, sizeof(iname), "fi");
                emit_push_num(comp, 0.0);
                add_local(comp, iname, true);
                emit_store_local(comp, (int)comp->local_count - 1);
                as_begin_breakable(comp, for_node->label, true);
                {
                    size_t loop_start = comp->code_size;
                    int local_idx = as_resolve_local(comp, iname);
                    emit_load_local(comp, local_idx);
                    compile_expression(comp, for_node->times);
                    as_emit_op(comp, OP_LT);
                    {
                        size_t exit_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                        as_emit_op(comp, OP_POP);
                        compile_statement(comp, for_node->body);
                        as_set_continue_target(comp);
                        emit_load_local(comp, local_idx);
                        emit_push_num(comp, 1.0);
                        as_emit_op(comp, OP_ADD);
                        emit_store_local(comp, local_idx);
                        as_emit_op(comp, OP_JUMP);
                        as_emit_i16(comp, (int16_t)(loop_start - (int)comp->code_size - 2));
                        as_patch_jump(comp, exit_jump);
                        as_emit_op(comp, OP_POP);
                    }
                }
                as_end_loop(comp);
            }
            as_end_scope(comp);
            break;
        }

        case NODE_EACH: {
            as_node_each_t* each = (as_node_each_t*)node;
            char arr_name[32], i_name[32], v_name[32];
            int destructure = (each->var && each->var->type != NODE_IDENTIFIER);
            unique_temp(comp, arr_name, sizeof(arr_name), "ea");
            unique_temp(comp, i_name, sizeof(i_name), "ei");
            unique_temp(comp, v_name, sizeof(v_name), "ev");
            as_begin_scope(comp);
            compile_expression(comp, each->items);
            add_local(comp, arr_name, false);
            emit_store_local(comp, (int)comp->local_count - 1);
            emit_push_num(comp, 0.0);
            add_local(comp, i_name, true);
            emit_store_local(comp, (int)comp->local_count - 1);
            emit_push_null(comp);
            add_local(comp, v_name, true);
            emit_store_local(comp, (int)comp->local_count - 1);
            if (each->var && each->var->type == NODE_IDENTIFIER) {
                /* Keep a named local alias for the item. */
                add_local(comp, ((as_node_identifier_t*)each->var)->name, true);
                emit_load_local(comp, as_resolve_local(comp, v_name));
                emit_store_local(comp, (int)comp->local_count - 1);
            }
            as_begin_breakable(comp, each->label, true);
            {
                int arr_idx = as_resolve_local(comp, arr_name);
                int i_idx = as_resolve_local(comp, i_name);
                int v_idx = as_resolve_local(comp, v_name);
                int named_idx = -1;
                size_t loop_start;
                if (each->var && each->var->type == NODE_IDENTIFIER) {
                    named_idx = as_resolve_local(comp, ((as_node_identifier_t*)each->var)->name);
                }
                loop_start = comp->code_size;
                emit_load_local(comp, i_idx);
                emit_load_local(comp, arr_idx);
                {
                    uint16_t len_sym = as_add_symbol(comp, "len");
                    as_emit_op(comp, OP_PROP_GET);
                    as_emit_u16(comp, len_sym);
                }
                as_emit_op(comp, OP_LT);
                {
                    size_t exit_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                    as_emit_op(comp, OP_POP);
                    emit_load_local(comp, arr_idx);
                    emit_load_local(comp, i_idx);
                    as_emit_op(comp, OP_INDEX_GET);
                    emit_store_local(comp, v_idx);
                    if (named_idx >= 0) {
                        emit_load_local(comp, v_idx);
                        emit_store_local(comp, named_idx);
                    } else if (destructure) {
                        emit_load_local(comp, v_idx);
                        compile_destructure_def(comp, each->var, true);
                    }
                    compile_statement(comp, each->body);
                    as_set_continue_target(comp);
                    emit_load_local(comp, i_idx);
                    emit_push_num(comp, 1.0);
                    as_emit_op(comp, OP_ADD);
                    emit_store_local(comp, i_idx);
                    as_emit_op(comp, OP_JUMP);
                    as_emit_i16(comp, (int16_t)(loop_start - (int)comp->code_size - 2));
                    as_patch_jump(comp, exit_jump);
                    as_emit_op(comp, OP_POP);
                }
            }
            as_end_loop(comp);
            as_end_scope(comp);
            break;
        }

        case NODE_LOOP: {
            as_node_loop_t* loop = (as_node_loop_t*)node;
            as_begin_breakable(comp, loop->label, true);
            {
                size_t i;
                size_t loop_start = comp->code_size;
                for (i = 0; i < loop->statement_count; i++) {
                    compile_statement(comp, loop->statements[i]);
                }
                as_emit_op(comp, OP_JUMP);
                as_emit_i16(comp, (int16_t)(loop_start - (int)comp->code_size - 2));
            }
            as_end_loop(comp);
            break;
        }

        case NODE_NAMESPACE: {
            as_node_namespace_t* ns = (as_node_namespace_t*)node;
            char* saved = comp->ns_prefix;
            char* prefix = join_ns(saved, ns->name);
            size_t i;
            comp->ns_prefix = prefix;
            /* Nested namespaces first so inner names are visible to later defs. */
            for (i = 0; i < ns->member_count; i++) {
                if (ns->members[i]->type == NODE_NAMESPACE) {
                    compile_statement(comp, ns->members[i]);
                }
            }
            for (i = 0; i < ns->member_count; i++) {
                as_node_t* member = ns->members[i];
                if (member->type != NODE_DEF) {
                    continue;
                }
                {
                    as_node_def_t* def = (as_node_def_t*)member;
                    if (def->is_mut) {
                        compile_error(comp, "var not allowed in namespace");
                    }
                    if (!def->dest || def->dest->type != NODE_IDENTIFIER) {
                        compile_error(comp, "destructuring not allowed in namespace");
                    }
                    if (def->expr) {
                        compile_expression(comp, def->expr);
                    } else {
                        emit_push_null(comp);
                    }
                    if (def->dest && def->dest->type == NODE_IDENTIFIER) {
                        as_node_identifier_t* id = (as_node_identifier_t*)def->dest;
                        char* full = join_ns(prefix, id->name);
                        uint16_t idx = as_add_symbol(comp, full);
                        if (comp->symbol_mutable) {
                            comp->symbol_mutable[idx] = false;
                        }
                        as_emit_op(comp, OP_STORE_GLOBAL);
                        as_emit_u16(comp, idx);
                        free(full);
                    } else {
                        as_emit_op(comp, OP_POP);
                    }
                }
            }
            comp->ns_prefix = saved;
            free(prefix);
            break;
        }
            
        /* Block */
        case NODE_BLOCK: {
            as_node_block_t* block = (as_node_block_t*)node;
            as_begin_breakable(comp, block->label, false);
            as_begin_scope(comp);
            for (size_t i = 0; i < block->statement_count; i++) {
                compile_statement(comp, block->statements[i]);
            }
            as_end_scope(comp);
            as_end_loop(comp);
            break;
        }
            
        /* If statement */
        case NODE_IF: {
            as_node_if_t* if_node = (as_node_if_t*)node;
            size_t i;
            size_t end_jumps[32];
            size_t end_count = 0;
            as_begin_breakable(comp, if_node->label, false);
            compile_expression(comp, if_node->cond);
            {
                size_t then_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                as_emit_op(comp, OP_POP);
                compile_statement(comp, if_node->then_node);
                if (end_count < 32) {
                    end_jumps[end_count++] = as_emit_jump(comp, OP_JUMP);
                }
                as_patch_jump(comp, then_jump);
                as_emit_op(comp, OP_POP);
            }
            for (i = 0; i < if_node->elseif_count; i++) {
                compile_expression(comp, if_node->elseif_conds[i]);
                {
                    size_t elif_jump = as_emit_jump(comp, OP_JUMP_IF_FALSE);
                    as_emit_op(comp, OP_POP);
                    compile_statement(comp, if_node->elseif_then[i]);
                    if (end_count < 32) {
                        end_jumps[end_count++] = as_emit_jump(comp, OP_JUMP);
                    }
                    as_patch_jump(comp, elif_jump);
                    as_emit_op(comp, OP_POP);
                }
            }
            if (if_node->else_node) {
                compile_statement(comp, if_node->else_node);
            }
            for (i = 0; i < end_count; i++) {
                as_patch_jump(comp, end_jumps[i]);
            }
            as_end_loop(comp);
            break;
        }
            
        /* Break and Continue */
        case NODE_BREAK: {
            as_node_break_t* br = (as_node_break_t*)node;
            if (br->expr) {
                compile_expression(comp, br->expr);
            } else {
                emit_push_null(comp);
            }
            as_emit_break_label(comp, br->label);
            break;
        }
            
        case NODE_CONTINUE: {
            as_node_continue_t* cont = (as_node_continue_t*)node;
            as_emit_continue_label(comp, cont->label);
            break;
        }
            
        default:
            /* Try as expression */
            compile_expression(comp, node);
            as_emit_op(comp, OP_POP);
            break;
    }
}

/* Compile a node */
void as_compile_node(as_compiler_t* comp, as_node_t* node) {
    compile_statement(comp, node);
}

/* Main compilation function */
as_bytecode_t* as_compile(as_node_t** nodes, size_t node_count, 
                           as_compiler_config_t* config) {
    /* Create compiler */
    as_compiler_t* comp = as_compiler_new(config);
    if (!comp) return NULL;
    
    /* Hoist namespaces so `Foo:bar` works before `:: Foo { ... }`. */
    for (size_t i = 0; i < node_count; i++) {
        if (nodes[i] && nodes[i]->type == NODE_NAMESPACE) {
            as_compile_node(comp, nodes[i]);
        }
    }
    for (size_t i = 0; i < node_count; i++) {
        if (nodes[i] && nodes[i]->type == NODE_NAMESPACE) {
            continue;
        }
        as_compile_node(comp, nodes[i]);
    }

    if (comp->error_message) {
        as_compiler_free(comp);
        return NULL;
    }
    
    /* Emit RETURN at end (like HALT) */
    as_emit_op(comp, OP_RETURN);
    
    /* Create bytecode structure */
    as_bytecode_t* bc = malloc(sizeof(as_bytecode_t));
    if (!bc) {
        as_compiler_free(comp);
        return NULL;
    }
    
    /* Initialize header */
    bc->header.magic = AS_BYTECODE_MAGIC;
    bc->header.version_major = AS_BYTECODE_VERSION_MAJOR;
    bc->header.version_minor = AS_BYTECODE_VERSION_MINOR;
    bc->header.flags = 0;
    if (comp->config.debug_info) {
        bc->header.flags |= AS_BC_FLAG_DEBUG_INFO;
    }
    if (comp->config.opt_level > OPT_NONE) {
        bc->header.flags |= AS_BC_FLAG_OPTIMIZED;
    }
    bc->header.code_size = comp->code_size;
    bc->header.const_count = comp->const_count;
    bc->header.native_count = comp->native_count;
    bc->header.debug_count = comp->debug_count;
    bc->header.checksum = 0;  /* Will be calculated by bytecode_write_file */
    memset(bc->header.reserved, 0, sizeof(bc->header.reserved));
    
    /* Transfer code */
    bc->code = comp->code;
    bc->code_size = comp->code_size;
    comp->code = NULL; /* Ownership transferred */
    comp->code_size = 0;
    
    /* Transfer constants */
    bc->constants = comp->constants;
    bc->const_count = comp->const_count;
    comp->constants = NULL; /* Ownership transferred */
    comp->const_count = 0;
    
    /* Transfer symbols */
    bc->symbols = comp->symbols;
    bc->symbol_count = comp->symbol_count;
    comp->symbols = NULL; /* Ownership transferred */
    comp->symbol_count = 0;
    
    /* Transfer natives */
    bc->natives = comp->natives;
    bc->native_count = comp->native_count;
    comp->natives = NULL; /* Ownership transferred */
    comp->native_count = 0;
    
    /* Transfer debug info */
    bc->debug_info = comp->debug_info;
    bc->debug_count = comp->debug_count;
    comp->debug_info = NULL; /* Ownership transferred */
    comp->debug_count = 0;
    
    /* Apply optimizations */
    if (config && config->opt_level > OPT_NONE) {
        as_optimize_bytecode(bc, config->opt_level);
    }
    
    /* Free compiler */
    as_compiler_free(comp);
    
    return bc;
}

/* Compile with default configuration */
as_bytecode_t* as_compile_default(as_node_t** nodes, size_t node_count) {
    as_compiler_config_t config = {
        .opt_level = OPT_BASIC,
        .debug_info = false,
        .inline_functions = false,
        .const_propagation = true,
        .target_arch = NULL
    };
    return as_compile(nodes, node_count, &config);
}

/* Loop management */
void as_begin_breakable(as_compiler_t* comp, const char* label, bool is_loop) {
    if (comp->loop_depth >= 32) {
        fprintf(stderr, "Compiler Error: Loop nesting too deep\n");
        return;
    }
    
    as_loop_context_t* loop = &comp->loop_stack[comp->loop_depth++];
    loop->start = comp->code_size;
    loop->label = label ? strdup(label) : NULL;
    loop->is_loop = is_loop;
    loop->breaks = NULL;
    loop->break_count = 0;
    loop->break_capacity = 0;
    loop->continues = NULL;
    loop->continue_count = 0;
    loop->continue_capacity = 0;
}

void as_begin_loop(as_compiler_t* comp) {
    as_begin_breakable(comp, NULL, true);
}

void as_end_loop(as_compiler_t* comp) {
    if (comp->loop_depth == 0) {
        fprintf(stderr, "Compiler Error: Not in a loop\n");
        return;
    }
    
    as_loop_context_t* loop = &comp->loop_stack[--comp->loop_depth];
    
    /* Patch all break statements */
    for (size_t i = 0; i < loop->break_count; i++) {
        size_t offset = loop->breaks[i];
        int16_t jump = (int16_t)(comp->code_size - offset - 2);
        comp->code[offset] = (jump >> 8) & 0xFF;
        comp->code[offset + 1] = jump & 0xFF;
    }
    
    /* Patch all continue statements */
    for (size_t i = 0; i < loop->continue_count; i++) {
        size_t offset = loop->continues[i];
        int16_t jump = (int16_t)(loop->start - offset - 2);
        comp->code[offset] = (jump >> 8) & 0xFF;
        comp->code[offset + 1] = jump & 0xFF;
    }
    
    if (loop->label) free(loop->label);
    if (loop->breaks) free(loop->breaks);
    if (loop->continues) free(loop->continues);
}

static as_loop_context_t* find_break_target(as_compiler_t* comp, const char* label)
{
    int i;
    for (i = comp->loop_depth - 1; i >= 0; i--) {
        as_loop_context_t* loop = &comp->loop_stack[i];
        if (label) {
            if (loop->label && strcmp(loop->label, label) == 0) {
                return loop;
            }
        } else if (loop->is_loop) {
            return loop;
        }
    }
    return NULL;
}

void as_emit_break(as_compiler_t* comp) {
    as_emit_break_label(comp, NULL);
}

void as_emit_break_label(as_compiler_t* comp, const char* label) {
    as_loop_context_t* loop = find_break_target(comp, label);
    if (!loop) {
        fprintf(stderr, "Compiler Error: Break outside of loop\n");
        return;
    }
    if (loop->break_count >= loop->break_capacity) {
        loop->break_capacity = loop->break_capacity == 0 ? 4 : loop->break_capacity * 2;
        loop->breaks = realloc(loop->breaks, sizeof(size_t) * loop->break_capacity);
    }
    {
        size_t jump_offset = as_emit_jump(comp, OP_JUMP);
        loop->breaks[loop->break_count++] = jump_offset;
    }
}

void as_emit_continue(as_compiler_t* comp) {
    as_emit_continue_label(comp, NULL);
}

void as_emit_continue_label(as_compiler_t* comp, const char* label) {
    as_loop_context_t* loop = NULL;
    int i;
    for (i = comp->loop_depth - 1; i >= 0; i--) {
        as_loop_context_t* cand = &comp->loop_stack[i];
        if (!cand->is_loop) {
            continue;
        }
        if (label) {
            if (cand->label && strcmp(cand->label, label) == 0) {
                loop = cand;
                break;
            }
        } else {
            loop = cand;
            break;
        }
    }
    if (!loop) {
        fprintf(stderr, "Compiler Error: Continue outside of loop\n");
        return;
    }
    if (loop->continue_count >= loop->continue_capacity) {
        loop->continue_capacity = loop->continue_capacity == 0 ? 4 : loop->continue_capacity * 2;
        loop->continues = realloc(loop->continues, sizeof(size_t) * loop->continue_capacity);
    }
    {
        size_t jump_offset = as_emit_jump(comp, OP_JUMP);
        loop->continues[loop->continue_count++] = jump_offset;
    }
}

/* Note: Optimization functions are defined in optimizer.c */
