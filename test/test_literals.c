#include <stdio.h>
#include <string.h>
#include "test.h"
#include "aiscript.h"

// Test: Hello, world!
TEST(hello_world) {
    as_value_t* result = execute_script("<: \"Hello, world!\"");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "Hello, world!");
    as_value_free(result);
}

// Test: String with single quotes
TEST(string_single_quote) {
    as_value_t* result = execute_script("<: 'foo'");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "foo");
    as_value_free(result);
}

// Test: String with double quotes
TEST(string_double_quote) {
    as_value_t* result = execute_script("<: \"foo\"");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "foo");
    as_value_free(result);
}

// Test: Boolean true
TEST(bool_true) {
    as_value_t* result = execute_script("<: true");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result) == true);
    as_value_free(result);
}

// Test: Boolean false
TEST(bool_false) {
    as_value_t* result = execute_script("<: false");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result) == false);
    as_value_free(result);
}

// Test: Number integer
TEST(number_int) {
    as_value_t* result = execute_script("<: 10");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 10);
    as_value_free(result);
}

// Test: Number float
TEST(number_float) {
    as_value_t* result = execute_script("<: 0.5");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT(VALUE_NUM(result) >= 0.49 && VALUE_NUM(result) <= 0.51);
    as_value_free(result);
}

// Test: Null
TEST(null_value) {
    as_value_t* result = execute_script("<: null");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NULL);
    as_value_free(result);
}

// Test: Array with comma separator
TEST(array_comma_separated) {
    as_value_t* result = execute_script("<: [1, 2, 3]");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_ARR);
    ASSERT_EQ(VALUE_ARR(result)->length, 3);
    ASSERT_EQ((int)VALUE_NUM(VALUE_ARR(result)->items[0]), 1);
    ASSERT_EQ((int)VALUE_NUM(VALUE_ARR(result)->items[1]), 2);
    ASSERT_EQ((int)VALUE_NUM(VALUE_ARR(result)->items[2]), 3);
    as_value_free(result);
}

// Test: Object literal
TEST(object_literal) {
    as_value_t* result = execute_script("<: { a: 1, b: 2, c: 3 }");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_OBJ);
    ASSERT_EQ(VALUE_OBJ(result)->size, 3);
    as_value_free(result);
}

void run_literal_tests(void) {
    printf("\n--- Literal Tests ---\n");
    RUN_TEST(hello_world);
    RUN_TEST(string_single_quote);
    RUN_TEST(string_double_quote);
    RUN_TEST(bool_true);
    RUN_TEST(bool_false);
    RUN_TEST(number_int);
    RUN_TEST(number_float);
    RUN_TEST(null_value);
    RUN_TEST(array_comma_separated);
    RUN_TEST(object_literal);
}

