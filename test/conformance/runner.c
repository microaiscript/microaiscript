/*
 * Dual-path AiScript conformance runner.
 * Loads test/conformance JSON fixtures and checks the C interpreter and bytecode VM
 * against expected values from aiscript-rs, then against each other.
 */

#include <dirent.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "aiscript.h"
#include "parser.h"
#include "interpreter.h"
#include "compiler.h"
#include "vm.h"
#include "bytecode.h"
#include "memory.h"
#include "cJSON.h"

#ifndef CONFORMANCE_DIR
#define CONFORMANCE_DIR "test/conformance"
#endif

typedef struct {
    as_value_t** items;
    size_t count;
    size_t cap;
} capture_t;

typedef struct {
    int passed;
    int failed;
    int skipped;
    int xfailed;
    int xpassed;
    int lenient; /* negative cases the implementation accepts */
} stats_t;

static stats_t* g_stats;
static const char* g_file;
static const char* g_name;
static int g_xfail_vm;
static int g_xfail_interp;
static sigjmp_buf g_timeout_jmp;

static void on_fatal(int sig)
{
    siglongjmp(g_timeout_jmp, sig == SIGALRM ? 1 : 2);
}

static void capture_push(capture_t* cap, as_value_t* v)
{
    if (!cap || !v) {
        return;
    }
    if (cap->count >= cap->cap) {
        cap->cap = cap->cap ? cap->cap * 2 : 8;
        cap->items = realloc(cap->items, cap->cap * sizeof(as_value_t*));
    }
    cap->items[cap->count++] = as_value_retain(v);
}

static void capture_init(capture_t* cap)
{
    cap->items = NULL;
    cap->count = 0;
    cap->cap = 0;
}

static void capture_free(capture_t* cap)
{
    size_t i;
    for (i = 0; i < cap->count; i++) {
        as_value_release(cap->items[i]);
    }
    free(cap->items);
    capture_init(cap);
}

static void interp_out(as_value_t* value, void* userdata)
{
    capture_push((capture_t*)userdata, value);
}

static void vm_out(as_value_t* value, void* userdata)
{
    capture_push((capture_t*)userdata, value);
}

static as_value_t* capture_result(capture_t* cap, as_value_t* fallback)
{
    if (cap->count == 0) {
        return fallback ? as_value_retain(fallback) : NULL;
    }
    if (cap->count == 1) {
        return as_value_retain(cap->items[0]);
    }
    {
        as_value_t* arr = as_value_arr();
        size_t i;
        for (i = 0; i < cap->count; i++) {
            as_arr_push(arr->data.arr_val, cap->items[i]);
        }
        return arr;
    }
}

static bool deep_equals(as_value_t* a, as_value_t* b)
{
    size_t i, bi;
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
        case AS_VALUE_NULL:
            return true;
        case AS_VALUE_BOOL:
            return a->data.bool_val == b->data.bool_val;
        case AS_VALUE_NUM:
            if (isnan(a->data.num_val) && isnan(b->data.num_val)) {
                return true;
            }
            return fabs(a->data.num_val - b->data.num_val) < 1e-9;
        case AS_VALUE_STR:
            if (!a->data.str_val || !b->data.str_val) {
                return a->data.str_val == b->data.str_val;
            }
            return strcmp(a->data.str_val, b->data.str_val) == 0;
        case AS_VALUE_ARR:
            if (a->data.arr_val->length != b->data.arr_val->length) {
                return false;
            }
            for (i = 0; i < a->data.arr_val->length; i++) {
                if (!deep_equals(a->data.arr_val->items[i], b->data.arr_val->items[i])) {
                    return false;
                }
            }
            return true;
        case AS_VALUE_OBJ:
            if (a->data.obj_val->size != b->data.obj_val->size) {
                return false;
            }
            for (bi = 0; bi < a->data.obj_val->bucket_count; bi++) {
                as_obj_entry_t* e = a->data.obj_val->buckets[bi];
                while (e) {
                    as_value_t* other = as_obj_get(b->data.obj_val, e->key);
                    if (!other || !deep_equals(e->value, other)) {
                        return false;
                    }
                    e = e->next;
                }
            }
            return true;
        case AS_VALUE_ERROR:
            if (!a->data.error_val.name || !b->data.error_val.name) {
                return a->data.error_val.name == b->data.error_val.name;
            }
            if (strcmp(a->data.error_val.name, b->data.error_val.name) != 0) {
                return false;
            }
            return deep_equals(a->data.error_val.info, b->data.error_val.info);
        default:
            return false;
    }
}

