/*
 * AiScript - AST Node implementation
 */

#include "node.h"
#include "memory.h"
#include <string.h>

as_node_t* as_node_new(as_node_type_t type, as_loc_t loc) {
    as_node_t* node = NULL;
    
    /* Allocate appropriate size based on node type */
    switch (type) {
        case NODE_DEF:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_def_t));
            break;
        case NODE_RETURN:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_return_t));
            break;
        case NODE_EACH:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_each_t));
            break;
        case NODE_FOR:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_for_t));
            break;
        case NODE_LOOP:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_loop_t));
            break;
        case NODE_WHILE:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_while_t));
            break;
        case NODE_DO_WHILE:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_do_while_t));
            break;
        case NODE_BREAK:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_break_t));
            break;
        case NODE_CONTINUE:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_continue_t));
            break;
        case NODE_ASSIGN:
        case NODE_ADD_ASSIGN:
        case NODE_SUB_ASSIGN:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_assign_t));
            break;
        case NODE_IF:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_if_t));
            break;
        case NODE_FN:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_fn_t));
            break;
        case NODE_MATCH:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_match_t));
            break;
        case NODE_BLOCK:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_block_t));
            break;
        case NODE_EXISTS:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_exists_t));
            break;
        case NODE_TMPL:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_tmpl_t));
            break;
        case NODE_META:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_meta_t));
            break;
        case NODE_ATTR:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_attr_t));
            break;
        case NODE_NAMESPACE:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_namespace_t));
            break;
        case NODE_IMPORT:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_import_t));
            break;
        case NODE_STR:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_str_t));
            break;
        case NODE_NUM:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_num_t));
            break;
        case NODE_BOOL:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_bool_t));
            break;
        case NODE_ARR:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_arr_t));
            break;
        case NODE_OBJ:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_obj_t));
            break;
        case NODE_IDENTIFIER:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_identifier_t));
            break;
        case NODE_CALL:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_call_t));
            break;
        case NODE_INDEX:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_index_t));
            break;
        case NODE_PROP:
            node = (as_node_t*)as_calloc(1, sizeof(as_node_prop_t));
            break;
        default:
            /* For unary/binary operators and other simple nodes */
            if (type >= NODE_PLUS && type <= NODE_OR) {
                if (type == NODE_PLUS || type == NODE_MINUS || type == NODE_NOT) {
                    node = (as_node_t*)as_calloc(1, sizeof(as_node_unary_t));
                } else {
                    node = (as_node_t*)as_calloc(1, sizeof(as_node_binary_t));
                }
            } else {
                node = as_calloc(1, sizeof(as_node_t));
            }
            break;
    }
    
    node->type = type;
    node->loc = loc;
    return node;
}

