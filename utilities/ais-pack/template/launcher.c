/*
 * Embedded .aiz launcher — compiled with ais_pack_bundle_data.c, libaiscript.a, cJSON.c
 */

#define _POSIX_C_SOURCE 200809L

#include <aiscript.h>
#include <cJSON.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

extern const unsigned char ais_pack_bundle[];
extern const size_t ais_pack_bundle_len;

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ensure_parent_dirs(char *path) {
    char *last = strrchr(path, '/');
    if (!last)
        return 0;
    *last = '\0';
    if (strlen(path) == 0) {
        *last = '/';
        return 0;
    }
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0755);
            *p = '/';
        }
    }
    mkdir(path, 0755);
    *last = '/';
    return 0;
}

static int write_file(const char *root, const char *rel, const unsigned char *data, size_t len) {
    char full[PATH_MAX];
    if (snprintf(full, sizeof(full), "%s/%s", root, rel) >= (int)sizeof(full))
        return -1;
    ensure_parent_dirs(full);
    FILE *f = fopen(full, "wb");
    if (!f)
        return -1;
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void rm_rf(const char *path) {
    char cmd[PATH_MAX + 32];
    /* single-quoted path for POSIX shell — paths from our archive should not contain ' */
    if (strchr(path, '\''))
        return;
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    int r = system(cmd);
    (void)r;
}

static char *slurp_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len)
        *out_len = n;
    return buf;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    const unsigned char *data = ais_pack_bundle;
    size_t len = ais_pack_bundle_len;

    if (len < 12) {
        fprintf(stderr, "ais-pack launcher: bundle too small\n");
        return 1;
    }
    if (data[0] != 'A' || data[1] != 'I' || data[2] != 'Z' || data[3] != 0x01) {
        fprintf(stderr, "ais-pack launcher: bad magic\n");
        return 1;
    }
    uint32_t fmt = read_le32(data + 4);
    if (fmt != 1) {
        fprintf(stderr, "ais-pack launcher: unsupported format %u\n", (unsigned)fmt);
        return 1;
    }
    uint32_t mjlen = read_le32(data + 8);
    if (len < 12 + mjlen) {
        fprintf(stderr, "ais-pack launcher: truncated manifest\n");
        return 1;
    }

    char *mj = (char *)malloc((size_t)mjlen + 1);
    if (!mj)
        return 1;
    memcpy(mj, data + 12, mjlen);
    mj[mjlen] = '\0';

    cJSON *root = cJSON_Parse(mj);
    free(mj);
    if (!root) {
        fprintf(stderr, "ais-pack launcher: manifest JSON parse error\n");
        return 1;
    }

    cJSON *entry_js = cJSON_GetObjectItem(root, "entry");
    if (!entry_js || !cJSON_IsString(entry_js)) {
        fprintf(stderr, "ais-pack launcher: missing entry\n");
        cJSON_Delete(root);
        return 1;
    }
    char *entry = strdup(entry_js->valuestring);
    if (!entry) {
        cJSON_Delete(root);
        return 1;
    }

    cJSON *files = cJSON_GetObjectItem(root, "files");
    if (!files || !cJSON_IsArray(files)) {
        fprintf(stderr, "ais-pack launcher: missing files\n");
        free(entry);
        cJSON_Delete(root);
        return 1;
    }

    unsigned long raw_total = 0;
    cJSON *f;
    cJSON_ArrayForEach(f, files) {
        cJSON *sz = cJSON_GetObjectItem(f, "size");
        if (!sz || !cJSON_IsNumber(sz)) {
            fprintf(stderr, "ais-pack launcher: bad file entry\n");
            free(entry);
            cJSON_Delete(root);
            return 1;
        }
        raw_total += (unsigned long)sz->valuedouble;
    }

    const unsigned char *zdata = data + 12 + mjlen;
    size_t zlen = len - 12 - mjlen;

    unsigned char *raw = (unsigned char *)malloc(raw_total ? raw_total : 1);
    if (!raw) {
        free(entry);
        cJSON_Delete(root);
        return 1;
    }
    unsigned long destlen = raw_total;
    int zr = uncompress(raw, &destlen, zdata, (unsigned long)zlen);
    if (zr != Z_OK || destlen != raw_total) {
        fprintf(stderr, "ais-pack launcher: zlib error %d (got %lu want %lu)\n", zr,
                (unsigned long)destlen, raw_total);
        free(raw);
        free(entry);
        cJSON_Delete(root);
        return 1;
    }

    const char *td = getenv("TMPDIR");
    if (!td || !*td)
        td = "/tmp";
    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof(tmpl), "%s/aisXXXXXX", td);
    char *tmpdir = mkdtemp(tmpl);
    if (!tmpdir) {
        perror("mkdtemp");
        free(raw);
        free(entry);
        cJSON_Delete(root);
        return 1;
    }
    char tmpdir_copy[PATH_MAX];
    strncpy(tmpdir_copy, tmpdir, sizeof(tmpdir_copy) - 1);
    tmpdir_copy[sizeof(tmpdir_copy) - 1] = '\0';

    size_t off = 0;
    cJSON_ArrayForEach(f, files) {
        cJSON *path = cJSON_GetObjectItem(f, "path");
        cJSON *sz = cJSON_GetObjectItem(f, "size");
        if (!path || !cJSON_IsString(path) || !sz || !cJSON_IsNumber(sz)) {
            fprintf(stderr, "ais-pack launcher: bad file row\n");
            free(raw);
            free(entry);
            rm_rf(tmpdir_copy);
            cJSON_Delete(root);
            return 1;
        }
        size_t fsz = (size_t)sz->valuedouble;
        if (write_file(tmpdir, path->valuestring, raw + off, fsz) != 0) {
            fprintf(stderr, "ais-pack launcher: failed to write %s\n", path->valuestring);
            free(raw);
            free(entry);
            rm_rf(tmpdir_copy);
            cJSON_Delete(root);
            return 1;
        }
        off += fsz;
    }
    free(raw);
    cJSON_Delete(root);

    if (chdir(tmpdir) != 0) {
        perror("chdir");
        free(entry);
        rm_rf(tmpdir_copy);
        return 1;
    }

    char entry_path[PATH_MAX];
    if (snprintf(entry_path, sizeof(entry_path), "%s/%s", tmpdir_copy, entry) >= (int)sizeof(entry_path)) {
        fprintf(stderr, "ais-pack launcher: entry path too long\n");
        free(entry);
        rm_rf(tmpdir_copy);
        return 1;
    }

    size_t srclen = 0;
    char *src = slurp_file(entry, &srclen);
    free(entry);
    if (!src) {
        fprintf(stderr, "ais-pack launcher: cannot read entry file\n");
        rm_rf(tmpdir_copy);
        return 1;
    }

    as_interpreter_t *interp = as_interpreter_new();
    if (!interp) {
        free(src);
        rm_rf(tmpdir_copy);
        return 1;
    }
    interp->current_file = entry_path;

    char *err = NULL;
    as_value_t *val = as_eval(interp, src, &err);
    free(src);

    int exit_code = 0;
    if (err) {
        fprintf(stderr, "%s\n", err);
        free(err);
        exit_code = 1;
    } else if (val && val->type == AS_VALUE_ERROR) {
        exit_code = 1;
    }

    if (val)
        as_value_release(val);
    as_interpreter_free(interp);

    rm_rf(tmpdir_copy);
    return exit_code;
}