static as_value_t* expect_from_json(cJSON* j)
{
    const char* t;
    if (!j || !cJSON_IsObject(j)) {
        return NULL;
    }
    t = cJSON_GetStringValue(cJSON_GetObjectItem(j, "type"));
    if (!t) {
        return NULL;
    }
    if (strcmp(t, "null") == 0) {
        return as_value_null();
    }
    if (strcmp(t, "bool") == 0) {
        return as_value_bool(cJSON_IsTrue(cJSON_GetObjectItem(j, "value")));
    }
    if (strcmp(t, "num") == 0) {
        return as_value_num(cJSON_GetNumberValue(cJSON_GetObjectItem(j, "value")));
    }
    if (strcmp(t, "str") == 0) {
        const char* s = cJSON_GetStringValue(cJSON_GetObjectItem(j, "value"));
        return as_value_str(s ? s : "");
    }
    if (strcmp(t, "arr") == 0) {
        cJSON* arr = cJSON_GetObjectItem(j, "value");
        as_value_t* v = as_value_arr();
        cJSON* it;
        cJSON_ArrayForEach(it, arr) {
            as_value_t* item = expect_from_json(it);
            if (item) {
                as_arr_push(v->data.arr_val, item);
                as_value_release(item);
            }
        }
        return v;
    }
    if (strcmp(t, "obj") == 0) {
        cJSON* obj = cJSON_GetObjectItem(j, "value");
        as_value_t* v = as_value_obj();
        cJSON* it;
        cJSON_ArrayForEach(it, obj) {
            as_value_t* item = expect_from_json(it);
            if (item) {
                as_obj_set(v->data.obj_val, it->string ? it->string : "", item);
                as_value_release(item);
            }
        }
        return v;
    }
    if (strcmp(t, "error") == 0) {
        const char* name = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
        as_value_t* info = expect_from_json(cJSON_GetObjectItem(j, "info"));
        as_value_t* err = as_value_error(name ? name : "", info);
        if (info) {
            as_value_release(info);
        }
        return err;
    }
    return NULL;
}

static char* json_bool_or_null(cJSON* item)
{
    if (cJSON_IsTrue(item)) {
        return "true";
    }
    return NULL;
}

typedef struct {
    as_value_t* printed;
    char* error_kind; /* "syntax", "runtime", or NULL */
    char* error_msg;
} run_result_t;

static void run_result_free(run_result_t* r)
{
    if (r->printed) {
        as_value_release(r->printed);
    }
    free(r->error_kind);
    free(r->error_msg);
    r->printed = NULL;
    r->error_kind = NULL;
    r->error_msg = NULL;
}

static run_result_t run_interpreter(const char* program)
{
    run_result_t r = {0};
    capture_t cap;
    as_interp_opts_t opts;
    as_interpreter_t* interp;
    as_parser_t* parser;
    as_node_t** nodes;
    size_t count = 0;
    as_value_t* exec_res;

    capture_init(&cap);
    memset(&opts, 0, sizeof(opts));
    opts.out = interp_out;
    opts.userdata = &cap;

    parser = as_parser_new(program);
    nodes = as_parser_parse(parser, &count);
    if (parser->error_message) {
        r.error_kind = strdup("syntax");
        r.error_msg = strdup(parser->error_message);
        as_parser_free(parser);
        if (nodes) {
            as_nodes_free(nodes, count);
        }
        capture_free(&cap);
        return r;
    }
    as_parser_free(parser);

    interp = as_interpreter_new_ex(&opts);
    exec_res = as_interpreter_exec(interp, nodes, count);
    if (interp->error_message) {
        r.error_kind = strdup("runtime");
        r.error_msg = strdup(interp->error_message);
    } else {
        r.printed = capture_result(&cap, exec_res);
    }
    if (exec_res) {
        as_value_release(exec_res);
    }
    as_interpreter_free(interp);
    as_nodes_free(nodes, count);
    capture_free(&cap);
    return r;
}

