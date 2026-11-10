/*
 * AiScript Standard Library - Async Namespace Implementation
 *
 * Provides Async:interval and Async:timeout using POSIX threads.
 *
 * Callbacks are not called directly from the background thread; instead they
 * are queued and dispatched on the main thread at safe yield points
 * (as_std_async_dispatch).  This avoids races with the interpreter state.
 */

#include "std_async.h"
#include "primitive_props.h"
#include "memory.h"
#include "scope.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
    #include <pthread.h>
#endif

/* --- Pending callback queue -------------------------------------------- */

typedef struct async_pending {
    as_value_t* callback;
    struct async_pending* next;
} async_pending_t;

#ifdef _WIN32
    /* Windows: use CRITICAL_SECTION */
    static CRITICAL_SECTION pending_cs;
    static int cs_initialized = 0;
    static void pending_lock(void)   { if (cs_initialized) EnterCriticalSection(&pending_cs); }
    static void pending_unlock(void) { if (cs_initialized) LeaveCriticalSection(&pending_cs); }
    static void pending_init(void)   { if (!cs_initialized) { InitializeCriticalSection(&pending_cs); cs_initialized = 1; } }
#else
    static pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;
    static void pending_lock(void)   { pthread_mutex_lock(&pending_mutex); }
    static void pending_unlock(void) { pthread_mutex_unlock(&pending_mutex); }
    static void pending_init(void)   { (void)0; }
#endif

static async_pending_t* pending_head = NULL;
static async_pending_t* pending_tail = NULL;

static void enqueue_callback(as_value_t* cb) {
    async_pending_t* node = malloc(sizeof(async_pending_t));
    if (!node) return;
    node->callback = as_value_retain(cb);
    node->next = NULL;
    pending_lock();
    if (pending_tail) pending_tail->next = node;
    else              pending_head = node;
    pending_tail = node;
    pending_unlock();
}

void as_std_async_dispatch(as_interpreter_t* interp) {
    /* Drain the queue without holding the lock during execution */
    pending_lock();
    async_pending_t* list = pending_head;
    pending_head = NULL;
    pending_tail = NULL;
    pending_unlock();

    while (list) {
        async_pending_t* next = list->next;
        as_call_user_fn(interp, list->callback, NULL, 0);
        as_value_release(list->callback);
        free(list);
        list = next;
    }
}

/* --- Timer thread context ---------------------------------------------- */

typedef struct {
    as_value_t* callback;
    long        interval_ms;
    long        delay_ms;
    int         immediate;
    volatile int cancelled;
} async_ctx_t;

static void ms_sleep(long ms) {
    if (ms <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* --- interval thread --------------------------------------------------- */

#ifdef _WIN32
static DWORD WINAPI interval_thread(LPVOID arg) {
#else
static void* interval_thread(void* arg) {
#endif
    async_ctx_t* ctx = (async_ctx_t*)arg;

    if (!ctx->immediate) {
        ms_sleep(ctx->interval_ms);
    }

    while (!ctx->cancelled) {
        enqueue_callback(ctx->callback);
        ms_sleep(ctx->interval_ms);
    }

    as_value_release(ctx->callback);
    free(ctx);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* --- timeout thread ---------------------------------------------------- */

#ifdef _WIN32
static DWORD WINAPI timeout_thread(LPVOID arg) {
#else
static void* timeout_thread(void* arg) {
#endif
    async_ctx_t* ctx = (async_ctx_t*)arg;

    ms_sleep(ctx->delay_ms);

    if (!ctx->cancelled) {
        enqueue_callback(ctx->callback);
    }

    as_value_release(ctx->callback);
    free(ctx);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* --- Cancellation function --------------------------------------------- */

typedef struct {
    async_ctx_t* ctx;
} cancel_userdata_t;

static as_value_t* cancel_fn(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count;
    cancel_userdata_t* cu = (cancel_userdata_t*)userdata;
    if (cu && cu->ctx) {
        cu->ctx->cancelled = 1;
        cu->ctx = NULL;
    }
    free(cu);
    return as_value_null();
}

static void spawn_detached(
#ifdef _WIN32
    LPTHREAD_START_ROUTINE fn,
#else
    void* (*fn)(void*),
#endif
    async_ctx_t* ctx)
{
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, fn, ctx, 0, NULL);
    if (h) CloseHandle(h);
#else
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, fn, ctx);
    pthread_attr_destroy(&attr);
#endif
}

/* --- Async:interval ---------------------------------------------------- */

static as_value_t* std_async_interval(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_FN)
        return as_value_null();

    long interval_ms = (long)args[0]->data.num_val;
    if (interval_ms < 0) interval_ms = 0;

    int immediate = 0;
    if (arg_count >= 3 && args[2]->type == AS_VALUE_BOOL)
        immediate = args[2]->data.bool_val ? 1 : 0;

    async_ctx_t* ctx = malloc(sizeof(async_ctx_t));
    if (!ctx) return as_value_null();
    ctx->callback    = as_value_retain(args[1]);
    ctx->interval_ms = interval_ms;
    ctx->delay_ms    = interval_ms;
    ctx->immediate   = immediate;
    ctx->cancelled   = 0;

    spawn_detached(interval_thread, ctx);

    cancel_userdata_t* cu = malloc(sizeof(cancel_userdata_t));
    if (!cu) { ctx->cancelled = 1; return as_value_null(); }
    cu->ctx = ctx;
    return as_value_fn_native(cancel_fn, cu);
}

/* --- Async:timeout ----------------------------------------------------- */

static as_value_t* std_async_timeout(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_FN)
        return as_value_null();

    long delay_ms = (long)args[0]->data.num_val;
    if (delay_ms < 0) delay_ms = 0;

    async_ctx_t* ctx = malloc(sizeof(async_ctx_t));
    if (!ctx) return as_value_null();
    ctx->callback    = as_value_retain(args[1]);
    ctx->delay_ms    = delay_ms;
    ctx->interval_ms = 0;
    ctx->immediate   = 0;
    ctx->cancelled   = 0;

    spawn_detached(timeout_thread, ctx);

    cancel_userdata_t* cu = malloc(sizeof(cancel_userdata_t));
    if (!cu) { ctx->cancelled = 1; return as_value_null(); }
    cu->ctx = ctx;
    return as_value_fn_native(cancel_fn, cu);
}

/* --- Registration ------------------------------------------------------ */

void as_std_async_register(as_interpreter_t* interp) {
    pending_init();

    as_value_t* fn;

    fn = as_value_fn_native(std_async_interval, NULL);
    as_scope_add(interp->global_scope, "Async:interval", fn, false);
    as_value_release(fn);

    fn = as_value_fn_native(std_async_timeout, NULL);
    as_scope_add(interp->global_scope, "Async:timeout", fn, false);
    as_value_release(fn);
}
