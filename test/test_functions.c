/*
 * Function Tests
 */

#include "test.h"

/* Basic function definition and call */
TEST(simple_function) {
    as_value_t* result = execute_script(
        "@add(a, b) { return a + b }\n"
        "<: add(3, 5)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 8);
    as_value_release(result);
}

/* Function without parameters */
TEST(no_param_function) {
    as_value_t* result = execute_script(
        "@get_number() { return 42 }\n"
        "<: get_number()"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 42);
    as_value_release(result);
}

/* Function with single parameter */
TEST(single_param_function) {
    as_value_t* result = execute_script(
        "@double(x) { return x * 2 }\n"
        "<: double(10)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 20);
    as_value_release(result);
}

/* Function expression (lambda) */
TEST(lambda_function) {
    as_value_t* result = execute_script(
        "let f = @(x) { return x + 1 }\n"
        "<: f(5)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 6);
    as_value_release(result);
}

/* Function with multiple statements */
TEST(multi_statement_function) {
    as_value_t* result = execute_script(
        "@calculate(x) {\n"
        "  let y = x + 1\n"
        "  let z = y * 2\n"
        "  return z\n"
        "}\n"
        "<: calculate(3)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 8);
    as_value_release(result);
}

/* Function with early return */
TEST(early_return) {
    as_value_t* result = execute_script(
        "@check(x) {\n"
        "  if x < 0 { return 0 }\n"
        "  return x * 2\n"
        "}\n"
        "<: check(-5)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 0);
    as_value_release(result);
}

/* Function with conditional return */
TEST(conditional_return) {
    as_value_t* result = execute_script(
        "@abs(x) {\n"
        "  if x < 0 { return -x }\n"
        "  return x\n"
        "}\n"
        "<: abs(-7)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 7);
    as_value_release(result);
}

/* Function calling another function */
TEST(nested_function_calls) {
    as_value_t* result = execute_script(
        "@add(a, b) { return a + b }\n"
        "@calculate() { return add(3, add(2, 1)) }\n"
        "<: calculate()"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 6);
    as_value_release(result);
}

/* Recursive function (factorial) */
TEST(recursive_factorial) {
    as_value_t* result = execute_script(
        "@factorial(n) {\n"
        "  if n <= 1 { return 1 }\n"
        "  return n * factorial(n - 1)\n"
        "}\n"
        "<: factorial(5)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 120);
    as_value_release(result);
}

/* Function returning string */
TEST(function_return_string) {
    as_value_t* result = execute_script(
        "@greet(name) { return name }\n"
        "<: greet(\"Alice\")"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT(strcmp(VALUE_STR(result), "Alice") == 0);
    as_value_release(result);
}

/* Function returning array */
TEST(function_return_array) {
    as_value_t* result = execute_script(
        "@make_array(a, b) { return [a, b] }\n"
        "<: make_array(1, 2)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_ARR);
    ASSERT_EQ(VALUE_ARR(result)->length, 2);
    ASSERT_EQ((int)VALUE_NUM(VALUE_ARR(result)->items[0]), 1);
    ASSERT_EQ((int)VALUE_NUM(VALUE_ARR(result)->items[1]), 2);
    as_value_release(result);
}

/* Function without explicit return */
TEST(implicit_return) {
    as_value_t* result = execute_script(
        "@last_value(x) {\n"
        "  let y = x + 1\n"
        "  y * 2\n"
        "}\n"
        "<: last_value(3)"
    );
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 8);
    as_value_release(result);
}

/* Export test functions */
void run_function_tests(void) {
    RUN_TEST(simple_function);
    RUN_TEST(no_param_function);
    RUN_TEST(single_param_function);
    RUN_TEST(lambda_function);
    RUN_TEST(multi_statement_function);
    RUN_TEST(early_return);
    RUN_TEST(conditional_return);
    RUN_TEST(nested_function_calls);
    RUN_TEST(recursive_factorial);
    RUN_TEST(function_return_string);
    RUN_TEST(function_return_array);
    RUN_TEST(implicit_return);
}

