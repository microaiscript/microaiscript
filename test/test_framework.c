#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "aiscript.h"
#include "parser.h"
#include "interpreter.h"
#include "node.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
int as_test_assertion_failed = 0;

void run_test(const char* name, test_func func) {
    tests_run++;
    printf("Running test: %s ... ", name);
    fflush(stdout);
    
    as_test_assertion_failed = 0;
    func();
    
    if (as_test_assertion_failed) {
        tests_failed++;
        printf("FAILED\n");
    } else {
        tests_passed++;
        printf("PASSED\n");
    }
}

int as_test_failure_count(void) {
    return tests_failed;
}

void run_all_tests(void) {
    printf("\n=== Running AiScript C Test Suite ===\n\n");
}

void print_test_summary(void) {
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\n✓ All tests passed!\n");
    } else {
        printf("\n✗ Some tests failed.\n");
    }
}

// Helper function to execute a script and return the result
as_value_t* execute_script(const char* source) {
    as_parser_t* parser = as_parser_new(source);
    if (!parser) {
        return NULL;
    }
    
    size_t node_count = 0;
    as_node_t** nodes = as_parser_parse(parser, &node_count);
    
    if (!nodes) {
        as_parser_free(parser);
        return NULL;
    }
    
    as_interpreter_t* interpreter = as_interpreter_new();
    if (!interpreter) {
        as_nodes_free(nodes, node_count);
        as_parser_free(parser);
        return NULL;
    }
    
    as_value_t* result = as_interpreter_exec(interpreter, nodes, node_count);
    
    // Clean up (but keep the result)
    as_nodes_free(nodes, node_count);
    as_parser_free(parser);
    as_interpreter_free(interpreter);
    
    return result;
}

// Helper function to compare two values
bool value_equals(as_value_t* a, as_value_t* b) {
    if (!a || !b) {
        return a == b;
    }
    
    if (a->type != b->type) {
        return false;
    }
    
    switch (a->type) {
        case AS_VALUE_NULL:
            return true;
            
        case AS_VALUE_BOOL:
            return VALUE_BOOL(a) == VALUE_BOOL(b);
            
        case AS_VALUE_NUM:
            // Handle NaN specially
            if (isnan(VALUE_NUM(a)) && isnan(VALUE_NUM(b))) {
                return true;
            }
            return fabs(VALUE_NUM(a) - VALUE_NUM(b)) < 1e-10;
            
        case AS_VALUE_STR:
            if (!VALUE_STR(a) || !VALUE_STR(b)) {
                return VALUE_STR(a) == VALUE_STR(b);
            }
            return strcmp(VALUE_STR(a), VALUE_STR(b)) == 0;
            
        case AS_VALUE_ARR:
            if (VALUE_ARR(a)->length != VALUE_ARR(b)->length) {
                return false;
            }
            for (size_t i = 0; i < VALUE_ARR(a)->length; i++) {
                if (!value_equals(VALUE_ARR(a)->items[i], VALUE_ARR(b)->items[i])) {
                    return false;
                }
            }
            return true;
            
        case AS_VALUE_OBJ: {
            if (VALUE_OBJ(a)->size != VALUE_OBJ(b)->size) {
                return false;
            }
            for (size_t bi = 0; bi < VALUE_OBJ(a)->bucket_count; bi++) {
                as_obj_entry_t* e = VALUE_OBJ(a)->buckets[bi];
                while (e) {
                    as_value_t* other = as_obj_get(VALUE_OBJ(b), e->key);
                    if (!other || !value_equals(e->value, other)) {
                        return false;
                    }
                    e = e->next;
                }
            }
            return true;
        }
            
        default:
            return false;
    }
}

