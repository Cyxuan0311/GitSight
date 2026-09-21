#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/color.h"
#include "gitsight/chart.h"
#include "gitsight/error.h"
#include "gitsight/usage.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int usage_day_is_empty(const UsageDay *d) {
    return d->calls == 0 && d->total_tokens == 0;
}

int cmd_usage(const Options *opts) {
    char cfg_path[1024];
    get_config_path(cfg_path, sizeof(cfg_path), opts);

    char usage_path[1024];
    usage_path_from_config(cfg_path, usage_path, sizeof(usage_path));
    usage_set_file(usage_path);

    char eff_path[1024];
    usage_file_path(eff_path, sizeof(eff_path));

    color_init(opts->color_mode >= 0 ? opts->color_mode : 0);

    if (opts->usage_clear) {
        ErrorCode e = usage_clear(NULL);
        if (e != GS_OK) {
            fprintf(stderr, "Failed to clear usage log: %s\n", gs_error_string(e));
            return 1;
        }
        fprintf(stderr, "Usage log cleared: %s\n", eff_path);
        return 0;
    }

    UsageRecord *recs = NULL;
    long n = usage_load(NULL, &recs);
    if (n < 0) {
        fprintf(stderr, "Failed to read usage log: %s\n", eff_path);
        return 1;
    }

    if (n == 0) {
        fprintf(stderr, "No LLM usage recorded yet. Run an analysis with --llm first.\n");
        fprintf(stderr, "Usage log: %s\n", eff_path);
        usage_free(recs);
        return 0;
    }

    long long calls = 0, pt = 0, ct = 0, tt = 0;
    usage_totals(recs, n, &calls, &pt, &ct, &tt);

    if (opts->usage_json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *total = cJSON_AddObjectToObject(root, "total");
        cJSON_AddNumberToObject(total, "calls", (double)calls);
        cJSON_AddNumberToObject(total, "prompt_tokens", (double)pt);
        cJSON_AddNumberToObject(total, "completion_tokens", (double)ct);
        cJSON_AddNumberToObject(total, "total_tokens", (double)tt);
        cJSON_AddNumberToObject(root, "records", (double)n);
        if (opts->usage_by_llm) {
            UsageGroup groups[USAGE_MAX_GROUPS];
            int g = usage_by_llm(recs, n, groups, USAGE_MAX_GROUPS);
            cJSON *arr = cJSON_AddArrayToObject(root, "by_llm");
            for (int i = 0; i < g; i++) {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "name", groups[i].name);
                cJSON_AddNumberToObject(o, "calls", (double)groups[i].calls);
                cJSON_AddNumberToObject(o, "prompt_tokens", (double)groups[i].prompt_tokens);
                cJSON_AddNumberToObject(o, "completion_tokens", (double)groups[i].completion_tokens);
                cJSON_AddNumberToObject(o, "total_tokens", (double)groups[i].total_tokens);
                cJSON_AddItemToArray(arr, o);
            }
        }
        char *str = cJSON_Print(root);
        if (str) { fputs(str, stdout); fputc('\n', stdout); free(str); }
        cJSON_Delete(root);
        usage_free(recs);
        return 0;
    }

    if (opts->usage_by_llm) {
        UsageGroup groups[USAGE_MAX_GROUPS];
        int g = usage_by_llm(recs, n, groups, USAGE_MAX_GROUPS);
        const char *labels[USAGE_MAX_GROUPS];
        long long vals[USAGE_MAX_GROUPS];
        for (int i = 0; i < g; i++) {
            labels[i] = groups[i].name;
            vals[i] = groups[i].total_tokens;
        }
        ChartOptions co = { CHART_HBAR, 24, color_active(), "tokens" };
        chart_render(stdout, "LLM usage by provider", labels, vals, g, &co);
        printf("\nTotal: %lld calls, %lld tokens (%lld prompt + %lld completion)\n",
               calls, tt, pt, ct);
        usage_free(recs);
        return 0;
    }

    int days = opts->usage_days > 0 ? opts->usage_days : 14;
    UsageDay *day = calloc((size_t)days, sizeof(UsageDay));
    if (!day) { usage_free(recs); return 1; }
    usage_by_day(recs, n, days, day);

    int lo = -1, hi = -1;
    for (int i = 0; i < days; i++) {
        if (usage_day_is_empty(&day[i])) continue;
        if (lo < 0) lo = i;
        hi = i;
    }

    if (lo < 0) {
        fprintf(stderr, "No LLM usage in the last %d days.\n", days);
        free(day);
        usage_free(recs);
        return 0;
    }

    int span = hi - lo + 1;
    const char **lbl = malloc((size_t)span * sizeof(const char *));
    long long *vals = malloc((size_t)span * sizeof(long long));
    char *label_buf = malloc((size_t)span * 16);
    for (int i = 0; i < span; i++) {
        time_t ds = (time_t)day[lo + i].day_start;
        struct tm *tmv = gmtime(&ds); /* not thread-safe, but this command is single-threaded */
        snprintf(label_buf + i * 16, 16, "%02d-%02d", tmv->tm_mon + 1, tmv->tm_mday);
        lbl[i] = label_buf + i * 16;
        vals[i] = day[lo + i].total_tokens;
    }

    char title[96];
    snprintf(title, sizeof(title), "LLM usage - last %d days", days);
    ChartOptions co = { CHART_VBAR, 8, color_active(), "tokens" };
    chart_render(stdout, title, lbl, vals, span, &co);
    printf("\nTotal: %lld calls, %lld tokens (%lld prompt + %lld completion)\n",
           calls, tt, pt, ct);

    free(lbl);
    free(vals);
    free(label_buf);
    free(day);
    usage_free(recs);
    return 0;
}
