#include "http.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define DEFAULT_MODEL "gemini-2.0-flash"

static const char *DEFAULT_SYSTEM_PROMPT =
    "You are a code repository analyst. Analyze the given GitHub activity data "
    "and provide a concise analysis. Cover: overall activity level, main themes "
    "and areas of focus, notable changes or patterns. Keep your analysis "
    "concise (2-4 paragraphs).";

static char *join_url(const LLMConfig *cfg, const char *model) {
    size_t base_len = strlen(cfg->api_base);
    size_t model_len = strlen(model);
    size_t cap = base_len + model_len + 32;
    char *url = malloc(cap);
    if (!url) return NULL;

    snprintf(url, cap, "%s", cfg->api_base);
    if (strstr(url, ":generateContent")) {
        /* Replace the :generateContent suffix if the model in the URL differs
         * from the configured model. */
        char *suffix = strstr(url, ":generateContent");
        if (!strstr(url, model)) {
            *suffix = '\0';
            snprintf(url + strlen(url), cap - strlen(url), "%s:generateContent", model);
        }
        return url;
    }

    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') url[--len] = '\0';
    snprintf(url + len, cap - len, "/models/%s:generateContent", model);
    return url;
}

ErrorCode llm_gemini_generate(const LLMConfig *cfg, const char *prompt,
                              char *output, size_t output_len)
{
    int max_tokens = cfg->max_tokens > 0 ? cfg->max_tokens : 1024;
    float temperature = cfg->temperature > 0.0f ? cfg->temperature : 0.3f;
    const char *model = cfg->model[0] ? cfg->model : DEFAULT_MODEL;

    output[0] = '\0';

    cJSON *root = cJSON_CreateObject();
    if (!root) return ERR_MEMORY;

    cJSON *sys = cJSON_CreateObject();
    cJSON *sys_parts = cJSON_AddArrayToObject(sys, "parts");
    cJSON *sys_part = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_part, "text",
        cfg->system_prompt[0] ? cfg->system_prompt : DEFAULT_SYSTEM_PROMPT);
    cJSON_AddItemToArray(sys_parts, sys_part);
    cJSON_AddItemToObject(root, "systemInstruction", sys);

    cJSON *contents = cJSON_AddArrayToObject(root, "contents");
    cJSON *content = cJSON_CreateObject();
    cJSON_AddStringToObject(content, "role", "user");
    cJSON *parts = cJSON_AddArrayToObject(content, "parts");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", prompt);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToArray(contents, content);

    cJSON *config = cJSON_AddObjectToObject(root, "generationConfig");
    cJSON_AddNumberToObject(config, "maxOutputTokens", max_tokens);
    cJSON_AddNumberToObject(config, "temperature", temperature);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ERR_MEMORY;

    char *url = join_url(cfg, model);
    if (!url) { free(body); return ERR_MEMORY; }

    char auth[600];
    snprintf(auth, sizeof(auth), "x-goog-api-key: %s", cfg->api_key);
    const char *headers[] = { auth, NULL };

    LLMBuffer resp = {0};
    long http_code = 0;
    ErrorCode err = llm_http_post(cfg, url, headers, body, &resp, &http_code);
    free(url);
    free(body);
    if (err != GS_OK) return err;

    cJSON *json = cJSON_Parse(resp.data);
    llm_buffer_free(&resp);
    if (!json) return ERR_JSON_PARSE;

    cJSON *candidates = cJSON_GetObjectItem(json, "candidates");
    if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON *first = cJSON_GetArrayItem(candidates, 0);
        cJSON *content_obj = cJSON_GetObjectItem(first, "content");
        cJSON *parts_arr = cJSON_GetObjectItem(content_obj, "parts");
        if (cJSON_IsArray(parts_arr)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, parts_arr) {
                cJSON *text = cJSON_GetObjectItem(item, "text");
                if (text && cJSON_IsString(text)) {
                    snprintf(output, output_len, "%s", text->valuestring);
                    break;
                }
            }
        }
    }

    cJSON *usage_obj = cJSON_GetObjectItem(json, "usageMetadata");
    if (usage_obj && cJSON_IsObject(usage_obj)) {
        long long pt = 0, ct = 0, tt = 0;
        cJSON *v;
        if ((v = cJSON_GetObjectItem(usage_obj, "promptTokenCount")) && cJSON_IsNumber(v))
            pt = (long long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(usage_obj, "candidatesTokenCount")) && cJSON_IsNumber(v))
            ct = (long long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(usage_obj, "totalTokenCount")) && cJSON_IsNumber(v))
            tt = (long long)v->valuedouble;
        else
            tt = pt + ct;
        llm_record_usage(cfg, model, pt, ct, tt);
    }

    cJSON_Delete(json);

    if (output[0] == '\0') return ERR_LLM_API;
    return GS_OK;
}
