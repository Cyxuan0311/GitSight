#define _GNU_SOURCE

#include "server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const struct { const char *ext, *mime; } k_mime[] = {
    { "html",  "text/html; charset=utf-8" },
    { "js",    "text/javascript; charset=utf-8" },
    { "mjs",   "text/javascript; charset=utf-8" },
    { "css",   "text/css; charset=utf-8" },
    { "json",  "application/json; charset=utf-8" },
    { "map",   "application/json; charset=utf-8" },
    { "svg",   "image/svg+xml" },
    { "png",   "image/png" },
    { "jpg",   "image/jpeg" },
    { "jpeg",  "image/jpeg" },
    { "gif",   "image/gif" },
    { "webp",  "image/webp" },
    { "ico",   "image/x-icon" },
    { "woff",  "font/woff" },
    { "woff2", "font/woff2" },
    { "ttf",   "font/ttf" },
    { "txt",   "text/plain; charset=utf-8" },
    { "wasm",  "application/wasm" },
    { "pdf",   "application/pdf" },
    { NULL, NULL }
};

static const char *mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    for (int i = 0; k_mime[i].ext; i++)
        if (strcasecmp(dot + 1, k_mime[i].ext) == 0) return k_mime[i].mime;
    return "application/octet-stream";
}

void static_set_root(StaticRoot *sr, const char *root) {
    memset(sr, 0, sizeof(*sr));
    if (root && *root) {
        snprintf(sr->root, sizeof(sr->root), "%s", root);
        /* Strip a trailing slash so joins are unambiguous. */
        size_t n = strlen(sr->root);
        while (n > 1 && sr->root[n - 1] == '/') sr->root[--n] = '\0';
        sr->enabled = 1;
    }
}

/* Canonicalizes `p` and verifies it stays inside `root`. */
static int within_root(const char *root, const char *p) {
    char rroot[PATH_MAX_LEN], rpath[PATH_MAX_LEN];
    if (!realpath(root, rroot)) return 0;
    if (!realpath(p, rpath)) return 0;
    size_t rl = strlen(rroot);
    if (strncmp(rpath, rroot, rl) != 0) return 0;
    return rpath[rl] == '\0' || rpath[rl] == '/';
}

int static_resolve(const StaticRoot *sr, const char *url_path, char *out, size_t len) {
    if (!sr->enabled) return 0;
    char rel[1024];
    /* Reject any traversal attempt before touching the filesystem. */
    if (strstr(url_path, "..")) return 0;
    snprintf(rel, sizeof(rel), "%s/%s", sr->root,
             (url_path[0] == '/') ? url_path + 1 : url_path);

    struct stat st;
    if (stat(rel, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    if (!within_root(sr->root, rel)) return 0;

    snprintf(out, len, "%s", rel);
    return 1;
}

/* A path whose last segment carries an extension is asking for an asset. */
static int path_looks_like_asset(const char *p) {
    const char *slash = strrchr(p, '/');
    const char *base = slash ? slash + 1 : p;
    return strchr(base, '.') != NULL;
}

/* SPA fallback: unknown GET paths resolve to index.html so client-side routes
 * survive a page reload. Never applied to asset-like paths or to anything
 * containing a traversal sequence -- those deserve a real 404, not a 200 that
 * silently hides the attempt. */
static int serve_index(const StaticRoot *sr, const char *path, Response *res) {
    if (strstr(path, "..")) return 0;
    if (path_looks_like_asset(path)) return 0;
    char idx[1024];
    snprintf(idx, sizeof(idx), "%s/index.html", sr->root);
    struct stat st;
    if (stat(idx, &st) != 0 || !S_ISREG(st.st_mode)) return 0;

    FILE *fp = fopen(idx, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return 0; }

    char *data = malloc((size_t)sz + 1);
    if (!data) { fclose(fp); return 0; }
    size_t got = fread(data, 1, (size_t)sz, fp);
    fclose(fp);
    data[got] = '\0';

    /* Hashed bundle filenames are immutable; index.html must not be cached or
     * a deploy would keep serving the previous entrypoint. */
    res_cache_control(res, "no-cache");
    res_bytes(res, 200, "text/html; charset=utf-8", data, got);
    free(data);
    return 1;
}

int static_serve(Request *req, Response *res) {
    StaticRoot sr;
    static_set_root(&sr, g_server.web_root);
    if (!sr.enabled) return 0;

    char path[1200];
    if (static_resolve(&sr, req->path, path, sizeof(path))) {
        FILE *fp = fopen(path, "rb");
        if (!fp) {
            res_error(res, 404, "not_found", "cannot read file");
            return 1;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz < 0) { fclose(fp); return 0; }

        char *data = malloc((size_t)sz + 1);
        if (!data) { fclose(fp); return 0; }
        size_t got = fread(data, 1, (size_t)sz, fp);
        fclose(fp);
        data[got] = '\0';

        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        /* Vite emits content-hashed names under /assets. */
        if (strstr(req->path, "/assets/")) {
            res_cache_control(res, "public, max-age=31536000, immutable");
        } else {
            res_cache_control(res, "no-cache");
        }

        res_bytes(res, 200, mime_for(base), data, got);
        free(data);
        return 1;
    }

    if (serve_index(&sr, req->path, res)) return 1;
    return 0;
}