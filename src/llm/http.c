#include "http.h"
#include "gitsight/usage.h"
#include "gitsight/cancel.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define LLM_MAX_RETRIES 3

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    LLMBuffer *buf = (LLMBuffer *)userdata;
    size_t total = size * nmemb;
    char *new_data = realloc(buf->data, buf->size + total + 1);
    if (!new_data) return 0;
    buf->data = new_data;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

void llm_buffer_free(LLMBuffer *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

static void sleep_secs(long secs) {
    if (secs <= 0) return;
    struct timespec ts;
    ts.tv_sec = secs;
    ts.tv_nsec = 0;
    while (nanosleep(&ts, &ts) == -1) { /* EINTR retry */ }
}

static int is_transient_http(long code) {
    return code == 429 || code == 500 || code == 502 || code == 503 || code == 504;
}

ErrorCode llm_http_post(const LLMConfig *cfg, const char *url,
                        const char *const headers[],
                        const char *body, LLMBuffer *out,
                        long *out_http_code)
{
    int timeout_secs = cfg->timeout_secs > 0 ? cfg->timeout_secs : 60;

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    for (int i = 0; headers && headers[i]; i++)
        hdrs = curl_slist_append(hdrs, headers[i]);

    int attempt = 0;
    for (;;) {
        llm_buffer_free(out);

        CURL *curl = curl_easy_init();
        if (!curl) {
            curl_slist_free_all(hdrs);
            return ERR_CURL_INIT;
        }

        gs_cancel_attach(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_secs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (out_http_code) *out_http_code = http_code;

        if (res != CURLE_OK) {
            if (res == CURLE_ABORTED_BY_CALLBACK || gs_cancel_requested()) {
                curl_slist_free_all(hdrs);
                llm_buffer_free(out);
                return ERR_CANCELLED;
            }
            if (attempt < LLM_MAX_RETRIES) {
                attempt++;
                sleep_secs(1L << (attempt - 1));
                continue;
            }
            curl_slist_free_all(hdrs);
            llm_buffer_free(out);
            return ERR_NETWORK;
        }

        if (is_transient_http(http_code) && attempt < LLM_MAX_RETRIES) {
            attempt++;
            sleep_secs(1L << (attempt - 1));
            continue;
        }

        if (http_code < 200 || http_code >= 300) {
            fprintf(stderr, "LLM API returned HTTP %ld: %s\n",
                    http_code, out->data ? out->data : "unknown");
            curl_slist_free_all(hdrs);
            llm_buffer_free(out);
            return ERR_LLM_API;
        }

        curl_slist_free_all(hdrs);
        return GS_OK;
    }
}

void llm_record_usage(const LLMConfig *cfg, const char *model,
                      long long prompt_tokens, long long completion_tokens,
                      long long total_tokens)
{
    const char *no_usage = getenv("GS_NO_USAGE");
    if (no_usage && no_usage[0]) return;

    UsageRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp = (long long)time(NULL);
    snprintf(rec.llm, sizeof(rec.llm), "%s", cfg->name);
    snprintf(rec.model, sizeof(rec.model), "%.127s",
             model && model[0] ? model : "unknown");
    rec.prompt_tokens = prompt_tokens;
    rec.completion_tokens = completion_tokens;
    rec.total_tokens = total_tokens;
    usage_record(NULL, &rec);
}
