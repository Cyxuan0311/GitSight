#define _GNU_SOURCE

#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>
#include "gitsight/thread.h"

/* ---- buffered socket I/O ---- */

typedef struct {
    int fd;
    char *buf;
    size_t cap, len, pos;
} Conn;

static void conn_init(Conn *c, int fd) {
    c->fd = fd;
    c->cap = 8192;
    c->buf = malloc(c->cap);
    c->len = c->pos = 0;
    if (c->buf) c->buf[0] = '\0';
}

static void conn_free(Conn *c) {
    free(c->buf);
    c->buf = NULL;
    c->cap = c->len = c->pos = 0;
}

/* Reads until at least `need` unconsumed bytes are buffered, or EOF. */
static int conn_fill(Conn *c, size_t need) {
    if (c->len - c->pos >= need) return 1;
    if (!c->buf) return 0;

    /* Compact before growing so pos stays meaningful. */
    if (c->pos > 0) {
        memmove(c->buf, c->buf + c->pos, c->len - c->pos);
        c->len -= c->pos;
        c->pos = 0;
    }
    if (need + 1 > c->cap) {
        size_t ncap = (need + 1) * 2;
        char *nb = realloc(c->buf, ncap);
        if (!nb) return 0;
        c->buf = nb;
        c->cap = ncap;
    }
    while (c->len < need + 1) {
        ssize_t n = read(c->fd, c->buf + c->len, c->cap - c->len - 1);
        if (n > 0) {
            c->len += (size_t)n;
            c->buf[c->len] = '\0';
            continue;
        }
        if (n == 0) return (c->len - c->pos >= need);
        if (errno == EINTR) continue;
        return 0;
    }
    return 1;
}

/* Finds a CRLF-terminated line in the buffer. Returns 1 and advances when
 * found. */
static int conn_line(Conn *c, char *out, size_t out_cap) {
    for (;;) {
        char *start = c->buf + c->pos;
        size_t avail = c->len - c->pos;
        char *nl = memchr(start, '\n', avail);
        if (nl) {
            size_t line_len = (size_t)(nl - start);
            size_t copy = line_len < out_cap - 1 ? line_len : out_cap - 1;
            memcpy(out, start, copy);
            out[copy] = '\0';
            if (copy && out[copy - 1] == '\r') out[copy - 1] = '\0';
            c->pos += line_len + 1;
            return 1;
        }
        if (avail >= HTTP_MAX_HEADER) return 0;
        if (!conn_fill(c, avail + 1)) return 0;
    }
}

/* Reads exactly n bytes into out. */
static int conn_read_exact(Conn *c, char *out, size_t n) {
    size_t got = 0;
    while (got < n) {
        if (c->len - c->pos == 0 && !conn_fill(c, n - got)) break;
        size_t take = c->len - c->pos;
        if (take > n - got) take = n - got;
        memcpy(out + got, c->buf + c->pos, take);
        c->pos += take;
        got += take;
    }
    return got == n;
}

/* Writes everything, retrying on EINTR and short writes. Binary safe. */
static int conn_write_all(Conn *c, const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(c->fd, p, left);
        if (n > 0) {
            p += n;
            left -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return 0;
    }
    return 1;
}

/* ---- request parsing ---- */

