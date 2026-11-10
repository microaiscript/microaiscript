/* Bytecode implementation for AiScript C
 * Handles bytecode structures and operations
 */

#include "bytecode.h"
#include "memory.h"
#include <string.h>
#include <stdio.h>

/* Create new bytecode structure */
as_bytecode_t* as_bytecode_new(void) {
    as_bytecode_t* bc = as_calloc(1, sizeof(as_bytecode_t));
    if (!bc) return NULL;
    
    /* Initialize header */
    bc->header.magic = AS_BYTECODE_MAGIC;
    bc->header.version_major = AS_BYTECODE_VERSION_MAJOR;
    bc->header.version_minor = AS_BYTECODE_VERSION_MINOR;
    bc->header.flags = 0;
    
    return bc;
}

/* Free constant */
static void free_constant(as_constant_t* constant) {
    if (!constant) return;
    
    switch (constant->type) {
        case CONST_STR:
            if (constant->value.str.data) {
                as_free(constant->value.str.data);
            }
            break;
            
        case CONST_FUNC_CODE:
            if (constant->value.func.code) {
                as_free(constant->value.func.code);
            }
            break;
            
        default:
            break;
    }
    
    as_free(constant);
}

/* Free bytecode structure */
void as_bytecode_free(as_bytecode_t* bc) {
    if (!bc) return;
    
    /* Free code */
    if (bc->code) {
        as_free(bc->code);
    }
    
    /* Free constants */
    if (bc->constants) {
        for (size_t i = 0; i < bc->const_count; i++) {
            free_constant(bc->constants[i]);
        }
        as_free(bc->constants);
    }
    
    /* Free native references */
    if (bc->natives) {
        for (size_t i = 0; i < bc->native_count; i++) {
            if (bc->natives[i].name) as_free(bc->natives[i].name);
            if (bc->natives[i].module) as_free(bc->natives[i].module);
        }
        as_free(bc->natives);
    }
    
    /* Free debug info */
    if (bc->debug_info) {
        as_free(bc->debug_info);
    }
    
    /* Free symbol table */
    if (bc->symbols) {
        for (size_t i = 0; i < bc->symbol_count; i++) {
            if (bc->symbols[i]) as_free(bc->symbols[i]);
        }
        as_free(bc->symbols);
    }
    
    as_free(bc);
}

/* Add constant to pool (returns index) */
uint16_t as_bytecode_add_const_num(as_bytecode_t* bc, double value) {
    as_constant_t* c = as_calloc(1, sizeof(as_constant_t));
    c->type = CONST_NUM;
    c->value.num_val = value;
    
    /* Add to pool */
    bc->constants = as_realloc(bc->constants, 
                               (bc->const_count + 1) * sizeof(as_constant_t*));
    bc->constants[bc->const_count] = c;
    
    return bc->const_count++;
}

uint16_t as_bytecode_add_const_str(as_bytecode_t* bc, const char* value) {
    as_constant_t* c = as_calloc(1, sizeof(as_constant_t));
    c->type = CONST_STR;
    c->value.str.length = strlen(value);
    c->value.str.data = as_strdup(value);
    
    /* Add to pool */
    bc->constants = as_realloc(bc->constants,
                               (bc->const_count + 1) * sizeof(as_constant_t*));
    bc->constants[bc->const_count] = c;
    
    return bc->const_count++;
}

uint16_t as_bytecode_add_const_bool(as_bytecode_t* bc, bool value) {
    as_constant_t* c = as_calloc(1, sizeof(as_constant_t));
    c->type = CONST_BOOL;
    c->value.bool_val = value;
    
    /* Add to pool */
    bc->constants = as_realloc(bc->constants,
                               (bc->const_count + 1) * sizeof(as_constant_t*));
    bc->constants[bc->const_count] = c;
    
    return bc->const_count++;
}

uint16_t as_bytecode_add_const_null(as_bytecode_t* bc) {
    as_constant_t* c = as_calloc(1, sizeof(as_constant_t));
    c->type = CONST_NULL;
    
    /* Add to pool */
    bc->constants = as_realloc(bc->constants,
                               (bc->const_count + 1) * sizeof(as_constant_t*));
    bc->constants[bc->const_count] = c;
    
    return bc->const_count++;
}