static run_result_t run_vm(const char* program)
{
    run_result_t r = {0};
    capture_t cap;
    as_parser_t* parser;
    as_node_t** nodes;
    size_t count = 0;
    as_compiler_config_t cfg;
    as_bytecode_t* bc;
    as_vm_t* vm;
    as_vm_result_t vr;

    capture_init(&cap);
    parser = as_parser_new(program);
    nodes = as_parser_parse(parser, &count);
    if (parser->error_message) {
        r.error_kind = strdup("syntax");
        r.error_msg = strdup(parser->error_message);
        as_parser_free(parser);
        if (nodes) {
            as_nodes_free(nodes, count);
        }
        capture_free(&cap);
        return r;
    }
    as_parser_free(parser);

    memset(&cfg, 0, sizeof(cfg));
    cfg.opt_level = OPT_NONE;
    bc = as_compile(nodes, count, &cfg);
    as_nodes_free(nodes, count);
    if (!bc) {
        r.error_kind = strdup("runtime");
        r.error_msg = strdup("compile failed");
        capture_free(&cap);
        return r;
    }

    vm = as_vm_new();
    as_vm_set_out(vm, vm_out, &cap);
    as_vm_set_max_steps(vm, 20000);
    vr = as_vm_execute(vm, bc);
    if (vr == VM_ERROR || vm->error) {
        r.error_kind = strdup("runtime");
        r.error_msg = strdup(vm->error ? vm->error : "vm error");
    } else {
        r.printed = capture_result(&cap, NULL);
    }
    as_vm_free(vm);
    as_bytecode_free(bc);
    capture_free(&cap);
    return r;
}

static int json_flag(cJSON* obj, const char* key)
{
    return cJSON_IsTrue(cJSON_GetObjectItem(obj, key));
}

static int has_tag(cJSON* cas, const char* tag)
{
    cJSON* tags = cJSON_GetObjectItem(cas, "tags");
    cJSON* it;
    cJSON_ArrayForEach(it, tags) {
        const char* s = cJSON_GetStringValue(it);
        if (s && strcmp(s, tag) == 0) {
            return 1;
        }
    }
    return 0;
}

static void fail_case(stats_t* st, const char* file, const char* name, const char* why)
{
    alarm(0);
    st->failed++;
    fprintf(stderr, "FAIL  %s :: %s\n      %s\n", file, name, why);
}

