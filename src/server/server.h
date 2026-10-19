#ifndef GS_SERVER_H
#define GS_SERVER_H

#include "cmd.h"
#include "gitsight/config.h"
#include "gitsight/error.h"

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * A small HTTP/1.1 server. It exists so the browser console can drive the
 * exact code paths the CLI already uses, rather than reimplementing any of
 * the analysis, export, or configuration logic in a second language.
 *
 * Scope is deliberately narrow: request line + headers + Content-Length body,
 * keep-alive, and binary-safe responses (xlsx/pdf are not text). There is no
 * TLS and no authentication -- the listener binds to loopback by default and
 * the tool stores its secrets in a local config file.
 * ------------------------------------------------------------------------ */

#define GS_SERVER_DEFAULT_HOST "127.0.0.1"
#define GS_SERVER_DEFAULT_PORT 8787

#define HTTP_MAX_BODY   (8 * 1024 * 1024)
#define HTTP_MAX_HEADER (16 * 1024)
#define HTTP_MAX_URI    2048
#define PATH_MAX_LEN    4096

typedef struct {
    char method[16];
    char target[HTTP_MAX_URI];   /* raw request target, may carry a query */
    char path[HTTP_MAX_URI];     /* percent-decoded path */
    char query[HTTP_MAX_URI];    /* raw query string, without '?' */

    char *body;
    size_t body_len;

    /* Headers, kept as parallel name/value arrays for case-insensitive
     * lookup without allocating per lookup. */
    char  header_name[32][128];
    char *header_val[32];
    int   header_count;

    int keep_alive;
    int fd;
    unsigned long long id;
    void *route;      /* Route* */
} Request;

/* Response is built either eagerly (static bytes) or streamed (SSE). */
typedef struct {
    int   status;
    char  content_type[128];
    char *headers[16];
    int   header_count;

    /* Emitted as the single Cache-Control field. Default "no-store", because a
     * response can reflect config the next request would not reproduce; handlers
     * that know better (static assets, the SPA shell) overwrite it. Kept out of
     * `headers` so one response can never carry two conflicting values. */
    char  cache_control[64];

    /* Eager payload. */
    char  *body;
    size_t body_len;

    /* Streaming payload: when set, the handler has already taken over the
     * socket and `finished` is irrelevant. */
    int   streaming;
    int   finished;
} Response;

/* ---- request / response helpers ---- */
void  req_init(Request *r, int fd);
void  req_free(Request *r);
const char *req_header(const Request *r, const char *name);
const char *req_query(const Request *r, const char *key, char *out, size_t len);

void  res_init(Response *res);
void  res_free(Response *res);
void  res_header(Response *res, const char *line);
/* Replaces the response's Cache-Control value; ignored once streaming. */
void  res_cache_control(Response *res, const char *value);
void  res_json(Response *res, int status, const char *json_text);
void  res_json_obj(Response *res, int status, void *cjson_root);
void  res_text(Response *res, int status, const char *ctype, const char *text);
void  res_bytes(Response *res, int status, const char *ctype, const void *data, size_t len);
void  res_error(Response *res, int status, const char *code, const char *message);
void  res_error_code(Response *res, ErrorCode code, const char *detail);
void  res_no_content(Response *res);
void  res_redirect(Response *res, int status, const char *location);
/* Marks the response as a live SSE stream; the connection is not reused. */
void  res_stream(Response *res, const char *content_type);

/* Maps an ErrorCode onto an HTTP status and emits the standard error body. */
int   http_status_for_error(ErrorCode code);

/* ---- router ---- */
typedef void (*RouteHandler)(Request *req, Response *res, void *user);

#define ROUTE_MAX 64
#define ROUTE_PATH_MAX 256

typedef struct {
    char  method[8];
    char  pattern[ROUTE_PATH_MAX];
    RouteHandler handler;
    void *user;
    /* Filled in by the router when the request matches. */
    char  params[8][128];
    char  param_names[8][64];
    int   param_count;
} Route;

typedef struct {
    Route routes[ROUTE_MAX];
    int   count;
} Router;

void  router_init(Router *r);
int   router_add(Router *r, const char *method, const char *pattern,
                 RouteHandler handler, void *user);
void  router_dispatch(Router *r, Request *req, Response *res);

/* ---- static file serving ---- */
typedef struct {
    char root[512];
    int  enabled;
} StaticRoot;

void static_set_root(StaticRoot *sr, const char *root);
int  static_serve(Request *req, Response *res);
/* Resolves a URL path against the static root. Returns 0 and fills `out` when
 * the file exists and stays inside the root. */
int  static_resolve(const StaticRoot *sr, const char *url_path, char *out, size_t len);

/* ---- server lifecycle ---- */
typedef struct {
    char host[128];
    int  port;
    char config_path[1024];
    char web_root[512];
    int  threads;
    int  quiet;
} ServeOptions;

int  server_run(const ServeOptions *so);

/* ---- API surface (implemented in api_*.c) ---- */
void api_register(Router *r);

/* ---- shared server state ---- */
typedef struct {
    ServeOptions opts;
    char config_path[1024];
    char usage_path[1024];
    char web_root[512];
    int  web_root_set;
    long started_at;
} ServerState;

extern ServerState g_server;

/* Reads the config under the global lock and copies it out. Returns 0 on
 * success. Callers get their own Config, so they can mutate it freely. */
int  server_load_config(Config *out);
int  server_config_path(char *out, size_t len);

/* Held across any read or write of process-global state the analysis path
 * touches: the language global, the usage log, the HTTP response cache, and
 * the config file. */
void server_lock(void);
void server_unlock(void);

/* Forces the presentation mode the daemon needs: no ANSI on stdout, no
 * spinners, but document colors still embedded for pdf/xlsx/html. */
void server_init_presentation(void);

/* Percent-decoding; returns the decoded length. Handles %XX and '+'.
 * With keep_slash set, %2F is left encoded so it cannot split a path
 * segment -- route parameters are decoded separately, per value. */
size_t url_decode(const char *in, char *out, size_t out_cap);
size_t url_decode_ex(const char *in, char *out, size_t out_cap, int keep_slash);

/* Format helpers shared by the API modules. */
const char *format_extension(const char *format);
const char *format_mime(const char *format);
int         format_is_known(const char *format);
int         format_supports_stdout(const char *format);

#endif /* GS_SERVER_H */