void as_node_free(as_node_t* node) {
    if (!node) return;
    
    /* Free node-specific data */
    switch (node->type) {
        case NODE_DEF: {
            as_node_def_t* def = (as_node_def_t*)node;
            as_node_free(def->dest);
            as_node_free(def->expr);
            if (def->type_annotation) as_free(def->type_annotation);
            if (def->attrs) {
                for (size_t i = 0; i < def->attr_count; i++) {
                    as_node_free((as_node_t*)def->attrs[i]);
                }
                as_free(def->attrs);
            }
            break;
        }
        case NODE_ATTR: {
            as_node_attr_t* attr = (as_node_attr_t*)node;
            if (attr->name) as_free(attr->name);
            as_node_free(attr->value);
            break;
        }
        case NODE_RETURN: {
            as_node_return_t* ret = (as_node_return_t*)node;
            as_node_free(ret->expr);
            break;
        }
        case NODE_EACH: {
            as_node_each_t* each = (as_node_each_t*)node;
            if (each->label) as_free(each->label);
            as_node_free(each->var);
            as_node_free(each->items);
            as_node_free(each->body);
            break;
        }
        case NODE_BLOCK: {
            as_node_block_t* block = (as_node_block_t*)node;
            if (block->label) as_free(block->label);
            for (size_t i = 0; i < block->statement_count; i++) {
                as_node_free(block->statements[i]);
            }
            if (block->statements) as_free(block->statements);
            break;
        }
        case NODE_FOR: {
            as_node_for_t* for_node = (as_node_for_t*)node;
            if (for_node->label) as_free(for_node->label);
            if (for_node->var) as_free(for_node->var);
            as_node_free(for_node->from);
            as_node_free(for_node->to);
            as_node_free(for_node->times);
            as_node_free(for_node->body);
            break;
        }
        case NODE_EXISTS: {
            as_node_exists_t* exists = (as_node_exists_t*)node;
            as_node_free(exists->identifier);
            break;
        }
        case NODE_TMPL: {
            as_node_tmpl_t* tmpl = (as_node_tmpl_t*)node;
            for (size_t i = 0; i < tmpl->value_count; i++) {
                as_node_free(tmpl->values[i]);
            }
            if (tmpl->values) as_free(tmpl->values);
            break;
        }
        case NODE_META: {
            as_node_meta_t* meta = (as_node_meta_t*)node;
            if (meta->name) as_free(meta->name);
            as_node_free(meta->value);
            break;
        }
        case NODE_NAMESPACE: {
            as_node_namespace_t* ns = (as_node_namespace_t*)node;
            if (ns->name) as_free(ns->name);
            if (ns->members) {
                for (size_t i = 0; i < ns->member_count; i++) {
                    as_node_free(ns->members[i]);
                }
                as_free(ns->members);
            }
            break;
        }
        case NODE_IMPORT: {
            as_node_import_t* import = (as_node_import_t*)node;
            if (import->path) as_free(import->path);
            if (import->alias) as_free(import->alias);
            if (import->imports) {
                for (size_t i = 0; i < import->import_count; i++) {
                    if (import->imports[i]) as_free(import->imports[i]);
                }
                as_free(import->imports);
            }
            break;
        }
        case NODE_STR: {
            as_node_str_t* str = (as_node_str_t*)node;
            if (str->value) as_free(str->value);
            break;
        }
        case NODE_IDENTIFIER: {
            as_node_identifier_t* id = (as_node_identifier_t*)node;
            if (id->name) as_free(id->name);
            if (id->namespace) as_free(id->namespace);
            break;
        }
        case NODE_PROP: {
            as_node_prop_t* prop = (as_node_prop_t*)node;
            as_node_free(prop->target);
            if (prop->name) as_free(prop->name);
            break;
        }
        /* Add more cases as needed */
        default:
            break;
    }
    
    as_free(node);
}

const char* as_node_type_to_string(as_node_type_t type) {
    switch (type) {
        case NODE_DEF: return "def";
        case NODE_RETURN: return "return";
        case NODE_FOR: return "for";
        case NODE_LOOP: return "loop";
        case NODE_WHILE: return "while";
        case NODE_DO_WHILE: return "do-while";
        case NODE_BREAK: return "break";
        case NODE_CONTINUE: return "continue";
        case NODE_ASSIGN: return "assign";
        case NODE_IF: return "if";
        case NODE_FN: return "fn";
        case NODE_MATCH: return "match";
        case NODE_BLOCK: return "block";
        case NODE_EXISTS: return "exists";
        case NODE_META: return "meta";
        case NODE_ATTR: return "attr";
        case NODE_NAMESPACE: return "namespace";
        case NODE_IMPORT: return "import";
        case NODE_TMPL: return "tmpl";
        case NODE_STR: return "str";
        case NODE_NUM: return "num";
        case NODE_BOOL: return "bool";
        case NODE_NULL: return "null";
        case NODE_ARR: return "arr";
        case NODE_OBJ: return "obj";
        case NODE_IDENTIFIER: return "identifier";
        case NODE_CALL: return "call";
        case NODE_INDEX: return "index";
        case NODE_PROP: return "prop";
        case NODE_ADD: return "add";
        case NODE_SUB: return "sub";
        case NODE_MUL: return "mul";
        case NODE_DIV: return "div";
        case NODE_EQ: return "eq";
        case NODE_NEQ: return "neq";
        case NODE_LT: return "lt";
        case NODE_GT: return "gt";
        default: return "unknown";
    }
}

