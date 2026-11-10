/* Bytecode definitions for AiScript C implementation
 * Similar to MicroPython's .mpy format
 */

#ifndef AS_BYTECODE_H
#define AS_BYTECODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "types.h"

/* Bytecode file magic number "AICC" (AiScript Compiled Code) */
#define AS_BYTECODE_MAGIC 0x41494343

/* Current bytecode version */
#define AS_BYTECODE_VERSION_MAJOR 1
#define AS_BYTECODE_VERSION_MINOR 0

/* Compilation flags */
#define AS_BC_FLAG_OPTIMIZED    (1 << 0)  /* Code was optimized */
#define AS_BC_FLAG_DEBUG_INFO   (1 << 1)  /* Contains debug information */
#define AS_BC_FLAG_NATIVE_REFS  (1 << 2)  /* Contains native function references */
#define AS_BC_FLAG_METADATA     (1 << 3)  /* Contains metadata */

/* Bytecode opcodes */
typedef enum {
    /* Stack operations (0x00-0x0F) */
    OP_PUSH_NULL = 0x00,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_PUSH_CONST,        /* arg: const_index (u16) */
    OP_PUSH_NUM_I8,       /* arg: int8_t immediate */
    OP_PUSH_NUM_I16,      /* arg: int16_t immediate */
    OP_POP,
    OP_DUP,               /* Duplicate top of stack */
    OP_SWAP,              /* Swap top two items */
    OP_ROT3,              /* Rotate top 3 items */
    
    /* Variable operations (0x10-0x1F) */
    OP_LOAD_VAR = 0x10,   /* arg: name_index (u16) */
    OP_STORE_VAR,         /* arg: name_index (u16) */
    OP_LOAD_GLOBAL,       /* arg: name_index (u16) */
    OP_STORE_GLOBAL,      /* arg: name_index (u16) */
    OP_LOAD_FAST,         /* arg: local_index (u8) - optimized local load */
    OP_STORE_FAST,        /* arg: local_index (u8) - optimized local store */
    
    /* Arithmetic operations (0x20-0x2F) */
    OP_ADD = 0x20,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_REM,
    OP_POW,
    OP_NEG,               /* Unary negation */
    OP_POS,               /* Unary plus */
    
    /* Comparison operations (0x30-0x3F) */
    OP_EQ = 0x30,
    OP_NEQ,
    OP_LT,
    OP_LTE,
    OP_GT,
    OP_GTE,
    
    /* Logical operations (0x40-0x4F) */
    OP_AND = 0x40,
    OP_OR,
    OP_NOT,
    
    /* Control flow (0x50-0x5F) */
    OP_JUMP = 0x50,           /* arg: offset (i16) */
    OP_JUMP_IF_FALSE,         /* arg: offset (i16) */
    OP_JUMP_IF_TRUE,          /* arg: offset (i16) */
    OP_JUMP_IF_FALSE_POP,     /* Jump and pop if false */
    OP_JUMP_IF_TRUE_POP,      /* Jump and pop if true */
    OP_CALL,                  /* arg: arg_count (u8) */
    OP_CALL_NATIVE,           /* arg: native_index (u16), arg_count (u8) */
    OP_RETURN,
    OP_RETURN_VALUE,          /* Return with value on stack */
    
    /* Array/Object operations (0x60-0x6F) */
    OP_BUILD_ARR = 0x60,      /* arg: element_count (u16) */
    OP_BUILD_OBJ,             /* arg: pair_count (u16) */
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_PROP_GET,              /* arg: name_index (u16) */
    OP_PROP_SET,              /* arg: name_index (u16) */
    OP_ARR_PUSH,              /* Optimized array push */
    
    /* Function operations (0x70-0x7F) */
    OP_MAKE_FUNCTION = 0x70,  /* arg: code_index (u16), param_count (u8) */
    OP_MAKE_CLOSURE,          /* arg: code_index (u16), closure_count (u8) */
    OP_LOAD_CLOSURE,          /* arg: closure_index (u8) */
    
    /* Loop operations (0x80-0x8F) */
    OP_SETUP_LOOP = 0x80,     /* arg: end_offset (u16) */
    OP_POP_LOOP,
    OP_BREAK,
    OP_CONTINUE,
    
    /* Special operations (0x90-0x9F) */
    OP_NOP = 0x90,            /* No operation */
    OP_PRINT,                 /* Print top of stack */
    OP_TYPE_CHECK,            /* Check type, error if mismatch */
    
    /* Extended operations (0xF0-0xFF) */
    OP_EXTENDED = 0xF0,       /* Extended opcode follows */
    
    OP_MAX = 0xFF
} as_opcode_t;

