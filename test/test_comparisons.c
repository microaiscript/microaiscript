#include <stdio.h>
#include "test.h"
#include "aiscript.h"

// Test: Less than
TEST(less_than) {
    as_value_t* result = execute_script("<: 5 < 10");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result) == true);
    as_value_free(result);
}

// Test: Greater than
TEST(greater_than) {
    as_value_t* result = execute_script("<: 5 > 10");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result) == false);
    as_value_free(result);
}

// Test: Less than or equal
TEST(less_than_or_equal) {
    as_value_t* result1 = execute_script("<: 5 <= 10");
    ASSERT_NOT_NULL(result1);
    ASSERT(result1->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result1) == true);
    as_value_free(result1);
    
    as_value_t* result2 = execute_script("<: 5 <= 5");
    ASSERT_NOT_NULL(result2);
    ASSERT(result2->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result2) == true);
    as_value_free(result2);
}

// Test: Greater than or equal
TEST(greater_than_or_equal) {
    as_value_t* result1 = execute_script("<: 10 >= 5");
    ASSERT_NOT_NULL(result1);
    ASSERT(result1->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result1) == true);
    as_value_free(result1);
    
    as_value_t* result2 = execute_script("<: 5 >= 5");
    ASSERT_NOT_NULL(result2);
    ASSERT(result2->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result2) == true);
    as_value_free(result2);
}

// Test: Equality
TEST(equality) {
    as_value_t* result = execute_script("<: 5 == 5");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result) == true);
    as_value_free(result);
}

// Test: Inequality
TEST(inequality) {
    as_value_t* result = execute_script("<: 5 != 10");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result) == true);
    as_value_free(result);
}

// Test: Logical AND
TEST(logical_and) {
    as_value_t* result1 = execute_script("<: true && true");
    ASSERT_NOT_NULL(result1);
    ASSERT(result1->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result1) == true);
    as_value_free(result1);
    
    as_value_t* result2 = execute_script("<: true && false");
    ASSERT_NOT_NULL(result2);
    ASSERT(result2->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result2) == false);
    as_value_free(result2);
}

// Test: Logical OR
TEST(logical_or) {
    as_value_t* result1 = execute_script("<: true || false");
    ASSERT_NOT_NULL(result1);
    ASSERT(result1->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result1) == true);
    as_value_free(result1);
    
    as_value_t* result2 = execute_script("<: false || false");
    ASSERT_NOT_NULL(result2);
    ASSERT(result2->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result2) == false);
    as_value_free(result2);
}

// Test: Logical NOT
TEST(logical_not) {
    as_value_t* result1 = execute_script("<: !false");
    ASSERT_NOT_NULL(result1);
    ASSERT(result1->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result1) == true);
    as_value_free(result1);
    
    as_value_t* result2 = execute_script("<: !true");
    ASSERT_NOT_NULL(result2);
    ASSERT(result2->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result2) == false);
    as_value_free(result2);
}

void run_comparison_tests(void) {
    printf("\n--- Comparison and Logical Tests ---\n");
    RUN_TEST(less_than);
    RUN_TEST(greater_than);
    RUN_TEST(less_than_or_equal);
    RUN_TEST(greater_than_or_equal);
    RUN_TEST(equality);
    RUN_TEST(inequality);
    RUN_TEST(logical_and);
    RUN_TEST(logical_or);
    RUN_TEST(logical_not);
}

