#include "server.h"

#include "gitsight/completions.h"
#include "gitsight/usage.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* GET /api/usage?days=14
 *
 * One request covers both groupings the CLI can print, because the browser
 * dashboard shows a daily bar chart and a per-provider breakdown side by side
 * and would otherwise have to issue two round trips. */
static void handle_usage(Request *req, Response *res, void *user) {
    (void)user;
    char buf[64];
    int days = 14;
    if (req_query(req, "days", buf, sizeof(buf)) && buf[0]) {
        days = atoi(buf);
        if (days < 1) days = 1;
        if (days > 365) days = 365;
    }

    UsageRecord *recs = NULL;
    long n = usage_load(NULL, &recs);
    if (n < 0) n = 0;

    long long calls = 0, prompt = 0, completion = 0, total = 0;
    if (recs) usage_totals(recs, n, &calls, &prompt, &completion, &total);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "records", (double)n);
    cJSON_AddNumberToObject(root, "days", days);
    cJSON_AddStringToObject(root, "path", g_server.usage_path);

    cJSON *totals = cJSON_CreateObject();
    cJSON_AddNumberToObject(totals, "calls", (double)calls);
    cJSON_AddNumberToObject(totals, "prompt_tokens", (double)prompt);
    cJSON_AddNumberToObject(totals, "completion_tokens", (double)completion);
    cJSON_AddNumberToObject(totals, "total_tokens", (double)total);
    cJSON_AddItemToObject(root, "total", totals);

    UsageDay *days_out = calloc((size_t)days, sizeof(UsageDay));
    UsageGroup groups[USAGE_MAX_GROUPS];
    if (days_out) {
        /* Always bucket, even with no records at all: usage_by_day labels the
         * window itself, so skipping it would report every day as the epoch. */
        usage_by_day(recs, n, days, days_out);
        int ng = 0;
        if (recs) ng = usage_by_llm(recs, n, groups, USAGE_MAX_GROUPS);

        cJSON *by_day = cJSON_CreateArray();
        for (int i = 0; i < days; i++) {
            cJSON *o = cJSON_CreateObject();
            char label[16];
            time_t t = (time_t)days_out[i].day_start;
            struct tm tmv;
            gmtime_r(&t, &tmv);
            strftime(label, sizeof(label), "%Y-%m-%d", &tmv);
            cJSON_AddStringToObject(o, "day", label);
            cJSON_AddNumberToObject(o, "calls", (double)days_out[i].calls);
            cJSON_AddNumberToObject(o, "prompt_tokens", (double)days_out[i].prompt_tokens);
            cJSON_AddNumberToObject(o, "completion_tokens", (double)days_out[i].completion_tokens);
            cJSON_AddNumberToObject(o, "total_tokens", (double)days_out[i].total_tokens);
            cJSON_AddItemToArray(by_day, o);
        }
        cJSON_AddItemToObject(root, "by_day", by_day);

        cJSON *by_llm = cJSON_CreateArray();
        for (int i = 0; i < ng; i++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "name", groups[i].name);
            cJSON_AddNumberToObject(o, "calls", (double)groups[i].calls);
            cJSON_AddNumberToObject(o, "prompt_tokens", (double)groups[i].prompt_tokens);
            cJSON_AddNumberToObject(o, "completion_tokens", (double)groups[i].completion_tokens);
            cJSON_AddNumberToObject(o, "total_tokens", (double)groups[i].total_tokens);
            cJSON_AddItemToArray(by_llm, o);
        }
        cJSON_AddItemToObject(root, "by_llm", by_llm);
    }
    free(days_out);
    usage_free(recs);
    res_json_obj(res, 200, root);
}

/* GET /api/usage/records?limit=200 -- newest first, for a data table. */
static void handle_usage_records(Request *req, Response *res, void *user) {
    (void)user;
    char buf[64];
    int limit = 200;
    if (req_query(req, "limit", buf, sizeof(buf)) && buf[0]) {
        limit = atoi(buf);
        if (limit < 1) limit = 1;
        if (limit > 2000) limit = 2000;
    }

    UsageRecord *recs = NULL;
    long n = usage_load(NULL, &recs);
    if (n < 0) n = 0;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total", (double)n);
    cJSON_AddNumberToObject(root, "returned",
                            (double)(n < limit ? n : limit));
    cJSON *arr = cJSON_AddArrayToObject(root, "records");
    for (long i = n - 1; i >= 0 && n - i <= limit; i--) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "timestamp", (double)recs[i].timestamp);
        char iso[32];
        time_t t = (time_t)recs[i].timestamp;
        struct tm tmv;
        gmtime_r(&t, &tmv);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tmv);
        cJSON_AddStringToObject(o, "at", iso);
        cJSON_AddStringToObject(o, "llm", recs[i].llm);
        cJSON_AddStringToObject(o, "model", recs[i].model);
        cJSON_AddNumberToObject(o, "prompt_tokens", (double)recs[i].prompt_tokens);
        cJSON_AddNumberToObject(o, "completion_tokens", (double)recs[i].completion_tokens);
        cJSON_AddNumberToObject(o, "total_tokens", (double)recs[i].total_tokens);
        cJSON_AddItemToArray(arr, o);
    }
    usage_free(recs);
    res_json_obj(res, 200, root);
}

/* DELETE /api/usage */
static void handle_usage_clear(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    server_lock();
    ErrorCode e = usage_clear(NULL);
    server_unlock();
    if (e != GS_OK) {
        res_error_code(res, e, g_server.usage_path);
        return;
    }
    res_no_content(res);
}

/* GET /api/completion/:shell -- the same scripts `gs completion <shell>`
 * prints, byte for byte, since they are the very strings compiled into the
 * binary. */
static void handle_completion(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing shell name");
        return;
    }
    char shell[32];
    url_decode(rt->params[0], shell, sizeof(shell));

    const char *script = NULL;
    if (strcmp(shell, "bash") == 0)      script = gs_completion_bash;
    else if (strcmp(shell, "zsh") == 0)  script = gs_completion_zsh;
    else if (strcmp(shell, "fish") == 0) script = gs_completion_fish;

    if (!script) {
        res_error(res, 404, "unsupported_shell",
                  "available shells are bash, zsh and fish");
        return;
    }
    res_text(res, 200, "text/plain; charset=utf-8", script);
}

void api_register_usage(Router *r) {
    router_add(r, "GET",    "/api/usage", handle_usage, NULL);
    router_add(r, "DELETE", "/api/usage", handle_usage_clear, NULL);
    router_add(r, "GET",    "/api/usage/records", handle_usage_records, NULL);
    router_add(r, "GET",    "/api/completion/:shell", handle_completion, NULL);
}