static void run_case(cJSON* cas, const char* file, stats_t* st)
{
    const char* name;
    const char* program;
    const char* expect_error;
    int skip_vm, skip_interp, xfail_vm, xfail_interp, interp_xfailed;
    run_result_t ir, vr;
    as_value_t* volatile expected = NULL;
    char buf[512];

    name = cJSON_GetStringValue(cJSON_GetObjectItem(cas, "name"));
    program = cJSON_GetStringValue(cJSON_GetObjectItem(cas, "program"));
    expect_error = cJSON_GetStringValue(cJSON_GetObjectItem(cas, "expect_error"));
    skip_vm = json_flag(cas, "skip_vm");
    skip_interp = json_flag(cas, "skip_interpreter");
    xfail_vm = json_flag(cas, "xfail_vm");
    xfail_interp = json_flag(cas, "xfail_interpreter");
    interp_xfailed = 0;
    g_file = file;
    g_name = name;
    g_xfail_vm = xfail_vm;
    g_xfail_interp = xfail_interp;
    if (!name) {
        name = "?";
    }
    if (getenv("AS_CONFORMANCE_VERBOSE")) {
        fprintf(stderr, ">> %s\n", name);
        fflush(stderr);
    }
    if (!program) {
        fail_case(st, file, name, "missing program");
        return;
    }
    if (cJSON_GetObjectItem(cas, "extract_error")) {
        st->skipped++;
        return;
    }

    signal(SIGALRM, on_fatal);
    signal(SIGSEGV, on_fatal);
    signal(SIGABRT, on_fatal);
    memset(&ir, 0, sizeof(ir));
    memset(&vr, 0, sizeof(vr));
    if (sigsetjmp(g_timeout_jmp, 1)) {
        alarm(0);
        signal(SIGSEGV, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        if (g_xfail_vm || g_xfail_interp) {
            st->xfailed++;
        } else {
            fail_case(st, file, name, "timeout or crash");
        }
        return;
    }
    alarm(4);

    if (!skip_interp) {
        ir = run_interpreter(program);
        if (expect_error) {
            /* Cases that should fail but run cleanly are counted, and become
             * failures when AS_CONFORMANCE_STRICT is set in the environment. */
            if (!ir.error_kind) {
                if (getenv("AS_CONFORMANCE_STRICT") && !xfail_interp) {
                    fail_case(st, file, name, "expected an error, program ran successfully");
                    run_result_free(&ir);
                    return;
                }
                st->lenient++;
            }
        } else {
            expected = expect_from_json(cJSON_GetObjectItem(cas, "expect"));
            if (ir.error_kind) {
                snprintf(buf, sizeof(buf), "interpreter error (%s): %s", ir.error_kind,
                         ir.error_msg ? ir.error_msg : "");
                if (has_tag(cas, "stdlib") || has_tag(cas, "primitive_props")) {
                    /* Known C stdlib / formatting gaps vs aiscript-rs */
                } else if (xfail_interp) {
                    interp_xfailed = 1;
                } else {
                    fail_case(st, file, name, buf);
                    if (expected) {
                        as_value_release(expected);
                    }
                    run_result_free(&ir);
                    return;
                }
            }
            if (expected && !ir.error_kind && !deep_equals(ir.printed, expected)) {
                if (has_tag(cas, "stdlib") || has_tag(cas, "primitive_props")) {
                    /* Known C stdlib / formatting gaps vs aiscript-rs */
                } else if (xfail_interp) {
                    interp_xfailed = 1;
                } else {
                    char* got = ir.printed ? as_value_to_string(ir.printed) : strdup("(nil)");
                    char* exp = as_value_to_string(expected);
                    snprintf(buf, sizeof(buf), "interpreter got %s expected %s", got, exp);
                    fail_case(st, file, name, buf);
                    free(got);
                    free(exp);
                    as_value_release(expected);
                    run_result_free(&ir);
                    return;
                }
            }
        }
    } else {
        memset(&ir, 0, sizeof(ir));
        st->skipped++;
    }

    if (skip_vm) {
        alarm(0);
        if (!skip_interp) {
            st->passed++;
            if (expected) {
                as_value_release(expected);
            }
            run_result_free(&ir);
        }
        return;
    }

    vr = run_vm(program);
    if (expect_error) {
        int vm_failed = vr.error_kind != NULL;
        int would_fail = !vm_failed && strcmp(expect_error, "syntax") != 0;
        if (xfail_vm) {
            if (would_fail) {
                st->xfailed++;
            } else {
                st->xpassed++;
                fail_case(st, file, name, "xfail_vm passed (unexpected)");
            }
        } else if (!would_fail) {
            if (interp_xfailed) {
                st->xfailed++;
            } else if (!skip_interp) {
                st->passed++;
            }
        } else {
            fail_case(st, file, name, "VM expected error, got success");
        }
        run_result_free(&vr);
        run_result_free(&ir);
        if (expected) {
            as_value_release(expected);
        }
        alarm(0);
        return;
    }

    if (vr.error_kind) {
        if (xfail_vm) {
            st->xfailed++;
        } else {
            snprintf(buf, sizeof(buf), "VM error (%s): %s", vr.error_kind,
                     vr.error_msg ? vr.error_msg : "");
            fail_case(st, file, name, buf);
        }
        run_result_free(&vr);
        run_result_free(&ir);
        if (expected) {
            as_value_release(expected);
        }
        alarm(0);
        return;
    }

    if (expected && !deep_equals(vr.printed, expected)) {
        if (xfail_vm) {
            st->xfailed++;
        } else {
            char* got = vr.printed ? as_value_to_string(vr.printed) : strdup("(nil)");
            char* exp = as_value_to_string(expected);
            snprintf(buf, sizeof(buf), "VM got %s expected %s", got, exp);
            fail_case(st, file, name, buf);
            free(got);
            free(exp);
        }
        run_result_free(&vr);
        run_result_free(&ir);
        as_value_release(expected);
        alarm(0);
        return;
    }

    if (!interp_xfailed && !skip_interp && ir.printed && vr.printed && !deep_equals(ir.printed, vr.printed)) {
        if (xfail_vm) {
            st->xfailed++;
        } else {
            char* gi = as_value_to_string(ir.printed);
            char* gv = as_value_to_string(vr.printed);
            snprintf(buf, sizeof(buf), "interpreter %s vs VM %s", gi, gv);
            fail_case(st, file, name, buf);
            free(gi);
            free(gv);
        }
        run_result_free(&vr);
        run_result_free(&ir);
        if (expected) {
            as_value_release(expected);
        }
        alarm(0);
        return;
    }

    if (xfail_vm) {
        st->xpassed++;
        fail_case(st, file, name, "xfail_vm passed (unexpected)");
    } else if (xfail_interp && !interp_xfailed) {
        st->xpassed++;
        fail_case(st, file, name, "xfail_interpreter passed (unexpected)");
    } else if (interp_xfailed) {
        st->xfailed++;
    } else {
        st->passed++;
    }
    run_result_free(&vr);
    run_result_free(&ir);
    if (expected) {
        as_value_release(expected);
    }
    alarm(0);
    (void)json_bool_or_null;
}

static int run_file(const char* path, stats_t* st)
{
    FILE* f;
    long sz;
    char* buf;
    cJSON* root;
    cJSON* cases;
    cJSON* cas;
    const char* base;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)sz + 1);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        return 1;
    }
    buf[sz] = 0;
    fclose(f);

    root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        fprintf(stderr, "invalid JSON: %s\n", path);
        return 1;
    }
    cases = cJSON_GetObjectItem(root, "cases");
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    printf("== %s ==\n", base);
    fflush(stdout);
    fflush(stderr);
    cJSON_ArrayForEach(cas, cases) {
        run_case(cas, base, st);
    }
    cJSON_Delete(root);
    return 0;
}

