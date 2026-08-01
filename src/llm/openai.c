#include "http.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define DEFAULT_MODEL "gpt-4o-mini"

static const char *DEFAULT_SYSTEM_PROMPT =
    "You are a code repository analyst. Analyze the given GitHub activity data "
    "and provide a concise analysis. Cover: overall activity level, main themes "
    "and areas of focus, notable changes or patterns. Keep your analysis "
    "concise (2-4 paragraphs).";

ErrorCode llm_openai_generate(const LLMConfig *cfg, const char *prompt,
                              char *output, size_t output_len)
{
    int max_tokens = cfg->max_tokens > 0 ? cfg->max_tokens : 1024;
    float temperature = cfg->temperature > 0.0f ? cfg->temperature : 0.3f;
    const char *model = cfg->model[0] ? cfg->model : DEFAULT_MODEL;

    output[0] = '\0';

    char url[1024];
    snprintf(url, sizeof(url), "%s", cfg->api_base);
    if (!strstr(url, "/chat/completions")) {
        size_t len = strlen(url);
        while (len > 0 && url[len - 1] == '/') url[--len] = '\0';
        snprintf(url + len, sizeof(url) - len, "/chat/completions");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return ERR_MEMORY;
    cJSON_AddStringToObject(root, "model", model);

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
    cJSON_Delete(root);
    if (!body) return ERR_MEMORY;

    char auth[600];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->api_key);
    const char *headers[] = { auth, NULL };

    LLMBuffer resp = {0};
    long http_code = 0;
    ErrorCode err = llm_http_post(cfg, url, headers, body, &resp, &http_code);
    free(body);
    if (err != GS_OK) return err;

    cJSON *json = cJSON_Parse(resp.data);
    llm_buffer_free(&resp);
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

    cJSON *usage_obj = cJSON_GetObjectItem(json, "usage");
    if (usage_obj && cJSON_IsObject(usage_obj)) {
        long long pt = 0, ct = 0, tt = 0;
        cJSON *v;
        if ((v = cJSON_GetObjectItem(usage_obj, "prompt_tokens")) && cJSON_IsNumber(v))
            pt = (long long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(usage_obj, "completion_tokens")) && cJSON_IsNumber(v))
            ct = (long long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(usage_obj, "total_tokens")) && cJSON_IsNumber(v))
            tt = (long long)v->valuedouble;
        llm_record_usage(cfg, model, pt, ct, tt);
    }

    cJSON_Delete(json);

    if (output[0] == '\0') return ERR_LLM_API;
    return GS_OK;
}
