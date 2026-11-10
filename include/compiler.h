/* Bytecode compiler for AiScript C implementation
 * Compiles AST to bytecode
 */

#ifndef AS_COMPILER_H
#define AS_COMPILER_H

#include <stdint.h>
#include <stdbool.h>
#include "node.h"
#include "bytecode.h"

/* Compiler optimization levels */
typedef enum {
    OPT_NONE = 0,      /* No optimization */
    OPT_BASIC = 1,     /* Basic optimizations (constant folding, dead code) */
    OPT_FULL = 2       /* Full optimizations (all + peephole, inlining) */
} as_opt_level_t;

/* Compiler configuration */
typedef struct {
    as_opt_level_t opt_level;
    bool debug_info;          /* Include debug information */
    bool inline_functions;    /* Inline small functions */
    bool const_propagation;   /* Propagate constants */
    const char* target_arch;  /* Target architecture (e.g., "esp32") */
} as_compiler_config_t;

/* Local variable information */
typedef struct {
    char* name;
    int depth;        /* Scope depth */
    bool is_mutable;
    bool is_cell;     /* Boxed for closure capture */
} as_local_t;

/* Loop / breakable context for break/continue */
typedef struct {
    size_t start;           /* Loop start offset */
    char* label;            /* Optional label (owned) */
    bool is_loop;           /* false for labeled if/match/block */
    size_t* breaks;         /* Break jump locations to patch */
    size_t break_count;
    size_t break_capacity;
    size_t* continues;      /* Continue jump locations to patch */
    size_t continue_count;
    size_t continue_capacity;
} as_loop_context_t;

/* Compiler state */
typedef struct as_compiler {
    /* Code generation */
    uint8_t* code;
    size_t code_size;
    size_t code_capacity;
    
    /* Constant pool */
    as_constant_t** constants;
    size_t const_count;
    size_t const_capacity;
    
    /* Symbol table (global variables) */
    char** symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    
    /* Local variables (current scope) */
    as_local_t* locals;
    size_t local_count;
    size_t local_capacity;
    
    /* Native function references */
    as_native_ref_t* natives;
    size_t native_count;
    size_t native_capacity;
    
    /* Debug information */
    as_debug_info_t* debug_info;
    size_t debug_count;
    size_t debug_capacity;
    
    /* Scope tracking */
    int scope_depth;
    
    /* Loop stack */
    as_loop_context_t loop_stack[32];
    int loop_depth;
    
    /* Closure support */
    struct as_compiler* parent;  /* Parent compiler for nested functions */
    char** captured_vars;        /* List of captured variable names */
    size_t captured_count;
    size_t captured_capacity;
    char** cell_names;           /* Locals that nested fns capture */
    size_t cell_count;
    size_t cell_capacity;
    char* ns_prefix;             /* Current namespace prefix (e.g. "Foo:Bar") */
    bool* symbol_mutable;        /* Parallel to symbols[] — false for `let` */
    unsigned temp_id;            /* Unique hidden local names */
    
    /* Configuration */
    as_compiler_config_t config;
    
    /* Error handling */
    char* error_message;
} as_compiler_t;

/* Compiler creation and destruction */
as_compiler_t* as_compiler_new(as_compiler_config_t* config);
void as_compiler_free(as_compiler_t* comp);

/* Main compilation interface */
as_bytecode_t* as_compile(as_node_t** nodes, size_t node_count, 
                           as_compiler_config_t* config);

/* Compile with default configuration */
as_bytecode_t* as_compile_default(as_node_t** nodes, size_t node_count);

/* Low-level compilation functions */
void as_compile_node(as_compiler_t* comp, as_node_t* node);
void as_compile_expression(as_compiler_t* comp, as_node_t* node);
void as_compile_statement(as_compiler_t* comp, as_node_t* node);

/* Code emission */
void as_emit_op(as_compiler_t* comp, as_opcode_t op);
void as_emit_u8(as_compiler_t* comp, uint8_t value);
void as_emit_u16(as_compiler_t* comp, uint16_t value);
void as_emit_i16(as_compiler_t* comp, int16_t value);
size_t as_emit_jump(as_compiler_t* comp, as_opcode_t op);
void as_patch_jump(as_compiler_t* comp, size_t offset);

/* Constant pool management */
uint16_t as_add_const(as_compiler_t* comp, as_constant_t* constant);
uint16_t as_add_const_num(as_compiler_t* comp, double value);
uint16_t as_add_const_str(as_compiler_t* comp, const char* value);
uint16_t as_add_const_bool(as_compiler_t* comp, bool value);

/* Symbol table management */
uint16_t as_add_symbol(as_compiler_t* comp, const char* name);
int as_resolve_local(as_compiler_t* comp, const char* name);
int as_resolve_symbol(as_compiler_t* comp, const char* name);

/* Scope management */
void as_begin_scope(as_compiler_t* comp);
void as_end_scope(as_compiler_t* comp);

/* Loop management */
void as_begin_loop(as_compiler_t* comp);
void as_begin_breakable(as_compiler_t* comp, const char* label, bool is_loop);
void as_end_loop(as_compiler_t* comp);
void as_emit_break(as_compiler_t* comp);
void as_emit_continue(as_compiler_t* comp);
void as_emit_break_label(as_compiler_t* comp, const char* label);
void as_emit_continue_label(as_compiler_t* comp, const char* label);

/* Debug info */
void as_emit_debug_info(as_compiler_t* comp, int line, int column);

/* Optimization passes */
void as_optimize_bytecode(as_bytecode_t* bc, as_opt_level_t level);
void as_optimize_constant_folding(as_compiler_t* comp);
void as_optimize_dead_code(as_bytecode_t* bc);
void as_optimize_peephole(as_bytecode_t* bc);
void as_optimize_jumps(as_bytecode_t* bc);

/* Utility functions */
bool as_is_constant_node(as_node_t* node);
as_value_t* as_eval_constant_node(as_node_t* node);

#endif /* AS_COMPILER_H */

