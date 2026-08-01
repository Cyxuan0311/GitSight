#define _DEFAULT_SOURCE

#include "gitsight/usage.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static char g_usage_file[1024];

void usage_set_file(const char *path) {
    g_usage_file[0] = '\0';
    if (path && path[0])
        snprintf(g_usage_file, sizeof(g_usage_file), "%s", path);
}

static void resolve_default_path(char *buf, size_t len) {
    const char *home = getenv("XDG_CONFIG_HOME");
    if (home && home[0]) {
        snprintf(buf, len, "%s/gitsight/%s", home, USAGE_FILE);
    } else {
        const char *h = getenv("HOME");
        if (h && h[0])
            snprintf(buf, len, "%s/.config/gitsight/%s", h, USAGE_FILE);
        else
            snprintf(buf, len, ".config/gitsight/%s", USAGE_FILE);
    }
}

static void resolve_path(const char *hint, char *buf, size_t len) {
    if (hint && hint[0]) { snprintf(buf, len, "%s", hint); return; }
    const char *env = getenv("GS_USAGE_FILE");
    if (env && env[0]) { snprintf(buf, len, "%s", env); return; }
    if (g_usage_file[0]) { snprintf(buf, len, "%s", g_usage_file); return; }
    resolve_default_path(buf, len);
}

void usage_path_from_config(const char *config_path, char *buf, size_t len) {
    if (config_path && config_path[0]) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", config_path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            if (dir[0] == '\0') snprintf(dir, sizeof(dir), ".");
        } else {
            snprintf(dir, sizeof(dir), ".");
        }
        snprintf(buf, len, "%s/%s", dir, USAGE_FILE);
    } else {
        resolve_default_path(buf, len);
    }
}

void usage_file_path(char *buf, size_t len) {
    resolve_path(NULL, buf, len);
}

static long load_records(const char *path, UsageRecord **out) {
    *out = NULL;
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);
    if (len <= 0) { fclose(fp); return 0; }

    char *content = malloc((size_t)len + 1);
    if (!content) { fclose(fp); return -1; }
    size_t rl = fread(content, 1, (size_t)len, fp);
    content[rl] = '\0';
    fclose(fp);

    cJSON *json = cJSON_Parse(content);
    free(content);
    if (!json) return -1;

    cJSON *arr = cJSON_GetObjectItem(json, "records");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(json); return 0; }

    int n = cJSON_GetArraySize(arr);
    if (n <= 0) { cJSON_Delete(json); return 0; }

    UsageRecord *recs = calloc((size_t)n, sizeof(UsageRecord));
    if (!recs) { cJSON_Delete(json); return -1; }

    int idx = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsObject(it)) continue;
        UsageRecord *r = &recs[idx];
        cJSON *v;
        if ((v = cJSON_GetObjectItem(it, "ts")) && cJSON_IsNumber(v))
            r->timestamp = (long long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(it, "llm")) && cJSON_IsString(v))
            snprintf(r->llm, sizeof(r->llm), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(it, "model")) && cJSON_IsString(v))
            snprintf(r->model, sizeof(r->model), "%s", v->valuestring);
        if ((v = cJSON_GetObjectItem(it, "prompt")) && cJSON_IsNumber(v))
            r->prompt_tokens = (long long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(it, "completion")) && cJSON_IsNumber(v))
            r->completion_tokens = (long long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(it, "total")) && cJSON_IsNumber(v))
            r->total_tokens = (long long)v->valuedouble;
        idx++;
    }
    cJSON_Delete(json);

    *out = recs;
    return idx;
}

