#include <stdio.h>
#include <math.h>
#include "test.h"
#include "aiscript.h"

// Test: Addition
TEST(addition) {
    as_value_t* result = execute_script("<: 2 + 3");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 5);
    as_value_free(result);
}

// Test: Subtraction
TEST(subtraction) {
    as_value_t* result = execute_script("<: 10 - 4");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 6);
    as_value_free(result);
}

// Test: Multiplication
TEST(multiplication) {
    as_value_t* result = execute_script("<: 6 * 7");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 42);
    as_value_free(result);
}

// Test: Division
TEST(division) {
    as_value_t* result = execute_script("<: 15 / 3");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 5);
    as_value_free(result);
}

// Test: Modulo
TEST(modulo) {
    as_value_t* result = execute_script("<: 17 % 5");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 2);
    as_value_free(result);
}

// Test: Power
TEST(power) {
    as_value_t* result = execute_script("<: 2 ^ 3");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 8);
    as_value_free(result);
}

// Test: Operator precedence
TEST(operator_precedence) {
    as_value_t* result = execute_script("<: 2 + 3 * 4");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 14);
    as_value_free(result);
}

// Test: Parentheses
TEST(parentheses) {
    as_value_t* result = execute_script("<: (2 + 3) * 4");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 20);
    as_value_free(result);
}

// Test: Division by zero
TEST(division_by_zero) {
    as_value_t* result = execute_script("<: 0 / 0");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT(isnan(VALUE_NUM(result)));
    as_value_free(result);
}

void run_arithmetic_tests(void) {
    printf("\n--- Arithmetic Tests ---\n");
    RUN_TEST(addition);
    RUN_TEST(subtraction);
    RUN_TEST(multiplication);
    RUN_TEST(division);
    RUN_TEST(modulo);
    RUN_TEST(power);
    RUN_TEST(operator_precedence);
    RUN_TEST(parentheses);
    RUN_TEST(division_by_zero);
}


