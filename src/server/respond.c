#include "server.h"
#include "gitsight/usage.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* ---- request ---- */

void req_init(Request *r, int fd) {
    memset(r, 0, sizeof(*r));
    r->fd = fd;
    r->keep_alive = 1;
}

void req_free(Request *r) {
    if (!r) return;
    free(r->body);
    r->body = NULL;
    r->body_len = 0;
}

const char *req_header(const Request *r, const char *name) {
    if (!r || !name) return NULL;
    for (int i = 0; i < r->header_count; i++)
        if (strcasecmp(r->header_name[i], name) == 0) return r->header_val[i];
    return NULL;
}

/* Query-string lookup. Values are percent-decoded into `out`. */
const char *req_query(const Request *r, const char *key, char *out, size_t len) {
    if (out && len) out[0] = '\0';
    if (!r || !key || !r->query[0]) return NULL;
    size_t klen = strlen(key);
    const char *p = r->query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        if (seg > klen && p[klen] == '=' && strncmp(p, key, klen) == 0) {
            const char *v = p + klen + 1;
            size_t vlen = seg - klen - 1;
            if (out) {
                if (vlen >= len) vlen = len - 1;
                memcpy(out, v, vlen);
                out[vlen] = '\0';
                url_decode(out, out, len);
            }
            return v;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return NULL;
}

/* ---- response ---- */

void res_init(Response *res) {
    memset(res, 0, sizeof(*res));
    res->status = 200;
    snprintf(res->content_type, sizeof(res->content_type),
             "application/octet-stream");
    snprintf(res->cache_control, sizeof(res->cache_control), "no-store");
}

void res_cache_control(Response *res, const char *value) {
    if (!res || !value || !*value) return;
    snprintf(res->cache_control, sizeof(res->cache_control), "%s", value);
}

void res_free(Response *res) {
    if (!res) return;
    free(res->body);
    res->body = NULL;
    for (int i = 0; i < res->header_count; i++) free(res->headers[i]);
    res->header_count = 0;
}

void res_header(Response *res, const char *line) {
    if (res->header_count >= 16) return;
    res->headers[res->header_count++] = strdup(line);
}

static void res_take(Response *res, int status, const char *ctype,
                     const void *data, size_t len) {
    res->status = status;
    if (ctype) snprintf(res->content_type, sizeof(res->content_type), "%s", ctype);
    res->body = malloc(len + 1);
    if (!res->body) {
        res->status = 500;
        return;
    }
    if (len && data) memcpy(res->body, data, len);
    res->body[len] = '\0';
    res->body_len = len;
}

void res_json(Response *res, int status, const char *json_text) {
    res_take(res, status, "application/json; charset=utf-8",
             json_text, json_text ? strlen(json_text) : 0);
}

/* Takes ownership of the cJSON tree and prints it compactly. */
void res_json_obj(Response *res, int status, void *cjson_root) {
    cJSON *root = (cJSON *)cjson_root;
    if (!root) {
        res_error(res, 500, "internal", "failed to build response");
        return;
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        res_error(res, 500, "internal", "failed to serialize response");
        return;
    }
    res_json(res, status, text);
    free(text);
}

void res_text(Response *res, int status, const char *ctype, const char *text) {
    res_take(res, status, ctype, text, text ? strlen(text) : 0);
}

void res_bytes(Response *res, int status, const char *ctype,
               const void *data, size_t len) {
    res_take(res, status, ctype, data, len);
}

void res_error(Response *res, int status, const char *code, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", code ? code : "error");
    cJSON_AddStringToObject(root, "message", message ? message : "");
    cJSON_AddNumberToObject(root, "status", status);
    res_json_obj(res, status, root);
}

int http_status_for_error(ErrorCode code) {
    switch (code) {
        case GS_OK:                    return 200;
        case ERR_INVALID_ARGS:         return 400;
        case ERR_REPO_FORMAT:          return 400;
        case ERR_UNSUPPORTED_FORMAT:   return 400;
        case ERR_CONFIG_LOAD:          return 400;
        case ERR_CANCELLED:            return 499;
        case ERR_CONFIG_WRITE:         return 500;
        case ERR_NETWORK:              return 502;
        case ERR_HTTP_STATUS:          return 502;
        case ERR_API_ERROR:            return 502;
        case ERR_LLM_API:              return 502;
        case ERR_LLM_CONFIG:           return 424;
        case ERR_JSON_PARSE:           return 500;
        case ERR_MEMORY:               return 500;
        case ERR_FILE_IO:              return 500;
        case ERR_EXPORT:               return 500;
        case ERR_CURL_INIT:            return 500;
        case ERR_THREAD:               return 500;
        default:                       return 500;
    }
}

void res_error_code(Response *res, ErrorCode code, const char *detail) {
    char code_name[64];
    snprintf(code_name, sizeof(code_name), "%s", gs_error_string(code));
    for (char *p = code_name; *p; p++)
        if (*p == ' ') { *p = '_'; break; }
    char message[512];
    snprintf(message, sizeof(message), "%s%s%s",
             gs_error_string(code),
             (detail && *detail) ? ": " : "",
             (detail && *detail) ? detail : "");
    res_error(res, http_status_for_error(code), code_name, message);
}

void res_no_content(Response *res) {
    res->status = 204;
    res->body = NULL;
    res->body_len = 0;
}

void res_redirect(Response *res, int status, const char *location) {
    char line[HTTP_MAX_URI + 32];
    snprintf(line, sizeof(line), "Location: %s", location);
    res_header(res, line);
    res_text(res, status, "text/plain; charset=utf-8", "");
}

void res_stream(Response *res, const char *content_type) {
    res->streaming = 1;
    res->finished = 1;
    snprintf(res->content_type, sizeof(res->content_type), "%s", content_type);
    res_cache_control(res, "no-cache, no-transform");
    res_header(res, "X-Accel-Buffering: no");
    res_header(res, "Connection: close");
}

/* ---- misc helpers ---- */

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Percent-decoding. `keep_slash` leaves %2F encoded so a value like
 * "owner%2Frepo" stays inside one path segment instead of becoming two; route
 * parameters are decoded per-value afterwards, where splitting is harmless. */
size_t url_decode_ex(const char *in, char *out, size_t out_cap, int keep_slash) {
    size_t o = 0;
    if (!out_cap) return 0;
    for (size_t i = 0; in[i] && o + 1 < out_cap; i++) {
        if (in[i] == '%' && hexval(in[i + 1]) >= 0 && hexval(in[i + 2]) >= 0) {
            int hi = hexval(in[i + 1]), lo = hexval(in[i + 2]);
            if (keep_slash && hi == 2 && lo == 0x0F) {   /* %2F */
                out[o++] = in[i];
                out[o++] = in[i + 1];
                out[o++] = in[i + 2];
            } else {
                out[o++] = (char)((hi << 4) | lo);
            }
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
    return o;
}

size_t url_decode(const char *in, char *out, size_t out_cap) {
    return url_decode_ex(in, out, out_cap, 0);
}

static const struct { const char *fmt, *ext, *mime; int stdout_ok; } k_formats[] = {
    { "json", "json", "application/json; charset=utf-8",   1 },
    { "md",   "md",   "text/markdown; charset=utf-8",       1 },
    { "txt",  "txt",  "text/plain; charset=utf-8",          1 },
    { "html", "html", "text/html; charset=utf-8",           1 },
    { "csv",  "csv",  "text/csv; charset=utf-8",            1 },
    { "xlsx", "xlsx",
      "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", 0 },
    { "pdf",  "pdf",  "application/pdf",                    0 },
    { NULL, NULL, NULL, 0 }
};

int format_is_known(const char *format) {
    for (int i = 0; k_formats[i].fmt; i++)
        if (strcmp(k_formats[i].fmt, format) == 0) return 1;
    return 0;
}

const char *format_extension(const char *format) {
    for (int i = 0; k_formats[i].fmt; i++)
        if (strcmp(k_formats[i].fmt, format) == 0) return k_formats[i].ext;
    return "txt";
}

const char *format_mime(const char *format) {
    for (int i = 0; k_formats[i].fmt; i++)
        if (strcmp(k_formats[i].fmt, format) == 0) return k_formats[i].mime;
    return "application/octet-stream";
}

/* Every exporter writes to a buffer here, so the stdout restriction that the
 * CLI enforces (xlsx/pdf need -o) does not apply to downloads. */
int format_supports_stdout(const char *format) {
    return format_is_known(format);
}

/* ---- router ---- */

void router_init(Router *r) {
    memset(r, 0, sizeof(*r));
}

int router_add(Router *r, const char *method, const char *pattern,
               RouteHandler handler, void *user) {
    if (r->count >= ROUTE_MAX) return -1;
    Route *rt = &r->routes[r->count];
    memset(rt, 0, sizeof(*rt));
    snprintf(rt->method, sizeof(rt->method), "%s", method);
    snprintf(rt->pattern, sizeof(rt->pattern), "%s", pattern);
    rt->handler = handler;
    rt->user = user;
    r->count++;
    return 0;
}

/* Matches a pattern with optional ":name" segments against a concrete path.
 * Returns 1 on match, capturing parameter values into params. */
static int route_match(Route *rt, const char *path) {
    rt->param_count = 0;
    const char *p = rt->pattern;
    const char *s = path;

    while (*p) {
        const char *pslash = strchr(p, '/');
        size_t seg_len = pslash ? (size_t)(pslash - p) : strlen(p);

        const char *sslash = strchr(s, '/');
        size_t sseg_len = sslash ? (size_t)(sslash - s) : strlen(s);

        if (seg_len == 0) {
            /* Empty pattern segment: the path segment must be empty too. */
            if (sseg_len != 0) return 0;
            s = sslash ? sslash + 1 : s + strlen(s);
            p = pslash ? pslash + 1 : p + seg_len;
            continue;
        }
        if (seg_len == 1 && p[0] == '*') {
            /* Trailing wildcard captures the remainder. */
            if (rt->param_count < 8)
                snprintf(rt->params[rt->param_count], 128, "%s", s);
            rt->param_count++;
            return 1;
        }

        /* A ":name" segment captures whatever the path offers there; only
         * literal segments are compared for equality. */
        int is_param = (seg_len >= 2 && p[0] == ':');
        if (!is_param && (sseg_len != seg_len || strncmp(p, s, seg_len) != 0))
            return 0;

        if (is_param) {
            if (rt->param_count >= 8 || sseg_len == 0) return 0;
            char name[64];
            size_t nl = seg_len - 1 < sizeof(name) - 1 ? seg_len - 1 : sizeof(name) - 1;
            memcpy(name, p + 1, nl);
            name[nl] = '\0';
            snprintf(rt->param_names[rt->param_count], 64, "%s", name);
            snprintf(rt->params[rt->param_count], 128, "%.*s", (int)sseg_len, s);
            rt->param_count++;
        }
        /* Both cursors step past their separator, so "a/b" walks to "b". */
        p = pslash ? pslash + 1 : p + seg_len;
        s = sslash ? sslash + 1 : s + sseg_len;
    }
    return *s == '\0';
}

void router_dispatch(Router *r, Request *req, Response *res) {
    int path_exists = 0;
    for (int i = 0; i < r->count; i++) {
        Route *rt = &r->routes[i];
        if (!route_match(rt, req->path)) continue;
        path_exists = 1;
        if (strcasecmp(rt->method, req->method) != 0) continue;
        req->route = rt;
        rt->handler(req, res, rt->user);
        return;
    }
    if (path_exists)
        res_error(res, 405, "method_not_allowed",
                  "the path exists but not for this HTTP method");
    else
        res_error(res, 404, "not_found", "no such endpoint");
}