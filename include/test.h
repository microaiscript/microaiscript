#ifndef AISCRIPT_TEST_H
#define AISCRIPT_TEST_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "types.h"

// Set by the assertion macros when an assertion fails; run_test() reads and
// clears it so a failed assertion fails the test.
extern int as_test_assertion_failed;

// Test framework macros
#define TEST(name) void test_##name(void)
#define RUN_TEST(name) run_test(#name, test_##name)

// Assertion macros
#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Assertion failed: %s at %s:%d\n", \
                    #condition, __FILE__, __LINE__); \
            as_test_assertion_failed = 1; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            fprintf(stderr, "Assertion failed: %s == %s (got %d != %d) at %s:%d\n", \
                    #a, #b, (int)(a), (int)(b), __FILE__, __LINE__); \
            as_test_assertion_failed = 1; \
            return; \
        } \
    } while(0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "Assertion failed: %s == %s (got '%s' != '%s') at %s:%d\n", \
                    #a, #b, (a), (b), __FILE__, __LINE__); \
            as_test_assertion_failed = 1; \
            return; \
        } \
    } while(0)

#define ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            fprintf(stderr, "Assertion failed: %s is NULL at %s:%d\n", \
                    #ptr, __FILE__, __LINE__); \
            as_test_assertion_failed = 1; \
            return; \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            fprintf(stderr, "Assertion failed: %s is not NULL at %s:%d\n", \
                    #ptr, __FILE__, __LINE__); \
            as_test_assertion_failed = 1; \
            return; \
        } \
    } while(0)

// Test runner
typedef void (*test_func)(void);

void run_test(const char* name, test_func func);
void run_all_tests(void);

// Helper macros for accessing value fields
#define VALUE_BOOL(v) ((v)->data.bool_val)
#define VALUE_NUM(v) ((v)->data.num_val)
#define VALUE_STR(v) ((v)->data.str_val)
#define VALUE_ARR(v) ((v)->data.arr_val)
#define VALUE_OBJ(v) ((v)->data.obj_val)

// Helper functions for testing AiScript execution
as_value_t* execute_script(const char* source);
bool value_equals(as_value_t* a, as_value_t* b);

#endif // AISCRIPT_TEST_H