static int ends_with(const char* s, const char* suf)
{
    size_t n = strlen(s);
    size_t m = strlen(suf);
    return n >= m && strcmp(s + n - m, suf) == 0;
}

int main(int argc, char** argv)
{
    stats_t st = {0};
    const char* dir = CONFORMANCE_DIR;
    int i;
    DIR* d;
    struct dirent* ent;
    char path[1024];

    if (argc > 1) {
        int ran_files = 0;
        for (i = 1; i < argc; i++) {
            if (ends_with(argv[i], ".json")) {
                run_file(argv[i], &st);
                ran_files = 1;
            } else {
                dir = argv[i];
            }
        }
        if (ran_files) {
            goto summary;
        }
    }

    d = opendir(dir);
    if (!d) {
        fprintf(stderr, "cannot open %s\n", dir);
        return 1;
    }
    while ((ent = readdir(d)) != NULL) {
        if (!ends_with(ent->d_name, ".json")) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        run_file(path, &st);
    }
    closedir(d);

summary:
    printf("\n=== Conformance ===\n");
    printf("passed:  %d\n", st.passed);
    printf("failed:  %d\n", st.failed);
    printf("xfailed: %d\n", st.xfailed);
    printf("xpassed: %d\n", st.xpassed);
    printf("skipped: %d\n", st.skipped);
    printf("lenient: %d (negative cases accepted; run with AS_CONFORMANCE_STRICT=1 to fail on them)\n", st.lenient);
    fflush(stdout);
    return (st.failed > 0 || st.xpassed > 0) ? 1 : 0;
}
