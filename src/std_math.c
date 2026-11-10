/*
 * AiScript Standard Library - Math Namespace Implementation
 * 
 * Most functions are thin wrappers around C's libm functions.
 */

#include "std_math.h"
#include "std_rng.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define _GNU_SOURCE  /* Enable mathematical constants */
#include <math.h>
#include <stdint.h>

#ifndef NO_MATH

/* ============================================================================
 * HELPER: Get numeric argument
 * ============================================================================ */
static bool get_num_arg(as_value_t** args, size_t arg_count, size_t index, double* out) {
    if (arg_count <= index || args[index]->type != AS_VALUE_NUM) {
        return false;
    }
    *out = args[index]->data.num_val;
    return true;
}

/* ============================================================================
 * BASIC FUNCTIONS
 * ============================================================================ */

/* Math:abs(x): num - Absolute value */
static as_value_t* std_math_abs(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(fabs(x));
}

/* Math:sign(x): num - Sign of x */
static as_value_t* std_math_sign(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    
    if (isnan(x)) return as_value_num(NAN);
    if (x > 0) return as_value_num(1.0);
    if (x < 0) return as_value_num(-1.0);
    return as_value_num(x);  /* Preserve +0 and -0 */
}

/* Math:round(x): num - Round to nearest integer */
static as_value_t* std_math_round(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(round(x));
}

/* Math:ceil(x): num - Round up */
static as_value_t* std_math_ceil(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(ceil(x));
}

/* Math:floor(x): num - Round down */
static as_value_t* std_math_floor(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(floor(x));
}

/* Math:trunc(x): num - Truncate decimal */
static as_value_t* std_math_trunc(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(trunc(x));
}

/* Math:min(a, b): num - Minimum of two values */
static as_value_t* std_math_min(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double a, b;
    if (!get_num_arg(args, arg_count, 0, &a) || !get_num_arg(args, arg_count, 1, &b)) {
        return as_value_num(NAN);
    }
    return as_value_num(fmin(a, b));
}

/* Math:max(a, b): num - Maximum of two values */
static as_value_t* std_math_max(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double a, b;
    if (!get_num_arg(args, arg_count, 0, &a) || !get_num_arg(args, arg_count, 1, &b)) {
        return as_value_num(NAN);
    }
    return as_value_num(fmax(a, b));
}

/* Math:sqrt(x): num - Square root */
static as_value_t* std_math_sqrt(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(sqrt(x));
}

/* Math:cbrt(x): num - Cube root */
static as_value_t* std_math_cbrt(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(cbrt(x));
}

/* Math:hypot(vs): num - Hypotenuse (takes array) */
static as_value_t* std_math_hypot(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_ARR) {
        return as_value_num(NAN);
    }
    
    as_arr_t* arr = args[0]->data.arr_val;
    double sum_of_squares = 0.0;
    
    for (size_t i = 0; i < arr->length; i++) {
        if (arr->items[i]->type == AS_VALUE_NUM) {
            double val = arr->items[i]->data.num_val;
            sum_of_squares += val * val;
        }
    }
    
    return as_value_num(sqrt(sum_of_squares));
}

/* ============================================================================
 * TRIGONOMETRIC FUNCTIONS (all angles in radians)
 * ============================================================================ */

static as_value_t* std_math_sin(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(sin(x));
}

static as_value_t* std_math_cos(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(cos(x));
}

static as_value_t* std_math_tan(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(tan(x));
}

static as_value_t* std_math_asin(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(asin(x));
}

static as_value_t* std_math_acos(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(acos(x));
}

static as_value_t* std_math_atan(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(atan(x));
}

static as_value_t* std_math_atan2(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double y, x;
    if (!get_num_arg(args, arg_count, 0, &y) || !get_num_arg(args, arg_count, 1, &x)) {
        return as_value_num(NAN);
    }
    return as_value_num(atan2(y, x));
}

/* ============================================================================
 * HYPERBOLIC FUNCTIONS
 * ============================================================================ */

#ifndef NO_MATH_HYPERBOLIC

static as_value_t* std_math_sinh(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(sinh(x));
}

static as_value_t* std_math_cosh(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(cosh(x));
}

static as_value_t* std_math_tanh(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(tanh(x));
}

static as_value_t* std_math_asinh(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(asinh(x));
}

static as_value_t* std_math_acosh(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(acosh(x));
}

static as_value_t* std_math_atanh(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(atanh(x));
}

#else /* NO_MATH_HYPERBOLIC */

#define STUB_HYPERBOLIC(name) \
    static as_value_t* std_math_##name(as_value_t** args, size_t arg_count, void* userdata) { \
        (void)args; (void)arg_count; (void)userdata; \
        fprintf(stderr, "Math:" #name " is not available (NO_MATH_HYPERBOLIC)\n"); \
        return as_value_num(NAN); \
    }

