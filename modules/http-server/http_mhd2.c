/*
 * Native HTTP server for AiScript C (libmicrohttpd2, external periodic mode).
 * Build: see Makefile in this directory.
 *
 * Model: single interpreter thread. Call HttpServer:poll often so MHD runs on that
 * thread and invokes your handler (as_call_user_fn). Interleave poll with your logic.
 *
 * Async:interval / Async:timeout (std_async.c) queue callbacks from timer threads;
 * they run on the main thread when as_std_async_dispatch runs (Core:sleep, between
 * top-level statements). Do not rely on Async alone to drive poll if MHD would block
 * the main thread during dispatch; keep poll in a loop you control unless you know
 * your MHD build returns quickly when idle.
 */

#define _GNU_SOURCE
#include <aiscript.h>
#include <primitive_props.h>
#include <microhttpd2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SERVERS 16

typedef struct {
    int id;
    int used;
    struct MHD_Daemon *daemon;
    as_interpreter_t *interp;
    as_value_t *handler;
} http_server_slot_t;

static http_server_slot_t g_servers[MAX_SERVERS];
static int g_next_id = 1;

static http_server_slot_t *find_slot_by_id(int id) {
    size_t i;
    for (i = 0; i < MAX_SERVERS; i++) {
        if (g_servers[i].used && g_servers[i].id == id)
            return &g_servers[i];
    }
    return NULL;
}

static struct MHD_Response *build_error_response(as_value_t *err_val) {
    char msg[512];
    msg[0] = '\0';
    if (err_val && err_val->type == AS_VALUE_ERROR) {
        char *info_str = NULL;
        if (err_val->data.error_val.info)
            info_str = as_value_to_string(err_val->data.error_val.info);
        snprintf(msg, sizeof(msg), "%s", info_str ? info_str : err_val->data.error_val.name);
        free(info_str);
    } else {
        snprintf(msg, sizeof(msg), "handler error");
    }
    return MHD_response_from_buffer_copy(MHD_HTTP_STATUS_INTERNAL_SERVER_ERROR, strlen(msg), msg);
}

static struct MHD_Response *build_from_handler_result(as_value_t *result) {
    if (!result)
        return MHD_response_from_buffer_copy(MHD_HTTP_STATUS_INTERNAL_SERVER_ERROR, 0, "");

    if (result->type == AS_VALUE_ERROR) {
        struct MHD_Response *r = build_error_response(result);
        as_value_release(result);
        return r;
    }

    if (result->type != AS_VALUE_OBJ) {
        as_value_release(result);
        return MHD_response_from_buffer_copy(MHD_HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                             strlen("invalid handler return"),
                                             "invalid handler return");
    }

    as_obj_t *ro = result->data.obj_val;
    long status = 200;
    as_value_t *vs = as_obj_get(ro, "status");
    if (vs && vs->type == AS_VALUE_NUM)
        status = (long)vs->data.num_val;

    const char *body = "";
    size_t body_len = 0;
    as_value_t *vb = as_obj_get(ro, "body");
    if (vb && vb->type == AS_VALUE_STR) {
        body = vb->data.str_val;
        body_len = strlen(body);
    }

    struct MHD_Response *resp =
        MHD_response_from_buffer_copy((enum MHD_HTTP_StatusCode)status, body_len, body);
    if (!resp) {
        as_value_release(result);
        return NULL;
    }

    as_value_t *vh = as_obj_get(ro, "headers");
    if (vh && vh->type == AS_VALUE_OBJ) {
        as_obj_t *ho = vh->data.obj_val;
        size_t bi;
        for (bi = 0; bi < ho->bucket_count; bi++) {
            as_obj_entry_t *e = ho->buckets[bi];
            while (e) {
                if (e->key && e->value && e->value->type == AS_VALUE_STR) {
                    MHD_response_add_header(resp, e->key, e->value->data.str_val);
                }
                e = e->next;
            }
        }
    }

    as_value_release(result);
    return resp;
}

static const struct MHD_Action *mhd_req_cb(void *cls, struct MHD_Request *request,
                                           const struct MHD_String *path, enum MHD_HTTP_Method method,
                                           uint_fast64_t upload_size) {
    http_server_slot_t *slot = (http_server_slot_t *)cls;
    const char *path_cstr = "/";
    if (path && path->cstr)
        path_cstr = path->cstr;

    const struct MHD_String *mstr = MHD_http_method_to_string(method);
    const char *method_cstr = "GET";
    if (mstr && mstr->cstr)
        method_cstr = mstr->cstr;

    as_value_t *req_obj = as_value_obj();
    as_obj_t *rq = req_obj->data.obj_val;
    as_obj_set(rq, "method", as_value_str(method_cstr));
    as_obj_set(rq, "path", as_value_str(path_cstr));
    as_obj_set(rq, "uploadSize", as_value_num((double)upload_size));

    as_value_t *arg = req_obj;
    as_value_t *result =
        as_call_user_fn(slot->interp, slot->handler, &arg, 1);
    as_value_release(req_obj);

    struct MHD_Response *resp = build_from_handler_result(result);
    if (!resp)
        return MHD_action_abort_request(request);

    const struct MHD_Action *action = MHD_action_from_response(request, resp);
    if (!action)
        MHD_response_destroy(resp);
    return action;
}

