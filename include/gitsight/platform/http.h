#ifndef GITSIGHT_PLATFORM_HTTP_H
#define GITSIGHT_PLATFORM_HTTP_H

#include <stddef.h>
#include "gitsight/error.h"

typedef struct {
    char  *data;
    size_t size;
} HttpBuffer;

void  http_buffer_init(HttpBuffer *b);
void  http_buffer_free(HttpBuffer *b);

typedef struct {
    long http_code;
    char next_page[1024];
    long next_page_num;
    long rate_limit_remaining;
    long rate_limit_reset;
    long retry_after;
    int  has_rate_limit;
    int  has_retry_after;
} HttpResult;

typedef struct {
    char auth_header[600];
    char proxy[512];
    char accept[128];
    long timeout_secs;
    int  max_pages;
    int  use_cache;
    char cache_dir[512];
    long cache_ttl;
} HttpClient;

void http_client_init(HttpClient *hc, const char *auth_header, const char *proxy,
                      const char *accept, long timeout_secs, int max_pages);

/* Enable/disable the disk response cache process-wide (off via --no-cache).
   Defaults to enabled. Must be called before any fetch begins. */
void http_set_cache_enabled(int enabled);

/* Reports where cached responses live, how long they are kept, and whether the
   cache is currently on. Any output pointer may be NULL. */
void http_cache_info(char *dir, size_t len, long *ttl, int *enabled);

/* Snapshot of the last API quota seen in a response header (process-wide).
   remaining = -1 when not yet known; reset_at is an epoch value. */
void http_quota(long *remaining, long *reset_at);

/**
 * What the process has spent on the network since it started.
 *
 * A run that feels slow is either waiting on the network or working through
 * the items, and the two need very different fixes. These counters say which:
 * how many requests went out, how many the cache answered, how often a
 * request had to be retried, and how many bytes came back.
 */
typedef struct {
    long requests;      /* Responses fetched from the network. */
    long pages;         /* Pages parsed, cache hits included. */
    long cache_hits;    /* Pages answered from the disk cache. */
    long cache_misses;  /* Pages that had to be fetched. */
    long retries;       /* Extra attempts after a transient failure. */
    long bytes;         /* Bytes received over the network. */
} HttpStats;

/* The counters, read as one consistent snapshot. */
void http_stats(HttpStats *out);

/* GET a single URL. Retries transient failures and rate limits internally.
   Returns GS_OK on HTTP 200 (result/data + out filled).
   Returns ERR_HTTP_STATUS on a final non-success HTTP code (out->http_code set).
   Returns ERR_NETWORK / ERR_CURL_INIT on transport failure after retries. */
ErrorCode http_get(const HttpClient *hc, const char *url,
                   HttpBuffer *result, HttpResult *out);

/* Parse callback for a single JSON page. Return 0 to continue to the next page,
   1 to stop early (e.g. remaining items are outside the requested window). */
typedef int (*HttpPageFn)(void *userdata, const char *body, const HttpResult *res);

/* GET all pages following pagination metadata, up to max_pages. Two styles
   are supported: the Link rel="next" header (GitHub/Gitea/Forgejo) and the
   X-Next-Page header (GitLab), which store a full URL or a page number in
   HttpResult.next_page / HttpResult.next_page_num respectively.
   If last_http is non-NULL, it receives the HTTP code of the last response
   (useful for building error messages when an error is returned). */
ErrorCode http_get_paginated(const HttpClient *hc, const char *url,
                             HttpPageFn cb, void *userdata, long *last_http);

/* URL-encode a string for use in query parameters.
    Returns the number of bytes written (excluding the null terminator),
    or -1 if the output buffer was too small. */
int url_encode(const char *src, char *dst, size_t dst_len);

#endif
