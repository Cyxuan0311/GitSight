#include "driver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

void gs_build_since_date(int since_days, char *buf, size_t len) {
    time_t now = time(NULL);
    time_t since_ts = now - (time_t)since_days * 86400;
    struct tm *tm = gmtime(&since_ts);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", tm);
}

void gs_build_since_day(int since_days, char *buf, size_t len) {
    time_t now = time(NULL);
    time_t since_ts = now - (time_t)since_days * 86400;
    struct tm *tm = gmtime(&since_ts);
    strftime(buf, len, "%Y-%m-%d", tm);
}

int gs_date_in_range(const char *date_str, const char *since_str) {
    if (!date_str || date_str[0] == '\0') return 0;
    return strcmp(date_str, since_str) >= 0;
}

void gs_flatten_message(const char *src, char *dst, int max) {
    int i = 0;
    while (*src && i < max) {
        if (*src == '\n') {
            *dst++ = ' ';
            src++;
            while (*src == '\n') src++;
        } else {
            *dst++ = *src++;
        }
        i++;
    }
    *dst = '\0';
}

void gs_extract_labels(cJSON *labels_json, char *buf, size_t len) {
    buf[0] = '\0';
    if (!cJSON_IsArray(labels_json)) return;

    int first = 1;
    cJSON *label;
    cJSON_ArrayForEach(label, labels_json) {
        const char *name = NULL;
        if (cJSON_IsString(label)) {
            name = label->valuestring;
        } else if (cJSON_IsObject(label)) {
            cJSON *n = cJSON_GetObjectItem(label, "name");
            if (n && cJSON_IsString(n)) name = n->valuestring;
            if (!name) {
                n = cJSON_GetObjectItem(label, "title");
                if (n && cJSON_IsString(n)) name = n->valuestring;
            }
        }
        if (name) {
            if (!first) strncat(buf, ", ", len - strlen(buf) - 1);
            strncat(buf, name, len - strlen(buf) - 1);
            first = 0;
        }
    }
}

/* Parse a {"C": 12345, ...} languages map into a LangEntry vector.
   scale converts the API number into a comparable weight
   (GitHub/Gitea byte counts: 1; GitLab percentage shares: 1e5). */
void gs_parse_languages_object(cJSON *json, Vector *langs, double scale) {
    if (!json || !cJSON_IsObject(json)) return;
    cJSON *ent = NULL;
    cJSON_ArrayForEach(ent, json) {
        if (!ent->string || !cJSON_IsNumber(ent)) continue;
        LangEntry *le = lang_create();
        if (!le) break;
        snprintf(le->name, sizeof(le->name), "%s", ent->string);
        double v = ent->valuedouble * scale;
        le->bytes = (long long)(v > 0 ? v : 0);
        vector_push(langs, le);
    }
}

static void set_http_error(const DriverBase *b, long http_code) {
    if (!b->error_buf) return;
    if (http_code == 401)
        snprintf(b->error_buf, b->error_len,
            "HTTP 401 (bad credentials). Check your %s.", b->token_check);
    else if (http_code == 403)
        snprintf(b->error_buf, b->error_len,
            "HTTP 403 (rate limited). Set %s in config or env.", b->token_set);
    else if (http_code == 404)
        snprintf(b->error_buf, b->error_len,
            "HTTP 404. %s '%s/%s' not found.", b->repo_noun, b->owner, b->repo);
    else
        snprintf(b->error_buf, b->error_len, "HTTP %ld error.", http_code);
}

ErrorCode gs_fetch_paginated(const DriverBase *base, const char *auth_header,
                             const char *url, HttpPageFn cb, void *userdata,
                             int tolerate_404) {
    HttpClient hc;
    http_client_init(&hc, auth_header, base->proxy, base->accept,
                     base->timeout_secs, base->max_pages);
    hc.use_cache = base->use_cache;

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, cb, userdata, &last_http);
    http_client_free(&hc);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) {
            if (tolerate_404 && last_http == 404) return GS_OK;
            set_http_error(base, last_http);
        }
        return err;
    }
    return GS_OK;
}

ErrorCode gs_fetch_json(const DriverBase *base, const char *auth_header,
                        const char *url, cJSON **out) {
    HttpClient hc;
    http_client_init(&hc, auth_header, base->proxy, base->accept,
                     base->timeout_secs, base->max_pages);
    hc.use_cache = base->use_cache;

    HttpBuffer buf;
    HttpResult res;
    http_buffer_init(&buf);
    ErrorCode err = http_get(&hc, url, &buf, &res);
    http_client_free(&hc);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_http_error(base, res.http_code);
        http_buffer_free(&buf);
        return err;
    }

    cJSON *json = cJSON_Parse(buf.data);
    http_buffer_free(&buf);
    if (!json) return ERR_JSON_PARSE;
    *out = json;
    return GS_OK;
}

ErrorCode gs_fetch_json_code(const DriverBase *base, const char *auth_header,
                             const char *url, cJSON **out, long *http_code) {
    HttpClient hc;
    http_client_init(&hc, auth_header, base->proxy, base->accept,
                     base->timeout_secs, base->max_pages);
    hc.use_cache = base->use_cache;

    HttpBuffer buf;
    HttpResult res;
    http_buffer_init(&buf);
    ErrorCode err = http_get(&hc, url, &buf, &res);
    if (http_code) *http_code = res.http_code;
    http_client_free(&hc);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_http_error(base, res.http_code);
        http_buffer_free(&buf);
        return err;
    }

    cJSON *json = cJSON_Parse(buf.data);
    http_buffer_free(&buf);
    if (!json) return ERR_JSON_PARSE;
    *out = json;
    return GS_OK;
}