/* Validate bytecode structure */
bool as_bytecode_validate(as_bytecode_t* bc) {
    if (!bc) return false;
    
    /* Check magic number */
    if (bc->header.magic != AS_BYTECODE_MAGIC) {
        fprintf(stderr, "Invalid bytecode magic: 0x%08X (expected 0x%08X)\n",
                bc->header.magic, AS_BYTECODE_MAGIC);
        return false;
    }
    
    /* Check version */
    if (bc->header.version_major != AS_BYTECODE_VERSION_MAJOR) {
        fprintf(stderr, "Incompatible bytecode version: %u.%u (expected %u.%u)\n",
                bc->header.version_major, bc->header.version_minor,
                AS_BYTECODE_VERSION_MAJOR, AS_BYTECODE_VERSION_MINOR);
        return false;
    }
    
    /* Check code size */
    if (bc->header.code_size != bc->code_size) {
        fprintf(stderr, "Code size mismatch: %u != %zu\n",
                bc->header.code_size, bc->code_size);
        return false;
    }
    
    /* Check constant count */
    if (bc->header.const_count != bc->const_count) {
        fprintf(stderr, "Constant count mismatch: %u != %zu\n",
                bc->header.const_count, bc->const_count);
        return false;
    }
    
    /* Validate constants */
    for (size_t i = 0; i < bc->const_count; i++) {
        as_constant_t* c = bc->constants[i];
        if (!c) {
            fprintf(stderr, "Null constant at index %zu\n", i);
            return false;
        }
        
        switch (c->type) {
            case CONST_STR:
                if (!c->value.str.data) {
                    fprintf(stderr, "String constant %zu has null data\n", i);
                    return false;
                }
                break;
                
            case CONST_FUNC_CODE:
                if (!c->value.func.code || c->value.func.code_size == 0) {
                    fprintf(stderr, "Function constant %zu has invalid code\n", i);
                    return false;
                }
                break;
                
            default:
                break;
        }
    }
    
    return true;
}

/* Get opcode name */
const char* as_opcode_name(as_opcode_t op) {
    switch (op) {
        /* Stack operations */
        case OP_PUSH_NULL: return "PUSH_NULL";
        case OP_PUSH_TRUE: return "PUSH_TRUE";
        case OP_PUSH_FALSE: return "PUSH_FALSE";
        case OP_PUSH_CONST: return "PUSH_CONST";
        case OP_PUSH_NUM_I8: return "PUSH_NUM_I8";
        case OP_PUSH_NUM_I16: return "PUSH_NUM_I16";
        case OP_POP: return "POP";
        case OP_DUP: return "DUP";
        case OP_SWAP: return "SWAP";
        case OP_ROT3: return "ROT3";
        
        /* Variable operations */
        case OP_LOAD_VAR: return "LOAD_VAR";
        case OP_STORE_VAR: return "STORE_VAR";
        case OP_LOAD_GLOBAL: return "LOAD_GLOBAL";
        case OP_STORE_GLOBAL: return "STORE_GLOBAL";
        case OP_LOAD_FAST: return "LOAD_FAST";
        case OP_STORE_FAST: return "STORE_FAST";
        
        /* Arithmetic operations */
        case OP_ADD: return "ADD";
        case OP_SUB: return "SUB";
        case OP_MUL: return "MUL";
        case OP_DIV: return "DIV";
        case OP_REM: return "REM";
        case OP_POW: return "POW";
        case OP_NEG: return "NEG";
        case OP_POS: return "POS";
        
        /* Comparison operations */
        case OP_EQ: return "EQ";
        case OP_NEQ: return "NEQ";
        case OP_LT: return "LT";
        case OP_LTE: return "LTE";
        case OP_GT: return "GT";
        case OP_GTE: return "GTE";
        
        /* Logical operations */
        case OP_AND: return "AND";
        case OP_OR: return "OR";
        case OP_NOT: return "NOT";
        
        /* Control flow */
        case OP_JUMP: return "JUMP";
        case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
        case OP_JUMP_IF_FALSE_POP: return "JUMP_IF_FALSE_POP";
        case OP_JUMP_IF_TRUE_POP: return "JUMP_IF_TRUE_POP";
        case OP_CALL: return "CALL";
        case OP_CALL_NATIVE: return "CALL_NATIVE";
        case OP_RETURN: return "RETURN";
        case OP_RETURN_VALUE: return "RETURN_VALUE";
        
        /* Array/Object operations */
        case OP_BUILD_ARR: return "BUILD_ARR";
        case OP_BUILD_OBJ: return "BUILD_OBJ";
        case OP_INDEX_GET: return "INDEX_GET";
        case OP_INDEX_SET: return "INDEX_SET";
        case OP_PROP_GET: return "PROP_GET";
        case OP_PROP_SET: return "PROP_SET";
        case OP_ARR_PUSH: return "ARR_PUSH";
        
        /* Function operations */
        case OP_MAKE_FUNCTION: return "MAKE_FUNCTION";
        case OP_MAKE_CLOSURE: return "MAKE_CLOSURE";
        case OP_LOAD_CLOSURE: return "LOAD_CLOSURE";
        
        /* Loop operations */
        case OP_SETUP_LOOP: return "SETUP_LOOP";
        case OP_POP_LOOP: return "POP_LOOP";
        case OP_BREAK: return "BREAK";
        case OP_CONTINUE: return "CONTINUE";
        
        /* Special operations */
        case OP_NOP: return "NOP";
        case OP_PRINT: return "PRINT";
        case OP_TYPE_CHECK: return "TYPE_CHECK";
        
        case OP_EXTENDED: return "EXTENDED";
        
        default: return "UNKNOWN";
    }
}