static void conn_timeouts(int fd) {
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* Parses one request from `c` into `req`. Returns 1 on success, 0 on a clean
 * EOF before any bytes, -1 on a protocol error (already answered). */
static int parse_request(Conn *c, Request *req, Response *res) {
    char line[HTTP_MAX_URI + 256];

    if (!conn_line(c, line, sizeof(line))) {
        /* Either EOF or an over-long line; let the caller close. */
        return c->len > c->pos ? -1 : 0;
    }
    if (line[0] == '\0') return 1;   /* tolerate a stray CRLF from keep-alive */

    char *sp1 = strchr(line, ' ');
    if (!sp1) {
        res_error(res, 400, "bad_request", "malformed request line");
        return -1;
    }
    *sp1 = '\0';
    char *target = sp1 + 1;
    char *sp2 = strchr(target, ' ');
    if (!sp2) {
        res_error(res, 400, "bad_request", "malformed request line");
        return -1;
    }
    *sp2 = '\0';

    if (strlen(line) >= sizeof(req->method)) {
        res_error(res, 400, "bad_request", "unsupported HTTP method");
        return -1;
    }
    snprintf(req->method, sizeof(req->method), "%s", line);
    if (strlen(target) >= sizeof(req->target)) {
        res_error(res, 414, "uri_too_long", "request target is too long");
        return -1;
    }
    snprintf(req->target, sizeof(req->target), "%s", target);

    /* Split path and query, then percent-decode the path only. */
    char *qm = strchr(target, '?');
    if (qm) {
        *qm = '\0';
        snprintf(req->query, sizeof(req->query), "%s", qm + 1);
    } else {
        req->query[0] = '\0';
    }
    url_decode_ex(target, req->path, sizeof(req->path), 1);

    /* Headers. */
    req->header_count = 0;
    for (;;) {
        if (!conn_line(c, line, sizeof(line))) {
            res_error(res, 431, "headers_too_large", "request headers too large");
            return -1;
        }
        if (line[0] == '\0') break;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        if (req->header_count < 32) {
            snprintf(req->header_name[req->header_count],
                     sizeof(req->header_name[0]), "%s", line);
            req->header_val[req->header_count] = strdup(val);
            req->header_count++;
        }
    }

    const char *conn_hdr = req_header(req, "Connection");
    req->keep_alive = !(conn_hdr && strcasecmp(conn_hdr, "close") == 0);

    const char *expect = req_header(req, "Expect");
    if (expect && strcasecmp(expect, "100-continue") == 0)
        conn_write_all(c, "HTTP/1.1 100 Continue\r\n\r\n", 25);

    const char *te = req_header(req, "Transfer-Encoding");
    if (te && strcasecmp(te, "identity") != 0 && *te) {
        /* The bundled client always sends Content-Length; refuse chunked
         * bodies rather than mis-framing the connection. */
        res_error(res, 501, "not_implemented",
                  "chunked request bodies are not supported; send Content-Length");
        return -1;
    }

    const char *cl = req_header(req, "Content-Length");
    if (cl && *cl) {
        char *end = NULL;
        long long n = strtoll(cl, &end, 10);
        if (end == cl || n < 0) {
            res_error(res, 400, "bad_request", "malformed Content-Length");
            return -1;
        }
        if (n > HTTP_MAX_BODY) {
            res_error(res, 413, "payload_too_large", "request body too large");
            return -1;
        }
        if (n > 0) {
            req->body = malloc((size_t)n + 1);
            if (!req->body) {
                res_error(res, 500, "internal", "out of memory");
                return -1;
            }
            if (!conn_read_exact(c, req->body, (size_t)n)) {
                free(req->body);
                req->body = NULL;
                return 0;
            }
            req->body[n] = '\0';
            req->body_len = (size_t)n;
        }
    }
    return 1;
}

/* ---- response writing ---- */

static void write_response(Conn *c, Request *req, Response *res) {
    char head[1024];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: %s\r\n",
        res->status,
        res->status == 200 ? "OK" :
        res->status == 201 ? "Created" :
        res->status == 204 ? "No Content" :
        res->status == 400 ? "Bad Request" :
        res->status == 403 ? "Forbidden" :
        res->status == 404 ? "Not Found" :
        res->status == 405 ? "Method Not Allowed" :
        res->status == 409 ? "Conflict" :
        res->status == 413 ? "Payload Too Large" :
        res->status == 424 ? "Failed Dependency" :
        res->status == 431 ? "Request Header Fields Too Large" :
        res->status == 499 ? "Client Closed Request" :
        res->status == 501 ? "Not Implemented" :
        res->status == 502 ? "Bad Gateway" :
        res->status == 503 ? "Service Unavailable" : "Internal Server Error",
        res->content_type,
        res->body ? res->body_len : (size_t)0,
        res->cache_control[0] ? res->cache_control : "no-store");

    for (int i = 0; i < res->header_count && n > 0 && n < (int)sizeof(head) - 128; i++)
        n += snprintf(head + n, sizeof(head) - (size_t)n, "%s\r\n", res->headers[i]);

    n += snprintf(head + n, sizeof(head) - (size_t)n,
                  "Connection: %s\r\n\r\n", req->keep_alive ? "keep-alive" : "close");

    conn_write_all(c, head, (size_t)n);
    /* HEAD advertises the length a GET would return but sends no body; writing
     * one would leave the next response on a kept-alive connection unframed. */
    if (strcmp(req->method, "HEAD") == 0) return;
    if (res->body && res->body_len)
        conn_write_all(c, res->body, res->body_len);
}