/* Constant types */
typedef enum {
    CONST_NULL = 0,
    CONST_BOOL,
    CONST_NUM,
    CONST_STR,
    CONST_FUNC_CODE        /* Nested function bytecode */
} as_const_type_t;

/* Constant in pool */
typedef struct {
    as_const_type_t type;
    union {
        bool bool_val;
        double num_val;
        struct {
            uint32_t length;
            char* data;    /* UTF-8 string, null-terminated */
        } str;
        struct {
            uint32_t code_size;
            uint8_t* code; /* Bytecode for nested function */
        } func;
    } value;
} as_constant_t;

/* Native function reference */
typedef struct {
    char* name;            /* Function name */
    char* module;          /* Module name (e.g., "mylib.so") */
    uint32_t symbol_hash;  /* FNV-1a hash for fast lookup */
} as_native_ref_t;

/* Debug information (optional) */
typedef struct {
    uint32_t line;         /* Source line number */
    uint32_t column;       /* Source column number */
    uint32_t pc;           /* Bytecode program counter */
} as_debug_info_t;

/* Bytecode file header */
typedef struct {
    uint32_t magic;           /* Must be AS_BYTECODE_MAGIC */
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t flags;           /* Compilation flags */
    uint32_t code_size;       /* Size of bytecode section */
    uint32_t const_count;     /* Number of constants */
    uint32_t native_count;    /* Number of native references */
    uint32_t debug_count;     /* Number of debug entries */
    uint32_t checksum;        /* CRC32 of content */
    uint32_t reserved[3];     /* Reserved for future use */
} as_bytecode_header_t;

/* Complete bytecode structure */
typedef struct {
    as_bytecode_header_t header;
    
    /* Bytecode instructions */
    uint8_t* code;
    size_t code_size;
    
    /* Constant pool */
    as_constant_t** constants;
    size_t const_count;
    
    /* Native function references */
    as_native_ref_t* natives;
    size_t native_count;
    
    /* Debug information (optional) */
    as_debug_info_t* debug_info;
    size_t debug_count;
    
    /* Symbol table (variable names) */
    char** symbols;
    size_t symbol_count;
} as_bytecode_t;

/* Bytecode creation and destruction */
as_bytecode_t* as_bytecode_new(void);
void as_bytecode_free(as_bytecode_t* bc);

/* Bytecode file I/O */
as_bytecode_t* as_bytecode_read_file(const char* filename);
as_bytecode_t* as_bytecode_read_memory(const uint8_t* data, size_t size);
bool as_bytecode_write_file(as_bytecode_t* bc, const char* filename);
bool as_bytecode_write_memory(as_bytecode_t* bc, uint8_t** data_out, size_t* size_out);

/* Bytecode validation */
bool as_bytecode_validate(as_bytecode_t* bc);

/* Constant pool operations */
uint16_t as_bytecode_add_const_num(as_bytecode_t* bc, double value);
uint16_t as_bytecode_add_const_str(as_bytecode_t* bc, const char* value);
uint16_t as_bytecode_add_const_bool(as_bytecode_t* bc, bool value);
uint16_t as_bytecode_add_const_null(as_bytecode_t* bc);

/* Disassembler (for debugging) */
void as_bytecode_disassemble(as_bytecode_t* bc);
void as_bytecode_disassemble_instruction(uint8_t* code, size_t offset);

/* Opcode information */
const char* as_opcode_name(as_opcode_t op);
int as_opcode_arg_count(as_opcode_t op);

#endif /* AS_BYTECODE_H */

