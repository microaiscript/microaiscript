/*
 * Tests for error reporting through the embedding API: syntax errors,
 * runtime errors (including ones raised inside functions) and recovery.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "aiscript.h"

TEST(syntax_error_is_reported) {
    as_interpreter_t* interp = as_interpreter_new();
    char* error = NULL;
    as_value_t* result = as_eval(interp, "let a = ", &error);
    ASSERT_NULL(result);
    ASSERT_NOT_NULL(error);
    ASSERT(strstr(error, "Syntax error") != NULL);
    free(error);
    as_interpreter_free(interp);
}

TEST(unknown_property_is_a_runtime_error) {
    as_interpreter_t* interp = as_interpreter_new();
    char* error = NULL;
    as_value_t* result = as_eval(interp, "<: \"\".constructor", &error);
    ASSERT_NULL(result);
    ASSERT_NULL(error);
    ASSERT_NOT_NULL(as_interpreter_get_error(interp));
    ASSERT(strstr(as_interpreter_get_error(interp), "No such property 'constructor' for type str") != NULL);
    as_interpreter_free(interp);
}

TEST(runtime_error_inside_function_propagates) {
    as_interpreter_t* interp = as_interpreter_new();
    char* error = NULL;
    as_value_t* result = as_eval(interp,
        "@f() { \"\".nope }\n"
        "f()\n"
        "<: \"unreachable\"", &error);
    ASSERT_NULL(result);
    ASSERT_NOT_NULL(as_interpreter_get_error(interp));
    as_interpreter_free(interp);
}

TEST(return_inside_function_is_not_an_error) {
    as_interpreter_t* interp = as_interpreter_new();
    char* error = NULL;
    as_value_t* result = as_eval(interp,
        "@f() { return 5 }\n"
        "<: f() + 1", &error);
    ASSERT_NOT_NULL(result);
    ASSERT_NULL(as_interpreter_get_error(interp));
    ASSERT_EQ((int)VALUE_NUM(result), 6);
    as_value_free(result);
    as_interpreter_free(interp);
}

TEST(interpreter_recovers_after_error) {
    as_interpreter_t* interp = as_interpreter_new();
    char* error = NULL;
    as_value_t* result = as_eval(interp, "<: \"\".nope", &error);
    ASSERT_NULL(result);
    ASSERT_NOT_NULL(as_interpreter_get_error(interp));

    /* The next evaluation starts with a clean slate */
    result = as_eval(interp, "@f() { 1 }\n<: f()", &error);
    ASSERT_NOT_NULL(result);
    ASSERT_NULL(as_interpreter_get_error(interp));
    ASSERT_EQ((int)VALUE_NUM(result), 1);
    as_value_free(result);
    as_interpreter_free(interp);
}

TEST(division_follows_ieee_754) {
    as_interpreter_t* interp = as_interpreter_new();
    char* error = NULL;
    as_value_t* result = as_eval(interp, "<: [0 / 0, 1 / 0, -1 / 0]", &error);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_ARR);
    ASSERT(VALUE_ARR(result)->items[0]->data.num_val != VALUE_ARR(result)->items[0]->data.num_val);
    ASSERT(VALUE_ARR(result)->items[1]->data.num_val > 1e308);
    ASSERT(VALUE_ARR(result)->items[2]->data.num_val < -1e308);
    as_value_free(result);
    as_interpreter_free(interp);
}

TEST(number_formatting) {
    char buf[64];
    as_num_to_str(0.1 + 0.2, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.30000000000000004");
    as_num_to_str(100, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "100");
    as_num_to_str(-0.0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0");
    as_num_to_str(1.0 / 0.0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Infinity");
}

static char g_captured_error[512];

static void capture_error(const char* message, void* userdata) {
    (void)userdata;
    snprintf(g_captured_error, sizeof(g_captured_error), "%s", message);
}

TEST(error_callback_receives_runtime_errors) {
    as_interp_opts_t opts;
    as_interpreter_t* interp;
    char* error = NULL;

    memset(&opts, 0, sizeof(opts));
    opts.err = capture_error;
    g_captured_error[0] = '\0';
    interp = as_interpreter_new_ex(&opts);

    ASSERT_NULL(as_eval(interp, "Math:gen_rng(null)", &error));
    ASSERT(strstr(g_captured_error, "`seed` must be either number or string.") != NULL);

    g_captured_error[0] = '\0';
    as_interpreter_clear_error(interp);
    as_value_free(as_eval(interp, "<: undefined_name", &error));
    ASSERT(strstr(g_captured_error, "NameError") != NULL);
    as_interpreter_free(interp);
}

void run_error_tests(void) {
    printf("\n--- Error Handling Tests ---\n");
    RUN_TEST(syntax_error_is_reported);
    RUN_TEST(unknown_property_is_a_runtime_error);
    RUN_TEST(runtime_error_inside_function_propagates);
    RUN_TEST(return_inside_function_is_not_an_error);
    RUN_TEST(interpreter_recovers_after_error);
    RUN_TEST(division_follows_ieee_754);
    RUN_TEST(number_formatting);
    RUN_TEST(error_callback_receives_runtime_errors);
}
