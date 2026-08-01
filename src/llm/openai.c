#include "gitsight/llm/client.h"
#include "gitsight/usage.h"
#include <cJSON.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define LLM_MAX_RETRIES 3

static const char *DEFAULT_SYSTEM_PROMPT =
    "You are a code repository analyst. Analyze the given GitHub activity data "
    "and provide a concise analysis. Cover: overall activity level, main themes "
    "and areas of focus, notable changes or patterns. Keep your analysis "
    "concise (2-4 paragraphs).";

struct MemoryBuf {
    char *data;
    size_t size;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct MemoryBuf *buf = (struct MemoryBuf *)userdata;
    size_t total = size * nmemb;
    char *new_data = realloc(buf->data, buf->size + total + 1);
    if (!new_data) return 0;
    buf->data = new_data;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
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

ErrorCode llm_generate(const LLMConfig *cfg, const char *prompt,
                       char *output, size_t output_len)
{
    if (!cfg->api_key[0] || !cfg->api_base[0]) {
        return ERR_LLM_CONFIG;
    }

    int timeout_secs = cfg->timeout_secs > 0 ? cfg->timeout_secs : 60;
    int max_tokens   = cfg->max_tokens > 0 ? cfg->max_tokens : 1024;
    float temperature = cfg->temperature > 0.0f ? cfg->temperature : 0.3f;

    output[0] = '\0';

    char url[1024];
    snprintf(url, sizeof(url), "%s", cfg->api_base);
    if (!strstr(url, "/chat/completions")) {
        size_t len = strlen(url);
        while (len > 0 && url[len - 1] == '/') url[--len] = '\0';
        snprintf(url + len, sizeof(url) - len, "/chat/completions");
    }

    int attempt = 0;
    for (;;) {
        cJSON *root = cJSON_CreateObject();
        if (cfg->model[0]) {
            cJSON_AddStringToObject(root, "model", cfg->model);
        } else {
            cJSON_AddStringToObject(root, "model", "gpt-4o-mini");
        }

        cJSON *messages = cJSON_AddArrayToObject(root, "messages");

        cJSON *sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content",
            cfg->system_prompt[0] ? cfg->system_prompt : DEFAULT_SYSTEM_PROMPT);
        cJSON_AddItemToArray(messages, sys_msg);

        cJSON *user_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg, "role", "user");
        cJSON_AddStringToObject(user_msg, "content", prompt);
        cJSON_AddItemToArray(messages, user_msg);

        cJSON_AddNumberToObject(root, "max_tokens", max_tokens);
        cJSON_AddNumberToObject(root, "temperature", temperature);

        char *body = cJSON_PrintUnformatted(root);
        if (!body) { cJSON_Delete(root); return ERR_MEMORY; }

        CURL *curl = curl_easy_init();
        if (!curl) { free(body); cJSON_Delete(root); return ERR_CURL_INIT; }

        struct MemoryBuf resp = {0};
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        char auth[600];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->api_key);
        headers = curl_slist_append(headers, auth);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_secs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(body);
        cJSON_Delete(root);

        if (res != CURLE_OK) {
            if (attempt < LLM_MAX_RETRIES) {
                attempt++;
                sleep_secs(1L << (attempt - 1));
                free(resp.data);
                continue;
            }
            free(resp.data);
            return ERR_NETWORK;
        }

        if (is_transient_http(http_code) && attempt < LLM_MAX_RETRIES) {
            attempt++;
            sleep_secs(1L << (attempt - 1));
            free(resp.data);
            continue;
        }

        if (http_code != 200) {
            fprintf(stderr, "LLM API returned HTTP %ld: %s\n",
                    http_code, resp.data ? resp.data : "unknown");
            free(resp.data);
            return ERR_LLM_API;
        }

        cJSON *json = cJSON_Parse(resp.data);
        free(resp.data);
        if (!json) return ERR_JSON_PARSE;

        cJSON *choices = cJSON_GetObjectItem(json, "choices");
        if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
            cJSON_Delete(json);
            return ERR_LLM_API;
        }

        cJSON *first = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(first, "message");
        cJSON *content = cJSON_GetObjectItem(message, "content");

        if (content && cJSON_IsString(content)) {
            snprintf(output, output_len, "%s", content->valuestring);
        }

        /* Persist usage (tokens per call) to the local usage log. */
        cJSON *usage_obj = cJSON_GetObjectItem(json, "usage");
        if (usage_obj && cJSON_IsObject(usage_obj)) {
            UsageRecord rec;
            memset(&rec, 0, sizeof(rec));
            rec.timestamp = (long long)time(NULL);
            snprintf(rec.llm, sizeof(rec.llm), "%s", cfg->name);
            snprintf(rec.model, sizeof(rec.model), "%.127s",
                     cfg->model[0] ? cfg->model : "gpt-4o-mini");
            cJSON *v;
            if ((v = cJSON_GetObjectItem(usage_obj, "prompt_tokens")) && cJSON_IsNumber(v))
                rec.prompt_tokens = (long long)v->valuedouble;
            if ((v = cJSON_GetObjectItem(usage_obj, "completion_tokens")) && cJSON_IsNumber(v))
                rec.completion_tokens = (long long)v->valuedouble;
            if ((v = cJSON_GetObjectItem(usage_obj, "total_tokens")) && cJSON_IsNumber(v))
                rec.total_tokens = (long long)v->valuedouble;
            const char *no_usage = getenv("GS_NO_USAGE");
            if (!(no_usage && no_usage[0]))
                usage_record(NULL, &rec);
        }

        cJSON_Delete(json);

        if (output[0] == '\0') return ERR_LLM_API;
        return GS_OK;
    }
}
