#include <stdio.h>
#include "test.h"
#include "aiscript.h"

// Test: Let variable
TEST(let_variable) {
    as_value_t* result = execute_script("let x = 42\n<: x");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 42);
    as_value_free(result);
}

// Test: Var variable
TEST(var_variable) {
    as_value_t* result = execute_script("var y = 10\n<: y");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 10);
    as_value_free(result);
}

// Test: Variable mutation
TEST(variable_mutation) {
    const char* script = 
        "var y = 10\n"
        "y = 20\n"
        "<: y";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 20);
    as_value_free(result);
}

// Test: Increment (+=)
TEST(increment) {
    const char* script = 
        "var a = 0\n"
        "a += 1\n"
        "a += 2\n"
        "a += 3\n"
        "<: a";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 6);
    as_value_free(result);
}

// Test: Decrement (-=)
TEST(decrement) {
    const char* script = 
        "var a = 0\n"
        "a -= 1\n"
        "a -= 2\n"
        "a -= 3\n"
        "<: a";
    as_value_t* result = execute_script(script);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), -6);
    as_value_free(result);
}

// Test: String variable
TEST(string_variable) {
    as_value_t* result = execute_script("let str = \"Hello\"\n<: str");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "Hello");
    as_value_free(result);
}

// Test: Boolean variable
TEST(bool_variable) {
    as_value_t* result = execute_script("let flag = true\n<: flag");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(VALUE_BOOL(result) == true);
    as_value_free(result);
}

// Test: Null variable
TEST(null_variable) {
    as_value_t* result = execute_script("let nothing = null\n<: nothing");
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NULL);
    as_value_free(result);
}

void run_variable_tests(void) {
    printf("\n--- Variable Tests ---\n");
    RUN_TEST(let_variable);
    RUN_TEST(var_variable);
    RUN_TEST(variable_mutation);
    RUN_TEST(increment);
    RUN_TEST(decrement);
    RUN_TEST(string_variable);
    RUN_TEST(bool_variable);
    RUN_TEST(null_variable);
}


