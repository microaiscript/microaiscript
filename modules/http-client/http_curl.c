/*
 * Native HTTP client for AiScript C (libcurl).
 * Build: see Makefile in this directory.
 */

#define _GNU_SOURCE
#include <aiscript.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

typedef struct {
    char *data;
    size_t len;
} mem_buf_t;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    mem_buf_t *buf = (mem_buf_t *)userdata;
    size_t add = size * nmemb;
    char *n = (char *)realloc(buf->data, buf->len + add + 1);
    if (!n)
        return 0;
    buf->data = n;
    memcpy(buf->data + buf->len, ptr, add);
    buf->len += add;
    buf->data[buf->len] = '\0';
    return add;
}

static void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n'))
        n--;
    s[n] = '\0';
}

static const char *ltrim(const char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static size_t curl_header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    as_obj_t *hdr_obj = (as_obj_t *)userdata;
    size_t total = size * nitems;
    char *line = (char *)malloc(total + 1);
    if (!line)
        return total;
    memcpy(line, buffer, total);
    line[total] = '\0';
    trim_crlf(line);

    if (line[0] == '\0') {
        free(line);
        return total;
    }
    if (strncmp(line, "HTTP/", 5) == 0) {
        free(line);
        return total;
    }

    char *colon = strchr(line, ':');
    if (!colon) {
        free(line);
        return total;
    }
    *colon = '\0';
    const char *name = ltrim(line);
    const char *val = ltrim(colon + 1);
    if (name[0] == '\0') {
        free(line);
        return total;
    }
    as_obj_set(hdr_obj, name, as_value_str(val));
    free(line);
    return total;
}

static as_value_t *perform_curl_request(const char *url, const char *method,
                                        as_obj_t *req_headers, const char *body, size_t body_len) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return as_value_error("OSError", as_value_str("curl_easy_init failed"));

    mem_buf_t body_buf = { NULL, 0 };
    as_value_t *hdr_val = as_value_obj();
    as_obj_t *hdr_obj = hdr_val->data.obj_val;

    struct curl_slist *slist = NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, hdr_obj);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    const char *m = method ? method : "GET";
    if (strcasecmp(m, "GET") != 0)
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, m);

    if (body && body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }

    if (req_headers) {
        size_t bi;
        for (bi = 0; bi < req_headers->bucket_count; bi++) {
            as_obj_entry_t *e = req_headers->buckets[bi];
            while (e) {
                if (e->key && e->value && e->value->type == AS_VALUE_STR) {
                    char *line;
                    if (asprintf(&line, "%s: %s", e->key, e->value->data.str_val) < 0) {
                        curl_slist_free_all(slist);
                        curl_easy_cleanup(curl);
                        as_value_release(hdr_val);
                        free(body_buf.data);
                        return as_value_error("OSError", as_value_str("out of memory"));
                    }
                    struct curl_slist *n = curl_slist_append(slist, line);
                    free(line);
                    if (!n) {
                        curl_slist_free_all(slist);
                        curl_easy_cleanup(curl);
                        as_value_release(hdr_val);
                        free(body_buf.data);
                        return as_value_error("OSError", as_value_str("curl_slist_append failed"));
                    }
                    slist = n;
                }
                e = e->next;
            }
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(slist);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        as_value_release(hdr_val);
        free(body_buf.data);
        return as_value_error("OSError", as_value_str(curl_easy_strerror(res)));
    }

    as_value_t *out = as_value_obj();
    as_obj_t *o = out->data.obj_val;
    as_obj_set(o, "status", as_value_num((double)http_code));
    as_obj_set(o, "ok", as_value_bool(http_code >= 200 && http_code < 300));
    {
        as_value_t *bodyv = as_value_str(body_buf.data ? body_buf.data : "");
        as_obj_set(o, "body", bodyv);
        as_obj_set(o, "content", bodyv);
        as_value_release(bodyv);
    }
    as_obj_set(o, "headers", hdr_val);
    as_value_release(hdr_val);
    free(body_buf.data);
    return out;
}

static as_value_t *http_request(as_value_t **args, size_t arg_count, void *userdata) {
    (void)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ)
        return as_value_error("TypeError", as_value_str("Http:request: expected options object"));

    as_obj_t *opts = args[0]->data.obj_val;
    as_value_t *vu = as_obj_get(opts, "url");
    if (!vu || vu->type != AS_VALUE_STR)
        return as_value_error("TypeError", as_value_str("Http:request: missing url string"));

    const char *url = vu->data.str_val;
    const char *method = "GET";
    as_value_t *vm = as_obj_get(opts, "method");
    if (vm && vm->type == AS_VALUE_STR)
        method = vm->data.str_val;

    const char *body = NULL;
    size_t body_len = 0;
    as_value_t *vb = as_obj_get(opts, "body");
    if (vb && vb->type == AS_VALUE_STR) {
        body = vb->data.str_val;
        body_len = strlen(body);
    }

    as_obj_t *reqh = NULL;
    as_value_t *vh = as_obj_get(opts, "headers");
    if (vh && vh->type == AS_VALUE_OBJ)
        reqh = vh->data.obj_val;

    return perform_curl_request(url, method, reqh, body, body_len);
}

static as_value_t *http_get_impl(as_value_t **args, size_t arg_count, void *userdata) {
    (void)userdata;
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR)
        return as_value_error("TypeError", as_value_str("Http:get: expected url string"));
    return perform_curl_request(args[0]->data.str_val, "GET", NULL, NULL, 0);
}

static int curl_inited = 0;

AS_EXPORT void as_modinit_http_client(as_interpreter_t *interp, as_scope_t *module_scope) {
    as_scope_t *global = interp->global_scope;
    if (!curl_inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_inited = 1;
    }

    as_value_t *fn;

    fn = as_value_fn_native(http_get_impl, NULL);
    as_scope_add(global, "Http:get", fn, false);
    as_scope_add(module_scope, "Http:get", fn, false);
    as_value_release(fn);

    fn = as_value_fn_native(http_request, NULL);
    as_scope_add(global, "Http:request", fn, false);
    as_scope_add(module_scope, "Http:request", fn, false);
    as_value_release(fn);
}

static as_module_metadata_t meta = {
    .name = "http_client",
    .version = "0.1.0",
    .author = "AiScript C",
    .description = "HTTP client via libcurl",
    .license = "MIT",
    .url = "",
};

AS_EXPORT as_module_metadata_t *as_modmeta_http_client(void) {
    return &meta;
}
