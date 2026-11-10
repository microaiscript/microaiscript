/* Bytecode optimizer
 * Implements various optimization passes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "bytecode.h"
#include "memory.h"

/* Apply optimization passes based on level */
void as_optimize_bytecode(as_bytecode_t* bc, as_opt_level_t level) {
    if (level == OPT_NONE) return;
    
    /* Basic optimizations */
    if (level >= OPT_BASIC) {
        as_optimize_dead_code(bc);
    }
    
    /* Full optimizations */
    if (level >= OPT_FULL) {
        as_optimize_peephole(bc);
        as_optimize_jumps(bc);
    }
}

/* Remove unreachable code after RETURN, HALT */
void as_optimize_dead_code(as_bytecode_t* bc) {
    /* Simple dead code elimination:
     * Remove instructions after RETURN/HALT until next jump target
     * This is a placeholder - full implementation would:
     * 1. Build control flow graph
     * 2. Mark reachable blocks
     * 3. Remove unreachable blocks
     */
    (void)bc; /* Currently unused - to be implemented */
}

/* Peephole optimizations
 * Look for small patterns and optimize them
 */
void as_optimize_peephole(as_bytecode_t* bc) {
    /* Common patterns to optimize:
     * 1. LOAD_CONST x, LOAD_CONST y, ADD -> LOAD_CONST (x+y)
     * 2. LOAD_CONST 0, ADD -> (remove, no-op)
     * 3. LOAD_CONST 1, MUL -> (remove, no-op)
     * 4. PUSH x, POP -> (remove both)
     * 5. JUMP +0 -> (remove)
     * 6. NOT, NOT -> (remove both)
     * 
     * This is a placeholder - full implementation would scan the bytecode
     * and replace patterns in place.
     */
    
    if (!bc || !bc->code || bc->code_size == 0) return;
    
    /* Example: Remove JUMP +0 (jump to next instruction) */
    for (size_t i = 0; i < bc->code_size; ) {
        as_opcode_t op = (as_opcode_t)bc->code[i];
        
        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE) {
            /* Check if jump offset is 0 */
            if (i + 2 < bc->code_size) {
                int16_t offset = (int16_t)((bc->code[i+1] << 8) | bc->code[i+2]);
                if (offset == 0) {
                    /* Remove this instruction by shifting rest down */
                    memmove(&bc->code[i], &bc->code[i+3], bc->code_size - i - 3);
                    bc->code_size -= 3;
                    continue; /* Don't advance i */
                }
            }
        }
        
        /* Advance to next instruction */
        i++;
        
        /* Skip operands */
        switch (op) {
            case OP_PUSH_CONST:
            case OP_LOAD_VAR:
            case OP_STORE_VAR:
            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL:
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_JUMP_IF_TRUE:
            case OP_PROP_GET:
            case OP_PROP_SET:
                i += 2; /* 16-bit operand */
                break;
            case OP_LOAD_FAST:
            case OP_STORE_FAST:
            case OP_CALL:
                i += 1; /* 8-bit operand */
                break;
            case OP_CALL_NATIVE:
                i += 3; /* 16-bit native_index + 8-bit arg_count */
                break;
            default:
                /* Most instructions have no operands */
                break;
        }
    }
}

/* Optimize jump chains
 * JUMP A -> A: JUMP B -> B: ... becomes JUMP final_target
 */
void as_optimize_jumps(as_bytecode_t* bc) {
    /* Jump threading optimization:
     * If we have:
     *   JUMP L1
     *   ...
     *   L1: JUMP L2
     *   ...
     *   L2: (target)
     * 
     * Replace the first jump to jump directly to L2.
     * 
     * This is a placeholder - full implementation would:
     * 1. Build jump table
     * 2. Follow jump chains
     * 3. Update all jumps to point to final target
     */
    (void)bc; /* Currently unused - to be implemented */
}

/* Constant folding (compile-time)
 * This is called during compilation, not post-processing
 */
void as_optimize_constant_folding(as_compiler_t* comp) {
    /* This would be implemented in the compiler itself,
     * evaluating constant expressions at compile time:
     * 
     * Examples:
     *   2 + 3 -> 5
     *   "hello" + " " + "world" -> "hello world"
     *   true && false -> false
     * 
     * This requires evaluating expressions during AST traversal.
     */
    (void)comp; /* Currently unused - to be implemented */
}

/* Check if a node is a compile-time constant */
bool as_is_constant_node(as_node_t* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_NUM:
        case NODE_STR:
        case NODE_BOOL:
        case NODE_NULL:
            return true;
            
        /* For now, don't consider compound expressions as constants */
        /* TODO: Implement when we have proper binary/unary node structures */
        default:
            return false;
    }
}

/* Evaluate a constant node at compile time */
as_value_t* as_eval_constant_node(as_node_t* node) {
    /* This would evaluate constant expressions:
     * 
     * Examples:
     *   NODE_NUM(2) + NODE_NUM(3) -> value(5)
     *   NODE_STR("hello") + NODE_STR(" world") -> value("hello world")
     * 
     * This is a placeholder - full implementation would handle all
     * constant operations and return an as_value_t*.
     */
    (void)node;
    return NULL; /* To be implemented */
}

/* Additional optimization: Inline small functions
 * This would be called during compilation to inline function calls
 */
void as_optimize_inline_functions(as_compiler_t* comp) {
    /* Function inlining:
     * Replace CALL instructions to small functions with the function body.
     * 
     * Benefits:
     *   - Eliminates function call overhead
     *   - Enables further optimizations on inlined code
     * 
     * Criteria for inlining:
     *   - Function body is small (< 10 instructions)
     *   - Function is not recursive
     *   - Function is called frequently
     * 
     * This is a placeholder - full implementation would:
     * 1. Identify inlining candidates during first pass
     * 2. Replace CALL sites with inlined code during second pass
     * 3. Adjust jump targets and local variable offsets
     */
    (void)comp; /* Currently unused - to be implemented */
}

/* Additional optimization: Constant propagation
 * Track known constant values through the code
 */
void as_optimize_constant_propagation(as_compiler_t* comp) {
    /* Constant propagation:
     * If a variable is assigned a constant value and never modified,
     * replace all uses of that variable with the constant.
     * 
     * Example:
     *   let x = 5
     *   let y = x + 3  // Can be optimized to: let y = 8
     *   <: y           // Can be optimized to: <: 8
     * 
     * This is a placeholder - full implementation would:
     * 1. Track constant assignments to variables
     * 2. Replace variable loads with constant loads
     * 3. Eliminate dead stores to unused variables
     */
    (void)comp; /* Currently unused - to be implemented */
}

