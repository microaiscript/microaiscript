/*
 * AiScript - Scope implementation
 */

#include "scope.h"
#include "memory.h"
#include <string.h>
#include <stdio.h>

as_scope_t* as_scope_new(as_scope_t* parent, const char* name) {
    as_scope_t* scope = as_malloc(sizeof(as_scope_t));
    scope->parent = parent;
    if (parent) {
        as_scope_retain(parent);  /* Retain parent scope */
    }
    scope->variables = NULL;
    scope->var_count = 0;
    scope->var_capacity = 0;
    scope->name = name ? as_strdup(name) : as_strdup("<anonymous>");
    scope->ns_name = NULL;
    scope->ref_count = 1;  /* Start with ref count of 1 */
    return scope;
}

void as_scope_free(as_scope_t* scope) {
    if (!scope) return;
    
    /* Release parent scope */
    if (scope->parent) {
        as_scope_release(scope->parent);
    }
    
    for (size_t i = 0; i < scope->var_count; i++) {
        as_variable_t* var = scope->variables[i];
        if (var->name) as_free(var->name);
        as_value_release(var->value);
        as_free(var);
    }
    
    if (scope->variables) as_free(scope->variables);
    if (scope->name) as_free(scope->name);
    if (scope->ns_name) as_free(scope->ns_name);
    as_free(scope);
}

as_scope_t* as_scope_retain(as_scope_t* scope) {
    if (scope) {
        scope->ref_count++;
    }
    return scope;
}

void as_scope_release(as_scope_t* scope) {
    if (!scope) return;
    scope->ref_count--;
    if (scope->ref_count <= 0) {
        as_scope_free(scope);
    }
}

as_scope_t* as_scope_create_child(as_scope_t* scope, const char* name) {
    return as_scope_new(scope, name);
}

as_scope_t* as_scope_create_child_namespace(as_scope_t* scope, const char* ns_name) {
    as_scope_t* child = as_scope_new(scope, ns_name);
    child->ns_name = ns_name ? as_strdup(ns_name) : NULL;
    return child;
}

void as_scope_add(as_scope_t* scope, const char* name, as_value_t* value, bool is_mutable) {
    /* Check if variable already exists in this scope */
    for (size_t i = 0; i < scope->var_count; i++) {
        if (strcmp(scope->variables[i]->name, name) == 0) {
            fprintf(stderr, "Runtime error: Variable '%s' already exists in scope '%s'\n",
                    name, scope->name);
            return;
        }
    }
    
    /* Add new variable */
    if (scope->var_count >= scope->var_capacity) {
        size_t new_capacity = scope->var_capacity == 0 ? 8 : scope->var_capacity * 2;
        scope->variables = as_realloc(scope->variables, new_capacity * sizeof(as_variable_t*));
        scope->var_capacity = new_capacity;
    }
    
    as_variable_t* var = as_malloc(sizeof(as_variable_t));
    var->name = as_strdup(name);
    var->value = as_value_retain(value);
    var->is_mutable = is_mutable;
    
    scope->variables[scope->var_count++] = var;

    /* Namespace scopes also publish "Ns:name" on the parent (and ancestors). */
    if (scope->ns_name && scope->parent) {
        size_t nlen = strlen(scope->ns_name) + 1 + strlen(name) + 1;
        char* prefixed = as_malloc(nlen);
        snprintf(prefixed, nlen, "%s:%s", scope->ns_name, name);
        as_scope_add(scope->parent, prefixed, value, is_mutable);
        as_free(prefixed);
    }
}

as_value_t* as_scope_get(as_scope_t* scope, const char* name) {
    /* Search in current scope */
    for (size_t i = 0; i < scope->var_count; i++) {
        if (strcmp(scope->variables[i]->name, name) == 0) {
            return scope->variables[i]->value;
        }
    }
    
    /* Search in parent scope */
    if (scope->parent) {
        return as_scope_get(scope->parent, name);
    }
    
    return NULL;
}

bool as_scope_exists(as_scope_t* scope, const char* name) {
    /* Search in current scope */
    for (size_t i = 0; i < scope->var_count; i++) {
        if (strcmp(scope->variables[i]->name, name) == 0) {
            return true;
        }
    }
    
    /* Search in parent scope */
    if (scope->parent) {
        return as_scope_exists(scope->parent, name);
    }
    
    return false;
}

void as_scope_assign(as_scope_t* scope, const char* name, as_value_t* value) {
    /* Search in current scope */
    for (size_t i = 0; i < scope->var_count; i++) {
        if (strcmp(scope->variables[i]->name, name) == 0) {
            as_variable_t* var = scope->variables[i];
            if (!var->is_mutable) {
                fprintf(stderr, "Runtime error: Cannot assign to immutable variable '%s'\n", name);
                return;
            }
            as_value_release(var->value);
            var->value = as_value_retain(value);
            return;
        }
    }
    
    /* Search in parent scope */
    if (scope->parent) {
        as_scope_assign(scope->parent, name, value);
        return;
    }
    
    fprintf(stderr, "Runtime error: Variable '%s' not found in scope '%s'\n", name, scope->name);
}