static void ensure_dir(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static ErrorCode save_records(const char *path, const UsageRecord *recs, long n) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return ERR_MEMORY;
    cJSON *arr = cJSON_AddArrayToObject(json, "records");

    for (long i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "ts", (double)recs[i].timestamp);
        if (recs[i].llm[0])
            cJSON_AddStringToObject(o, "llm", recs[i].llm);
        if (recs[i].model[0])
            cJSON_AddStringToObject(o, "model", recs[i].model);
        cJSON_AddNumberToObject(o, "prompt", (double)recs[i].prompt_tokens);
        cJSON_AddNumberToObject(o, "completion", (double)recs[i].completion_tokens);
        cJSON_AddNumberToObject(o, "total", (double)recs[i].total_tokens);
        cJSON_AddItemToArray(arr, o);
    }

    ensure_dir(path);
    FILE *fp = fopen(path, "w");
    if (!fp) { cJSON_Delete(json); return ERR_CONFIG_WRITE; }

    char *str = cJSON_Print(json);
    fprintf(fp, "%s\n", str ? str : "");
    free(str);
    cJSON_Delete(json);
    fclose(fp);
    return GS_OK;
}

ErrorCode usage_record(const char *hint, const UsageRecord *rec) {
    char path[1024];
    resolve_path(hint, path, sizeof(path));

    UsageRecord *existing = NULL;
    long n = load_records(path, &existing);
    if (n < 0) return ERR_FILE_IO;

    if (n >= USAGE_MAX_RECORDS) {
        long keep = USAGE_MAX_RECORDS - 1;
        for (long i = 0; i < keep; i++)
            existing[i] = existing[n - keep + i];
        n = keep;
    }

    UsageRecord *all = realloc(existing, (size_t)(n + 1) * sizeof(UsageRecord));
    if (!all) { free(existing); return ERR_MEMORY; }
    all[n] = *rec;
    n++;

    ErrorCode e = save_records(path, all, n);
    free(all);
    return e;
}

long usage_load(const char *hint, UsageRecord **out) {
    char path[1024];
    resolve_path(hint, path, sizeof(path));
    return load_records(path, out);
}

void usage_free(UsageRecord *recs) {
    free(recs);
}

ErrorCode usage_clear(const char *hint) {
    char path[1024];
    resolve_path(hint, path, sizeof(path));
    return save_records(path, NULL, 0);
}

void usage_totals(const UsageRecord *recs, long n,
                  long long *calls, long long *prompt_tokens,
                  long long *completion_tokens, long long *total_tokens)
{
    long long c = 0, p = 0, co = 0, t = 0;
    for (long i = 0; i < n; i++) {
        c++;
        p += recs[i].prompt_tokens;
        co += recs[i].completion_tokens;
        t += recs[i].total_tokens;
    }
    if (calls) *calls = c;
    if (prompt_tokens) *prompt_tokens = p;
    if (completion_tokens) *completion_tokens = co;
    if (total_tokens) *total_tokens = t;
}

int usage_by_llm(const UsageRecord *recs, long n, UsageGroup *out, int max_out) {
    int g = 0;
    for (long i = 0; i < n; i++) {
        int idx = -1;
        for (int j = 0; j < g; j++) {
            if (strcmp(out[j].name, recs[i].llm) == 0) { idx = j; break; }
        }
        if (idx < 0) {
            if (g >= max_out) continue;
            idx = g++;
            memset(&out[idx], 0, sizeof(UsageGroup));
            snprintf(out[idx].name, sizeof(out[idx].name), "%s",
                     recs[i].llm[0] ? recs[i].llm : "?");
        }
        out[idx].calls++;
        out[idx].prompt_tokens += recs[i].prompt_tokens;
        out[idx].completion_tokens += recs[i].completion_tokens;
        out[idx].total_tokens += recs[i].total_tokens;
    }
    return g;
}

void usage_by_day(const UsageRecord *recs, long n, int days, UsageDay *out) {
    memset(out, 0, (size_t)days * sizeof(UsageDay));
    long long today = (long long)time(NULL) / 86400;
    for (int i = 0; i < days; i++)
        out[i].day_start = (today - (days - 1 - i)) * 86400;

    for (long i = 0; i < n; i++) {
        long long d = recs[i].timestamp / 86400;
        long long offset = today - d;
        if (offset < 0 || offset >= days) continue;
        UsageDay *u = &out[days - 1 - (int)offset];
        u->calls++;
        u->prompt_tokens += recs[i].prompt_tokens;
        u->completion_tokens += recs[i].completion_tokens;
        u->total_tokens += recs[i].total_tokens;
    }
}
