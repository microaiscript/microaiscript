/*
 * AiScript - AST Node definitions
 */

#ifndef AISCRIPT_NODE_H
#define AISCRIPT_NODE_H

#include "types.h"

/* Node types (matching TypeScript node types) */
typedef enum {
    /* Statements */
    NODE_DEF,
    NODE_RETURN,
    NODE_EACH,
    NODE_FOR,
    NODE_LOOP,
    NODE_WHILE,
    NODE_DO_WHILE,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_ASSIGN,
    NODE_ADD_ASSIGN,
    NODE_SUB_ASSIGN,
    
    /* Expressions */
    NODE_IF,
    NODE_FN,
    NODE_MATCH,
    NODE_BLOCK,
    NODE_EXISTS,
    NODE_TMPL,
    NODE_STR,
    NODE_NUM,
    NODE_BOOL,
    NODE_NULL,
    NODE_OBJ,
    NODE_ARR,
    NODE_PLUS,
    NODE_MINUS,
    NODE_NOT,
    NODE_POW,
    NODE_MUL,
    NODE_DIV,
    NODE_REM,
    NODE_ADD,
    NODE_SUB,
    NODE_LT,
    NODE_LTEQ,
    NODE_GT,
    NODE_GTEQ,
    NODE_EQ,
    NODE_NEQ,
    NODE_AND,
    NODE_OR,
    NODE_IDENTIFIER,
    NODE_CALL,
    NODE_INDEX,
    NODE_PROP,
    
    /* Other */
    NODE_META,
    NODE_ATTR,
    NODE_NAMESPACE,
    NODE_IMPORT
} as_node_type_t;

/* Base node structure - all nodes start with these fields */
typedef struct as_node {
    as_node_type_t type;
    as_loc_t loc;
} as_node_t;

/* Attribute node */
typedef struct {
    as_node_t base;
    char* name;           /* Attribute name */
    as_node_t* value;     /* Attribute value (expression) */
} as_node_attr_t;

/* Definition node */
typedef struct {
    as_node_t base;
    as_node_t* dest;      /* Destination (identifier or pattern) */
    as_node_t* expr;      /* Expression */
    bool is_mut;          /* Is mutable */
    char* type_annotation;  /* Optional type annotation (e.g., "num", "str") */
    as_node_attr_t** attrs;   /* Optional attributes (e.g., #[Event "msg"]) */
    size_t attr_count;
} as_node_def_t;

/* Return node */
typedef struct {
    as_node_t base;
    as_node_t* expr;
} as_node_return_t;

/* Each loop node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t* var;       /* Iterator variable (identifier node) */
    as_node_t* items;     /* Array to iterate over */
    as_node_t* body;      /* Loop body */
} as_node_each_t;

/* For loop node */
typedef struct {
    as_node_t base;
    char* label;
    char* var;            /* Iterator variable name */
    as_node_t* from;      /* Start value */
    as_node_t* to;        /* End value */
    as_node_t* times;     /* Number of iterations */
    as_node_t* body;      /* Loop body */
} as_node_for_t;

/* Loop node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t** statements;
    size_t statement_count;
} as_node_loop_t;

/* While loop node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t* cond;
    as_node_t* body;
} as_node_while_t;

/* Do-while loop node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t* body;
    as_node_t* cond;
} as_node_do_while_t;

/* Break node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t* expr;
} as_node_break_t;

/* Continue node */
typedef struct {
    as_node_t base;
    char* label;
} as_node_continue_t;

/* Assignment nodes */
typedef struct {
    as_node_t base;
    as_node_t* dest;
    as_node_t* expr;
} as_node_assign_t;

/* If node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t* cond;
    as_node_t* then_node;
    as_node_t** elseif_conds;
    as_node_t** elseif_then;
    size_t elseif_count;
    as_node_t* else_node;
} as_node_if_t;

/* Function node */
typedef struct {
    as_node_t base;
    as_fn_param_t* params;
    size_t param_count;
    as_node_t** statements;
    size_t statement_count;
    char* return_type;  /* Optional return type annotation */
} as_node_fn_t;

/* Block node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t** statements;
    size_t statement_count;
} as_node_block_t;

/* Match expression case */
typedef struct {
    as_node_t* q;  /* Question (condition expression) */
    as_node_t* a;  /* Answer (result statement/expression) */
} as_match_case_t;

/* Match expression node */
typedef struct {
    as_node_t base;
    char* label;
    as_node_t* about;        /* Expression to match against */
    as_match_case_t* cases;  /* Array of cases */
    size_t case_count;
    as_node_t* default_case; /* Default case (optional) */
} as_node_match_t;

/* Exists operator node */
typedef struct {
    as_node_t base;
    as_node_t* identifier;  /* Identifier to check */
} as_node_exists_t;

/* Template string node */
typedef struct {
    as_node_t base;
    as_node_t** values;    /* Array of expression nodes (strings and embedded expressions) */
    size_t value_count;
} as_node_tmpl_t;

/* Metadata node */
typedef struct {
    as_node_t base;
    char* name;       /* Metadata key (e.g., "name", "version") */
    as_node_t* value; /* Metadata value (must be a literal) */
} as_node_meta_t;

/* String literal node */
typedef struct {
    as_node_t base;
    char* value;
} as_node_str_t;

/* Number literal node */
typedef struct {
    as_node_t base;
    double value;
} as_node_num_t;

/* Boolean literal node */
typedef struct {
    as_node_t base;
    bool value;
} as_node_bool_t;

/* Array literal node */
typedef struct {
    as_node_t base;
    as_node_t** elements;
    size_t element_count;
} as_node_arr_t;

/* Object entry */
typedef struct {
    char* key;
    as_node_t* value;
} as_obj_kv_t;

/* Object literal node */
typedef struct {
    as_node_t base;
    as_obj_kv_t* entries;
    size_t entry_count;
} as_node_obj_t;

/* Binary operation node */
typedef struct {
    as_node_t base;
    as_node_t* left;
    as_node_t* right;
} as_node_binary_t;

/* Unary operation node */
typedef struct {
    as_node_t base;
    as_node_t* expr;
} as_node_unary_t;

/* Identifier node */
typedef struct {
    as_node_t base;
    char* name;
    char* namespace;  /* Optional namespace (e.g., "Core" for "Core:type") */
} as_node_identifier_t;

/* Function call node */
typedef struct {
    as_node_t base;
    as_node_t* target;
    as_node_t** args;
    size_t arg_count;
} as_node_call_t;

/* Index access node */
typedef struct {
    as_node_t base;
    as_node_t* target;
    as_node_t* index;
} as_node_index_t;

/* Property access node */
typedef struct {
    as_node_t base;
    as_node_t* target;
    char* name;
} as_node_prop_t;

/* Namespace definition node */
typedef struct {
    as_node_t base;
    char* name;              /* Namespace name */
    as_node_t** members;     /* Member definitions (variables/functions/nested namespaces) */
    size_t member_count;
} as_node_namespace_t;

/* Import node */
typedef struct {
    as_node_t base;
    char* path;           /* Module path (e.g., "utils.ais", "libmodule.so") */
    char* alias;          /* Optional alias (e.g., "as Utils") - NULL if not used */
    char** imports;       /* Selective imports (e.g., ["func1", "func2"]) - NULL for full import */
    size_t import_count;  /* Number of selective imports - 0 for full import */
} as_node_import_t;

/* Node operations */
as_node_t* as_node_new(as_node_type_t type, as_loc_t loc);
void as_node_free(as_node_t* node);
const char* as_node_type_to_string(as_node_type_t type);

#endif /* AISCRIPT_NODE_H */