static as_value_t *http_server_create(as_value_t **args, size_t arg_count, void *userdata) {
    as_interpreter_t *interp = (as_interpreter_t *)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ)
        return as_value_error("TypeError", as_value_str("HttpServer:create: expected options object"));

    as_obj_t *opts = args[0]->data.obj_val;
    as_value_t *vp = as_obj_get(opts, "port");
    if (!vp || vp->type != AS_VALUE_NUM)
        return as_value_error("TypeError", as_value_str("HttpServer:create: missing port number"));

    int port = (int)vp->data.num_val;
    if (port < 1 || port > 65535)
        return as_value_error("TypeError", as_value_str("HttpServer:create: port out of range"));

    as_value_t *vh = as_obj_get(opts, "handler");
    if (!vh || vh->type != AS_VALUE_FN)
        return as_value_error("TypeError", as_value_str("HttpServer:create: missing handler function"));

    size_t i;
    for (i = 0; i < MAX_SERVERS; i++) {
        if (!g_servers[i].used)
            break;
    }
    if (i >= MAX_SERVERS)
        return as_value_error("OSError", as_value_str("HttpServer:create: too many servers"));

    http_server_slot_t *slot = &g_servers[i];
    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->id = g_next_id++;
    slot->interp = interp;
    slot->handler = as_value_retain(vh);

    struct MHD_Daemon *d = MHD_daemon_create(&mhd_req_cb, slot);
    if (!d) {
        as_value_release(slot->handler);
        memset(slot, 0, sizeof(*slot));
        return as_value_error("OSError", as_value_str("MHD_daemon_create failed"));
    }
    slot->daemon = d;

    if (MHD_SC_OK !=
        MHD_DAEMON_SET_OPTIONS(d, MHD_D_OPTION_WM_EXTERNAL_PERIODIC(),
                               MHD_D_OPTION_BIND_PORT(MHD_AF_AUTO, (uint_least16_t)port))) {
        MHD_daemon_destroy(d);
        as_value_release(slot->handler);
        memset(slot, 0, sizeof(*slot));
        return as_value_error("OSError", as_value_str("MHD_DAEMON_SET_OPTIONS failed"));
    }

    if (MHD_SC_OK != MHD_daemon_start(d)) {
        MHD_daemon_destroy(d);
        as_value_release(slot->handler);
        memset(slot, 0, sizeof(*slot));
        return as_value_error("OSError", as_value_str("MHD_daemon_start failed"));
    }

    return as_value_num((double)slot->id);
}

static as_value_t *http_server_poll(as_value_t **args, size_t arg_count, void *userdata) {
    (void)userdata;
    if (arg_count < 2 || args[0]->type != AS_VALUE_NUM || args[1]->type != AS_VALUE_NUM)
        return as_value_error("TypeError",
                              as_value_str("HttpServer:poll: expected (handle, microseconds)"));

    int id = (int)args[0]->data.num_val;
    uint_fast64_t usec = (uint_fast64_t)args[1]->data.num_val;

    http_server_slot_t *slot = find_slot_by_id(id);
    if (!slot || !slot->daemon)
        return as_value_error("OSError", as_value_str("HttpServer:poll: invalid handle"));

    enum MHD_StatusCode sc = MHD_daemon_process_blocking(slot->daemon, usec);
    if (sc != MHD_SC_OK)
        return as_value_error("OSError", as_value_str("MHD_daemon_process_blocking failed"));
    return as_value_null();
}

static as_value_t *http_server_stop(as_value_t **args, size_t arg_count, void *userdata) {
    (void)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_NUM)
        return as_value_error("TypeError", as_value_str("HttpServer:stop: expected handle"));

    int id = (int)args[0]->data.num_val;
    http_server_slot_t *slot = find_slot_by_id(id);
    if (!slot || !slot->daemon)
        return as_value_error("OSError", as_value_str("HttpServer:stop: invalid handle"));

    MHD_daemon_destroy(slot->daemon);
    slot->daemon = NULL;
    as_value_release(slot->handler);
    slot->handler = NULL;
    slot->used = 0;
    return as_value_null();
}

AS_EXPORT void as_modinit_http_server(as_interpreter_t *interp, as_scope_t *module_scope) {
    as_scope_t *global = interp->global_scope;
    as_value_t *fn;

    fn = as_value_fn_native(http_server_create, interp);
    as_scope_add(global, "HttpServer:create", fn, false);
    as_scope_add(module_scope, "HttpServer:create", fn, false);
    as_value_release(fn);

    fn = as_value_fn_native(http_server_poll, NULL);
    as_scope_add(global, "HttpServer:poll", fn, false);
    as_scope_add(module_scope, "HttpServer:poll", fn, false);
    as_value_release(fn);

    fn = as_value_fn_native(http_server_stop, NULL);
    as_scope_add(global, "HttpServer:stop", fn, false);
    as_scope_add(module_scope, "HttpServer:stop", fn, false);
    as_value_release(fn);
}

static as_module_metadata_t meta = {
    .name = "http_server",
    .version = "0.1.0",
    .author = "AiScript C",
    .description = "HTTP server via libmicrohttpd2 (external periodic)",
    .license = "LGPL (libmicrohttpd2)",
    .url = "",
};

AS_EXPORT as_module_metadata_t *as_modmeta_http_server(void) {
    return &meta;
}
