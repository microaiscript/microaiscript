/*
 * AiScript - Scope (variable management) interface
 */

#ifndef AISCRIPT_SCOPE_H
#define AISCRIPT_SCOPE_H

#include "types.h"

/* Variable structure */
typedef struct {
    char* name;
    as_value_t* value;
    bool is_mutable;
} as_variable_t;

/* Scope structure */
struct as_scope {
    as_scope_t* parent;
    as_variable_t** variables;
    size_t var_count;
    size_t var_capacity;
    char* name;
    char* ns_name;  /* Non-NULL if this is a namespace scope (Foo, Bar, ...) */
    int ref_count;  /* Reference counting for closure lifetimes */
};

/* Create and destroy. Scopes are reference counted so that closures keep theirs alive. */
AISCRIPT_API as_scope_t* as_scope_new(as_scope_t* parent, const char* name);
AISCRIPT_API void as_scope_free(as_scope_t* scope);
AISCRIPT_API as_scope_t* as_scope_retain(as_scope_t* scope);
AISCRIPT_API void as_scope_release(as_scope_t* scope);

/* Scope operations. Namespaced names are stored under their full name
 * ("Math:sin"). Lookups walk up the parent chain. */
AISCRIPT_API as_scope_t* as_scope_create_child(as_scope_t* scope, const char* name);
AISCRIPT_API as_scope_t* as_scope_create_child_namespace(as_scope_t* scope, const char* ns_name);
AISCRIPT_API void as_scope_add(as_scope_t* scope, const char* name, as_value_t* value, bool is_mutable);
AISCRIPT_API as_value_t* as_scope_get(as_scope_t* scope, const char* name);
AISCRIPT_API bool as_scope_exists(as_scope_t* scope, const char* name);
AISCRIPT_API void as_scope_assign(as_scope_t* scope, const char* name, as_value_t* value);

#endif /* AISCRIPT_SCOPE_H */