/* Get argument count for opcode */
int as_opcode_arg_count(as_opcode_t op) {
    switch (op) {
        /* No arguments */
        case OP_PUSH_NULL:
        case OP_PUSH_TRUE:
        case OP_PUSH_FALSE:
        case OP_POP:
        case OP_DUP:
        case OP_SWAP:
        case OP_ROT3:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_REM:
        case OP_POW:
        case OP_NEG:
        case OP_POS:
        case OP_EQ:
        case OP_NEQ:
        case OP_LT:
        case OP_LTE:
        case OP_GT:
        case OP_GTE:
        case OP_AND:
        case OP_OR:
        case OP_NOT:
        case OP_RETURN:
        case OP_RETURN_VALUE:
        case OP_INDEX_GET:
        case OP_INDEX_SET:
        case OP_ARR_PUSH:
        case OP_POP_LOOP:
        case OP_BREAK:
        case OP_CONTINUE:
        case OP_NOP:
        case OP_PRINT:
            return 0;
        
        /* 1 byte argument */
        case OP_PUSH_NUM_I8:
        case OP_LOAD_FAST:
        case OP_STORE_FAST:
        case OP_CALL:
        case OP_LOAD_CLOSURE:
            return 1;
        
        /* 2 byte argument */
        case OP_PUSH_CONST:
        case OP_LOAD_VAR:
        case OP_STORE_VAR:
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL:
        case OP_PUSH_NUM_I16:
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_FALSE_POP:
        case OP_JUMP_IF_TRUE_POP:
        case OP_BUILD_ARR:
        case OP_BUILD_OBJ:
        case OP_PROP_GET:
        case OP_PROP_SET:
        case OP_SETUP_LOOP:
            return 2;
        
        /* 3 byte argument */
        case OP_CALL_NATIVE:
        case OP_MAKE_FUNCTION:
        case OP_MAKE_CLOSURE:
            return 3;
        
        default:
            return 0;
    }
}

/* Disassemble single instruction */
void as_bytecode_disassemble_instruction(uint8_t* code, size_t offset) {
    printf("%04zu: ", offset);
    
    as_opcode_t op = (as_opcode_t)code[offset];
    printf("%-20s", as_opcode_name(op));
    
    int arg_count = as_opcode_arg_count(op);
    
    if (arg_count == 1) {
        uint8_t arg = code[offset + 1];
        printf(" %u", arg);
    } else if (arg_count == 2) {
        uint16_t arg = (code[offset + 1] << 8) | code[offset + 2];
        printf(" %u", arg);
    } else if (arg_count == 3) {
        uint16_t arg1 = (code[offset + 1] << 8) | code[offset + 2];
        uint8_t arg2 = code[offset + 3];
        printf(" %u, %u", arg1, arg2);
    }
    
    printf("\n");
}

/* Disassemble entire bytecode */
void as_bytecode_disassemble(as_bytecode_t* bc) {
    if (!bc || !bc->code) {
        printf("No bytecode to disassemble\n");
        return;
    }
    
    printf("=== AiScript Bytecode Disassembly ===\n");
    printf("Version: %u.%u\n", bc->header.version_major, bc->header.version_minor);
    printf("Code size: %zu bytes\n", bc->code_size);
    printf("Constants: %zu\n", bc->const_count);
    printf("Natives: %zu\n", bc->native_count);
    printf("\n");
    
    /* Disassemble constants */
    if (bc->const_count > 0) {
        printf("=== Constants ===\n");
        for (size_t i = 0; i < bc->const_count; i++) {
            as_constant_t* c = bc->constants[i];
            printf("[%zu] ", i);
            
            switch (c->type) {
                case CONST_NULL:
                    printf("null\n");
                    break;
                case CONST_BOOL:
                    printf("%s\n", c->value.bool_val ? "true" : "false");
                    break;
                case CONST_NUM:
                    printf("%g\n", c->value.num_val);
                    break;
                case CONST_STR:
                    printf("\"%s\"\n", c->value.str.data);
                    break;
                case CONST_FUNC_CODE:
                    printf("<function code, %u bytes>\n", c->value.func.code_size);
                    break;
            }
        }
        printf("\n");
    }
    
    /* Disassemble native references */
    if (bc->native_count > 0) {
        printf("=== Native References ===\n");
        for (size_t i = 0; i < bc->native_count; i++) {
            printf("[%zu] %s from %s (hash: 0x%08X)\n",
                   i,
                   bc->natives[i].name,
                   bc->natives[i].module,
                   bc->natives[i].symbol_hash);
        }
        printf("\n");
    }
    
    /* Disassemble code */
    printf("=== Code ===\n");
    size_t offset = 0;
    while (offset < bc->code_size) {
        as_bytecode_disassemble_instruction(bc->code, offset);
        
        as_opcode_t op = (as_opcode_t)bc->code[offset];
        offset += 1 + as_opcode_arg_count(op);
    }
}