/* ---- connection handling ---- */

static unsigned long long g_conn_counter = 0;

static void *connection_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    Conn c;
    conn_init(&c, fd);
    if (!c.buf) {
        close(fd);
        return NULL;
    }
    conn_timeouts(fd);

    for (;;) {
        Request req;
        Response res;
        req_init(&req, fd);
        req.id = ++g_conn_counter;
        res_init(&res);

        int rc = parse_request(&c, &req, &res);
        if (rc <= 0) {
            if (rc < 0 && res.body) write_response(&c, &req, &res);
            res_free(&res);
            req_free(&req);
            break;
        }

        /* CORS is permissive because the server binds to loopback and holds no
         * credentials; the Vite dev server runs on another port. */
        res_header(&res, "Access-Control-Allow-Origin: *");
        res_header(&res, "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS");
        res_header(&res, "Access-Control-Allow-Headers: Content-Type");

        if (strcmp(req.method, "OPTIONS") == 0) {
            res.status = 204;
            req.keep_alive = 0;
            write_response(&c, &req, &res);
        } else {
            Router router;
            router_init(&router);
            api_register(&router);
            if (strncmp(req.path, "/api/", 5) == 0) {
                router_dispatch(&router, &req, &res);
            } else if (strcmp(req.method, "GET") == 0 || strcmp(req.method, "HEAD") == 0) {
                if (!static_serve(&req, &res))
                    res_error(&res, 404, "not_found",
                              "no web console built; run the frontend build first");
            } else {
                res_error(&res, 404, "not_found", "no such endpoint");
            }
            /* A streaming handler has already written the response itself. */
            if (!res.streaming) write_response(&c, &req, &res);
        }

        int keep = req.keep_alive && !res.streaming;
        res_free(&res);
        req_free(&req);
        if (!keep) break;
    }

    conn_free(&c);
    close(fd);
    return NULL;
}

/* ---- listener ---- */

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

/* Installs a handler that is allowed to interrupt a blocking call.
 *
 * signal() on glibc registers with SA_RESTART, which restarts accept() behind
 * the handler's back: g_stop is set but accept() never returns, so the loop
 * never re-tests it and Ctrl+C leaves the daemon running until it is killed.
 * sigaction() without SA_RESTART makes accept() fail with EINTR instead, which
 * the accept loop already treats as "try again". */
static void install_stop_handler(int sig) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* deliberately no SA_RESTART */
    sigaction(sig, &sa, NULL);
}

int server_run(const ServeOptions *so) {
    signal(SIGPIPE, SIG_IGN);
    install_stop_handler(SIGINT);
    install_stop_handler(SIGTERM);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", so->port);
    int gai = getaddrinfo(so->host, portbuf, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "Error: cannot resolve %s:%d (%s)\n",
                so->host, so->port, gai_strerror(gai));
        return 1;
    }

    int lfd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        lfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (lfd < 0) continue;
        int one = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (ai->ai_family == AF_INET6)
            setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
        if (bind(lfd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(lfd);
        lfd = -1;
    }
    freeaddrinfo(res);

    if (lfd < 0) {
        fprintf(stderr, "Error: cannot bind %s:%d (%s)\n",
                so->host, so->port, strerror(errno));
        return 1;
    }
    if (listen(lfd, 64) != 0) {
        fprintf(stderr, "Error: listen failed (%s)\n", strerror(errno));
        close(lfd);
        return 1;
    }

    char bound[128] = "";
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getsockname(lfd, (struct sockaddr *)&ss, &sl) == 0) {
        if (ss.ss_family == AF_INET)
            inet_ntop(AF_INET, &((struct sockaddr_in *)&ss)->sin_addr, bound, sizeof(bound));
        else
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&ss)->sin6_addr, bound, sizeof(bound));
    }

    printf("GitSight web console on http://%s:%d\n", bound[0] ? bound : so->host, so->port);
    printf("Config: %s\n", g_server.config_path);
    fflush(stdout);

    while (!g_stop) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        int one = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        /* One thread per connection: the daemon is single-user and the work is
         * network bound, so this stays well under any practical connection
         * count while keeping handlers free to block on SSE streams. */
        pthread_t th;
        if (gs_thread_create(&th, connection_thread, (void *)(intptr_t)cfd) == 0) {
            pthread_detach(th);
        } else {
            close(cfd);
        }
    }

    close(lfd);
    printf("\nShutting down.\n");
    return 0;
}