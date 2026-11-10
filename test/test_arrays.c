#include <stdio.h>
#include "test.h"
#include "aiscript.h"

// Test: Array item access
TEST(array_item_access) {
    const char* script = 
        "let arr = [\"ai\", \"chan\", \"kawaii\"]\n"
        "<: arr[1]";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "chan");
    as_value_free(result);
}

// Test: Array length
TEST(array_length) {
    const char* script = 
        "let arr = [1, 2, 3]\n"
        "<: arr.len";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 3);
    as_value_free(result);
}

// Test: Empty array
TEST(empty_array) {
    as_value_t* result = execute_script("<: []");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_ARR);
    ASSERT_EQ(VALUE_ARR(result)->length, 0);
    as_value_free(result);
}

// Test: Mixed type array
TEST(mixed_type_array) {
    const char* script = "<: [1, \"hello\", true, null]";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_ARR);
    ASSERT_EQ(VALUE_ARR(result)->length, 4);
    ASSERT(VALUE_ARR(result)->items[0]->type == AS_VALUE_NUM);
    ASSERT(VALUE_ARR(result)->items[1]->type == AS_VALUE_STR);
    ASSERT(VALUE_ARR(result)->items[2]->type == AS_VALUE_BOOL);
    ASSERT(VALUE_ARR(result)->items[3]->type == AS_VALUE_NULL);
    as_value_free(result);
}

void run_array_tests(void) {
    printf("\n--- Array Tests ---\n");
    RUN_TEST(array_item_access);
    RUN_TEST(array_length);
    RUN_TEST(empty_array);
    RUN_TEST(mixed_type_array);
}

