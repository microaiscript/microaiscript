#include <stdio.h>
#include "test.h"
#include "aiscript.h"

// Test: Simple if expression
TEST(simple_if) {
    as_value_t* result = execute_script("<: if (true) { 1 } else { 2 }");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 1);
    as_value_free(result);
}

// Test: If-else with false condition
TEST(if_else_false) {
    as_value_t* result = execute_script("<: if (false) { 1 } else { 2 }");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 2);
    as_value_free(result);
}

// Test: If-elif-else
TEST(if_elif_else) {
    const char* script = 
        "let x = 5\n"
        "<: if (x < 5) { \"less\" } elif (x == 5) { \"equal\" } else { \"greater\" }";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "equal");
    as_value_free(result);
}

// Test: Nested if
TEST(nested_if) {
    const char* script = 
        "let x = 10\n"
        "let y = 20\n"
        "<: if (x < 20) { if (y > 15) { \"both true\" } else { \"first true\" } } else { \"neither\" }";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "both true");
    as_value_free(result);
}

// Test: If with comparison
TEST(if_with_comparison) {
    as_value_t* result = execute_script("<: if (10 > 5) { \"yes\" } else { \"no\" }");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "yes");
    as_value_free(result);
}

// Test: If with variable
TEST(if_with_variable) {
    const char* script = 
        "let x = 10\n"
        "let msg = if (x > 5) { \"big\" } else { \"small\" }\n"
        "<: msg";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "big");
    as_value_free(result);
}

void run_conditional_tests(void) {
    printf("\n--- Conditional Tests ---\n");
    RUN_TEST(simple_if);
    RUN_TEST(if_else_false);
    RUN_TEST(if_elif_else);
    RUN_TEST(nested_if);
    RUN_TEST(if_with_comparison);
    RUN_TEST(if_with_variable);
}


