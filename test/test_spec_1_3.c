/*
 * Tests for AiScript 1.3.0 language and standard library changes:
 * object shorthand, duplicate-key errors, Math:gen_rng validation and the
 * ChaCha20 numeric-seed fix, the `help` constant, and the absence of
 * exponent notation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "aiscript.h"

/* Run `source` in a fresh interpreter. Returns the result (NULL on error).
 * *syntax_error receives a malloc'd syntax error message, if any. If
 * runtime_error is non-NULL it receives a copy of the runtime error message. */
static as_value_t* eval_checked(const char* source, char** syntax_error, char** runtime_error) {
    as_interpreter_t* interp = as_interpreter_new();
    as_value_t* result;

    *syntax_error = NULL;
    if (runtime_error) *runtime_error = NULL;
    result = as_eval(interp, source, syntax_error);
    if (runtime_error && as_interpreter_get_error(interp)) {
        *runtime_error = strdup(as_interpreter_get_error(interp));
    }
    as_interpreter_free(interp);
    return result;
}

/* Assert that `source` is rejected with a syntax error containing `fragment`. */
#define ASSERT_SYNTAX_ERROR(source, fragment) \
    do { \
        char* syn_ = NULL; \
        as_value_t* res_ = eval_checked((source), &syn_, NULL); \
        ASSERT_NULL(res_); \
        ASSERT_NOT_NULL(syn_); \
        ASSERT(strstr(syn_, (fragment)) != NULL); \
        free(syn_); \
    } while (0)

TEST(obj_shorthand_literal) {
    char* syn;
    as_value_t* result = eval_checked("let a = 1\n<: { a }.a", &syn, NULL);
    ASSERT_NULL(syn);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_NUM);
    ASSERT_EQ((int)VALUE_NUM(result), 1);
    as_value_free(result);
}

TEST(obj_shorthand_mixed) {
    char* syn;
    as_value_t* result = eval_checked("let a = 1\nlet b = 2\n<: { a, c: 3, b }", &syn, NULL);
    ASSERT_NULL(syn);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_OBJ);
    ASSERT_EQ((int)VALUE_OBJ(result)->size, 3);
    as_value_free(result);
}

TEST(obj_shorthand_destructuring) {
    char* syn;
    as_value_t* result = eval_checked("let { value } = { value: 7 }\n<: value", &syn, NULL);
    ASSERT_NULL(syn);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((int)VALUE_NUM(result), 7);
    as_value_free(result);
}

TEST(obj_shorthand_keyword_is_syntax_error) {
    ASSERT_SYNTAX_ERROR("<: { exists }", "Expected ':'");
}

TEST(obj_shorthand_string_is_syntax_error) {
    ASSERT_SYNTAX_ERROR("<: { \"hoge\" }", "Expected ':'");
}

TEST(obj_duplicate_key_is_syntax_error) {
    ASSERT_SYNTAX_ERROR("<: { hoge: 1, hoge: 2 }", "Key hoge is duplicated.");
}

TEST(obj_duplicate_key_string_and_identifier) {
    ASSERT_SYNTAX_ERROR("<: { \"a\": 1, a: 2 }", "Key a is duplicated.");
}

TEST(obj_duplicate_key_in_pattern) {
    ASSERT_SYNTAX_ERROR("let { v, v } = { v: 1 }", "Key v is duplicated.");
}

TEST(help_constant) {
    char* syn;
    as_value_t* result = eval_checked("<: help", &syn, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_STR);
    ASSERT_STR_EQ(VALUE_STR(result), "SEE: https://aiscript-dev.github.io/guides/get-started.html");
    as_value_free(result);
}

