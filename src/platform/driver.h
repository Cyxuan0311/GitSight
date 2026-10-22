#ifndef GS_PLATFORM_DRIVER_H
#define GS_PLATFORM_DRIVER_H

/* Shared fetch skeleton for the platform drivers (github-compatible and
   gitlab). Each driver keeps only its URL building and JSON field mapping;
   pagination, error messages and small parse helpers live here. */

#include "gitsight/error.h"
#include "gitsight/models.h"
#include "gitsight/container.h"
#include "gitsight/platform/http.h"
#include <cJSON.h>
#include <stddef.h>

typedef struct {
    Vector *vec;
    /* The exact start of the window, used to filter the items. */
    char since_str[MAX_DATE_LEN];
    /* The same start as a plain date, used in the request URL. A timestamp
     * there would change the URL every second, and the URL is the cache
     * key -- so the cache would never answer and every run would leave a
     * trail of files nobody reads. */
    char since_day[MAX_DATE_LEN];
} PageCtx;

/* Identity + HTTP settings of the calling driver, filled per fetch call. */
typedef struct {
    const char *api_base;
    const char *proxy;
    const char *accept;
    long timeout_secs;
    int  max_pages;
    /* Whether this fetch may use the disk cache. A run that asked to bypass
     * the cache has to bypass it for its own requests only, so the flag
     * travels with the client rather than switching the cache off for the
     * whole process. */
    int  use_cache;
    const char *owner;
    const char *repo;
    char *error_buf;
    size_t error_len;
    /* Wording knobs for the 401/403/404 messages, since the platforms call
       the credential and the container differently. */
    const char *token_check;   /* "Check your <token_check>." */
    const char *token_set;     /* "Set <token_set> in config or env." */
    const char *repo_noun;     /* "<repo_noun> 'owner/repo' not found." */
} DriverBase;

void gs_build_since_date(int since_days, char *buf, size_t len);
void gs_build_since_day(int since_days, char *buf, size_t len);
int  gs_date_in_range(const char *date_str, const char *since_str);

/* Copy src into dst[0..max), collapsing runs of newlines into one space. */
void gs_flatten_message(const char *src, char *dst, int max);

/* Join label names from a JSON array of strings or objects ("name"/"title"). */
void gs_extract_labels(cJSON *labels_json, char *buf, size_t len);

/* Parse a {"C": 12345, ...} languages map into a LangEntry vector. */
void gs_parse_languages_object(cJSON *json, Vector *langs, double scale);

/* Paginated GET parsed page-by-page with cb. On ERR_HTTP_STATUS a driver
   error message is written to base->error_buf, unless tolerate_404 (a 404
   means "no such resource", reported as success). */
ErrorCode gs_fetch_paginated(const DriverBase *base, const char *auth_header,
                             const char *url, HttpPageFn cb, void *userdata,
                             int tolerate_404);

/* Single GET returning a parsed JSON document (caller cJSON_Delete's *out). */
ErrorCode gs_fetch_json(const DriverBase *base, const char *auth_header,
                        const char *url, cJSON **out);

#endif /* GS_PLATFORM_DRIVER_H */