STUB_HYPERBOLIC(sinh)
STUB_HYPERBOLIC(cosh)
STUB_HYPERBOLIC(tanh)
STUB_HYPERBOLIC(asinh)
STUB_HYPERBOLIC(acosh)
STUB_HYPERBOLIC(atanh)

#endif /* NO_MATH_HYPERBOLIC */

/* ============================================================================
 * EXPONENTIAL / LOGARITHMIC FUNCTIONS
 * ============================================================================ */

static as_value_t* std_math_pow(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x, y;
    if (!get_num_arg(args, arg_count, 0, &x) || !get_num_arg(args, arg_count, 1, &y)) {
        return as_value_num(NAN);
    }
    return as_value_num(pow(x, y));
}

static as_value_t* std_math_exp(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(exp(x));
}

static as_value_t* std_math_expm1(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(expm1(x));
}

static as_value_t* std_math_log(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(log(x));
}

static as_value_t* std_math_log1p(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(log1p(x));
}

static as_value_t* std_math_log10(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(log10(x));
}

static as_value_t* std_math_log2(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    return as_value_num(log2(x));
}

/* ============================================================================
 * RANDOM GENERATORS
 * ============================================================================ */

#ifndef NO_MATH_RANDOM

/* Initialize random seed (called once) */
static bool rng_initialized = false;
static void init_rng(void) {
    if (!rng_initialized) {
        srand((unsigned int)time(NULL));
        rng_initialized = true;
    }
}

/* Math:rnd(min?, max?): num - Random number */
static as_value_t* std_math_rnd(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    init_rng();
    
    if (arg_count >= 2) {
        /* Integer range: min <= x <= max */
        double min_d, max_d;
        if (!get_num_arg(args, arg_count, 0, &min_d) || !get_num_arg(args, arg_count, 1, &max_d)) {
            return as_value_num(NAN);
        }
        
        int min = (int)min_d;
        int max = (int)max_d;
        
        if (min > max) {
            int tmp = min;
            min = max;
            max = tmp;
        }
        
        int range = max - min + 1;
        int result = min + (rand() % range);
        return as_value_num((double)result);
    } else {
        /* Float range: 0 <= x < 1 */
        double result = (double)rand() / (double)RAND_MAX;
        return as_value_num(result);
    }
}

#else /* NO_MATH_RANDOM */

static as_value_t* std_math_rnd(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count; (void)userdata;
    fprintf(stderr, "Math:rnd is not available (NO_MATH_RANDOM)\n");
    return as_value_num(0.0);
}

#endif /* NO_MATH_RANDOM */

/* ============================================================================
 * MISC FUNCTIONS
 * ============================================================================ */

#ifndef NO_MATH_MISC

/* Math:clz32(x): num - Count leading zeros in 32-bit representation */
static as_value_t* std_math_clz32(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(32);
    
    uint32_t n = (uint32_t)(int32_t)x;
    if (n == 0) return as_value_num(32);
    
#ifdef __GNUC__
    /* Use GCC/Clang builtin */
    return as_value_num(__builtin_clz(n));
#else
    /* Manual implementation */
    int count = 0;
    uint32_t mask = 0x80000000;
    while ((n & mask) == 0 && count < 32) {
        count++;
        mask >>= 1;
    }
    return as_value_num(count);
#endif
}

/* Math:fround(x): num - Round to 32-bit float */
static as_value_t* std_math_fround(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x;
    if (!get_num_arg(args, arg_count, 0, &x)) return as_value_num(NAN);
    
    float f = (float)x;
    return as_value_num((double)f);
}

/* Math:imul(x, y): num - 32-bit integer multiplication */
static as_value_t* std_math_imul(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    double x, y;
    if (!get_num_arg(args, arg_count, 0, &x) || !get_num_arg(args, arg_count, 1, &y)) {
        return as_value_num(0);
    }
    
    int32_t a = (int32_t)x;
    int32_t b = (int32_t)y;
    int32_t result = a * b;
    
    return as_value_num((double)result);
}

#else /* NO_MATH_MISC */

#define STUB_MISC(name) \
    static as_value_t* std_math_##name(as_value_t** args, size_t arg_count, void* userdata) { \
        (void)args; (void)arg_count; (void)userdata; \
        fprintf(stderr, "Math:" #name " is not available (NO_MATH_MISC)\n"); \
        return as_value_num(NAN); \
    }

STUB_MISC(clz32)
STUB_MISC(fround)
STUB_MISC(imul)

#endif /* NO_MATH_MISC */

/* ============================================================================
 * REGISTRATION
 * ============================================================================ */