TEST(core_version) {
    char* syn;
    as_value_t* result = eval_checked("<: Core:v", &syn, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(VALUE_STR(result), "1.3.0-c");
    as_value_free(result);
}

TEST(exponent_notation_is_not_a_number_literal) {
    char* syn;
    char* rt;
    as_value_t* result = eval_checked("<: 1e3", &syn, &rt);
    /* `1e3` is the number 1 followed by the identifier e3, which is undefined. */
    ASSERT(result == NULL || result->type != AS_VALUE_NUM || VALUE_NUM(result) != 1000.0);
    if (result) as_value_free(result);
    free(syn);
    free(rt);
}

TEST(gen_rng_rejects_null_seed) {
    char* syn;
    char* rt;
    as_value_t* result = eval_checked("Math:gen_rng(null)", &syn, &rt);
    ASSERT_NULL(result);
    ASSERT_NOT_NULL(rt);
    ASSERT_STR_EQ(rt, "`seed` must be either number or string.");
    free(syn);
    free(rt);
}

TEST(gen_rng_rejects_non_object_options) {
    char* syn;
    char* rt;
    as_value_t* result = eval_checked("Math:gen_rng(1, 5)", &syn, &rt);
    ASSERT_NULL(result);
    ASSERT_NOT_NULL(rt);
    ASSERT_STR_EQ(rt, "`options` must be an object if specified.");
    free(syn);
    free(rt);
}

TEST(gen_rng_options_requires_algorithm) {
    char* syn;
    char* rt;
    as_value_t* result = eval_checked("Math:gen_rng(1, {})", &syn, &rt);
    ASSERT_NULL(result);
    ASSERT_NOT_NULL(rt);
    ASSERT_STR_EQ(rt, "`options.algorithm` must be string.");
    free(syn);
    free(rt);
}

/* 3 and the next representable double must seed different streams (the
 * pre-1.3.0 behaviour truncated the seed to one byte). */
TEST(gen_rng_chacha20_number_seed_uses_all_bits) {
    char* syn;
    as_value_t* result = eval_checked(
        "<: Math:gen_rng(3)() == Math:gen_rng(3.0000000000000004)()", &syn, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_BOOL);
    ASSERT(!VALUE_BOOL(result));
    as_value_free(result);
}

TEST(gen_rng_same_seed_is_deterministic) {
    char* syn;
    as_value_t* result = eval_checked(
        "<: Math:gen_rng(42)() == Math:gen_rng(42)()", &syn, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT(VALUE_BOOL(result));
    as_value_free(result);
}

TEST(gen_rng_legacy_number_seed_behaviour) {
    char* syn;
    /* Legacy mode keeps only the low byte of the truncated seed. */
    as_value_t* result = eval_checked(
        "let o = { algorithm: 'chacha20', chacha20_number_seed_legacy_behavior: true }\n"
        "<: Math:gen_rng(3, o)() == Math:gen_rng(259.5, o)()", &syn, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT(VALUE_BOOL(result));
    as_value_free(result);
}

/* AiScript 1.2.1: attribute values are evaluated, including those of namespace members */
TEST(attribute_values_are_evaluated) {
    char* syn;
    char* rt;
    as_value_t* result = eval_checked("#[x missing_name]\n@f() { 1 }\n<: f()", &syn, &rt);
    ASSERT_NOT_NULL(result);
    ASSERT(result->type == AS_VALUE_ERROR);
    as_value_free(result);
    free(syn);
    free(rt);
}

TEST(namespace_member_attribute_values_are_evaluated) {
    char* syn;
    char* rt;
    as_value_t* result = eval_checked(":: Ns { #[x missing_name]\n@f() { 1 } }\n<: Ns:f()", &syn, &rt);
    ASSERT(result == NULL || result->type == AS_VALUE_ERROR);
    if (result) as_value_free(result);
    free(syn);
    free(rt);
}

TEST(namespace_member_with_literal_attribute) {
    char* syn;
    as_value_t* result = eval_checked(":: Ns { #[x 42]\n@f() { 7 } }\n<: Ns:f()", &syn, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((int)VALUE_NUM(result), 7);
    as_value_free(result);
}

void run_spec_1_3_tests(void) {
    printf("\n--- AiScript 1.3.0 Tests ---\n");
    RUN_TEST(obj_shorthand_literal);
    RUN_TEST(obj_shorthand_mixed);
    RUN_TEST(obj_shorthand_destructuring);
    RUN_TEST(obj_shorthand_keyword_is_syntax_error);
    RUN_TEST(obj_shorthand_string_is_syntax_error);
    RUN_TEST(obj_duplicate_key_is_syntax_error);
    RUN_TEST(obj_duplicate_key_string_and_identifier);
    RUN_TEST(obj_duplicate_key_in_pattern);
    RUN_TEST(help_constant);
    RUN_TEST(core_version);
    RUN_TEST(exponent_notation_is_not_a_number_literal);
    RUN_TEST(gen_rng_rejects_null_seed);
    RUN_TEST(gen_rng_rejects_non_object_options);
    RUN_TEST(gen_rng_options_requires_algorithm);
    RUN_TEST(gen_rng_chacha20_number_seed_uses_all_bits);
    RUN_TEST(gen_rng_same_seed_is_deterministic);
    RUN_TEST(gen_rng_legacy_number_seed_behaviour);
    RUN_TEST(attribute_values_are_evaluated);
    RUN_TEST(namespace_member_attribute_values_are_evaluated);
    RUN_TEST(namespace_member_with_literal_attribute);
}
