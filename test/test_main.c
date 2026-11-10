#include <stdio.h>
#include "test.h"

// External test suite declarations
extern void run_literal_tests(void);
extern void run_arithmetic_tests(void);
extern void run_variable_tests(void);
extern void run_comparison_tests(void);
extern void run_conditional_tests(void);
extern void run_array_tests(void);
extern void run_object_tests(void);
extern void run_function_tests(void);
extern void run_spec_1_3_tests(void);
extern void run_error_tests(void);
extern void print_test_summary(void);
extern int as_test_failure_count(void);

int main(void) {
    run_all_tests();
    
    // Run all test suites
    run_literal_tests();
    run_arithmetic_tests();
    run_variable_tests();
    run_comparison_tests();
    run_conditional_tests();
    run_array_tests();
    run_object_tests();
    run_function_tests();
    run_spec_1_3_tests();
    run_error_tests();
    
    print_test_summary();
    
    return as_test_failure_count() == 0 ? 0 : 1;
}

