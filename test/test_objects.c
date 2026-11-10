#include <stdio.h>
#include "test.h"
#include "aiscript.h"

// Test: Object property access
TEST(object_property_access) {
    const char* script = 
        "let person = { name: \"Alice\", age: 30 }\n"
        "<: person.name";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "Alice");
    as_value_free(result);
}

// Test: Empty object
TEST(empty_object) {
    as_value_t* result = execute_script("<: {}");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_OBJ);
    ASSERT_EQ(VALUE_OBJ(result)->size, 0);
    as_value_free(result);
}

// Test: Object with multiple properties
TEST(object_multiple_properties) {
    const char* script = "<: { a: 1, b: 2, c: 3 }";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_OBJ);
    ASSERT_EQ(VALUE_OBJ(result)->size, 3);
    as_value_free(result);
}

// Test: Nested object access
TEST(nested_object_access) {
    const char* script = 
        "let obj = { a: { b: { c: 42 } } }\n"
        "<: obj.a.b.c";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 42);
    as_value_free(result);
}

// Test: Object type
TEST(object_type) {
    const char* script = 
        "let person = { name: \"Alice\" }\n"
        "<: Core:type(person)";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "obj");
    as_value_free(result);
}

void run_object_tests(void) {
    printf("\n--- Object Tests ---\n");
    RUN_TEST(object_property_access);
    RUN_TEST(empty_object);
    RUN_TEST(object_multiple_properties);
    RUN_TEST(nested_object_access);
    RUN_TEST(object_type);
}