void as_std_math_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* val;
    
    /* Constants */
    val = as_value_num(INFINITY);
    as_scope_add(global, "Math:Infinity", val, false);
    as_value_release(val);
    
    val = as_value_num(M_E);
    as_scope_add(global, "Math:E", val, false);
    as_value_release(val);
    
    val = as_value_num(M_LN2);
    as_scope_add(global, "Math:LN2", val, false);
    as_value_release(val);
    
    val = as_value_num(M_LN10);
    as_scope_add(global, "Math:LN10", val, false);
    as_value_release(val);
    
    val = as_value_num(M_LOG2E);
    as_scope_add(global, "Math:LOG2E", val, false);
    as_value_release(val);
    
    val = as_value_num(M_LOG10E);
    as_scope_add(global, "Math:LOG10E", val, false);
    as_value_release(val);
    
    val = as_value_num(M_PI);
    as_scope_add(global, "Math:PI", val, false);
    as_value_release(val);
    
    val = as_value_num(M_SQRT1_2);
    as_scope_add(global, "Math:SQRT1_2", val, false);
    as_value_release(val);
    
    val = as_value_num(M_SQRT2);
    as_scope_add(global, "Math:SQRT2", val, false);
    as_value_release(val);
    
    /* Basic functions */
    val = as_value_fn_native(std_math_abs, NULL);
    as_scope_add(global, "Math:abs", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_sign, NULL);
    as_scope_add(global, "Math:sign", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_round, NULL);
    as_scope_add(global, "Math:round", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_ceil, NULL);
    as_scope_add(global, "Math:ceil", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_floor, NULL);
    as_scope_add(global, "Math:floor", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_trunc, NULL);
    as_scope_add(global, "Math:trunc", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_min, NULL);
    as_scope_add(global, "Math:min", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_max, NULL);
    as_scope_add(global, "Math:max", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_sqrt, NULL);
    as_scope_add(global, "Math:sqrt", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_cbrt, NULL);
    as_scope_add(global, "Math:cbrt", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_hypot, NULL);
    as_scope_add(global, "Math:hypot", val, false);
    as_value_release(val);
    
    /* Trigonometric functions */
    val = as_value_fn_native(std_math_sin, NULL);
    as_scope_add(global, "Math:sin", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_cos, NULL);
    as_scope_add(global, "Math:cos", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_tan, NULL);
    as_scope_add(global, "Math:tan", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_asin, NULL);
    as_scope_add(global, "Math:asin", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_acos, NULL);
    as_scope_add(global, "Math:acos", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_atan, NULL);
    as_scope_add(global, "Math:atan", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_atan2, NULL);
    as_scope_add(global, "Math:atan2", val, false);
    as_value_release(val);
    
    /* Hyperbolic functions */
    val = as_value_fn_native(std_math_sinh, NULL);
    as_scope_add(global, "Math:sinh", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_cosh, NULL);
    as_scope_add(global, "Math:cosh", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_tanh, NULL);
    as_scope_add(global, "Math:tanh", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_asinh, NULL);
    as_scope_add(global, "Math:asinh", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_acosh, NULL);
    as_scope_add(global, "Math:acosh", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_atanh, NULL);
    as_scope_add(global, "Math:atanh", val, false);
    as_value_release(val);
    
    /* Exponential/Logarithmic functions */
    val = as_value_fn_native(std_math_pow, NULL);
    as_scope_add(global, "Math:pow", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_exp, NULL);
    as_scope_add(global, "Math:exp", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_expm1, NULL);
    as_scope_add(global, "Math:expm1", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_log, NULL);
    as_scope_add(global, "Math:log", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_log1p, NULL);
    as_scope_add(global, "Math:log1p", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_log10, NULL);
    as_scope_add(global, "Math:log10", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_log2, NULL);
    as_scope_add(global, "Math:log2", val, false);
    as_value_release(val);
    
    /* Random generators */
    val = as_value_fn_native(std_math_rnd, NULL);
    as_scope_add(global, "Math:rnd", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(as_std_gen_rng, NULL);
    as_scope_add(global, "Math:gen_rng", val, false);
    as_value_release(val);
    
    /* Misc functions */
    val = as_value_fn_native(std_math_clz32, NULL);
    as_scope_add(global, "Math:clz32", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_fround, NULL);
    as_scope_add(global, "Math:fround", val, false);
    as_value_release(val);
    
    val = as_value_fn_native(std_math_imul, NULL);
    as_scope_add(global, "Math:imul", val, false);
    as_value_release(val);
}

bool as_std_math_is_available(void) {
    return true;
}

#else /* NO_MATH */

/* Stub when Math namespace is disabled */
void as_std_math_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Math namespace is not available in this build (NO_MATH)\n");
}

bool as_std_math_is_available(void) {
    return false;
}

#endif /* NO_MATH */

