#include "server.h"
#include "jobs.h"

#include "gitsight/i18n.h"
#include "gitsight/config.h"
#include "gitsight/llm/client.h"
#include "gitsight/models.h"
#include "gitsight/platform/http.h"
#include "gitsight/platform/profile.h"
#include "gitsight/usage.h"
#include "gitsight/version.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* GET /api/health */
static void handle_health(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "version", GS_VERSION);
    cJSON_AddStringToObject(root, "config_path", g_server.config_path);
    cJSON_AddStringToObject(root, "usage_path", g_server.usage_path);
    cJSON_AddStringToObject(root, "web_root", g_server.web_root);
    cJSON_AddNumberToObject(root, "uptime_seconds",
                            (double)(time(NULL) - g_server.started_at));
    cJSON_AddNumberToObject(root, "active_jobs", jobs_active_count());
    res_json_obj(res, 200, root);
}

/* GET /api/meta/options
 *
 * The frontend builds every dropdown, toggle and numeric bound from this one
 * payload, so the browser can never offer an option the C code would reject.
 * The values below mirror the tables the CLI itself validates against:
 * exporter registry, platform profiles, export field defaults, and the named
 * color palette. */
static cJSON *str_array(const char *const *items, int n) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(items[i]));
    return arr;
}

static void handle_options(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    cJSON *root = cJSON_CreateObject();

    static const char *const platforms[] = { "github", "gitlab", "codeberg", "gitea", "forgejo" };
    cJSON *pa = cJSON_CreateArray();
    for (size_t i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
        const PlatformProfile *p = platform_profile(platforms[i]);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", platforms[i]);
        /* GitLab has its own driver and therefore no profile row. */
        cJSON_AddStringToObject(o, "api_base", p ? p->default_api_base : "https://gitlab.com/api/v4");
        cJSON_AddStringToObject(o, "auth_scheme", p ? p->auth_scheme : "PRIVATE-TOKEN");
        cJSON_AddBoolToObject(o, "uses_github_driver", p != NULL);
        cJSON_AddItemToArray(pa, o);
    }
    cJSON_AddItemToObject(root, "platforms", pa);

    static const char *const fmts[] = { "json", "md", "txt", "html", "csv", "xlsx", "pdf" };
    cJSON *fa = cJSON_CreateArray();
    for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", fmts[i]);
        cJSON_AddStringToObject(o, "extension", format_extension(fmts[i]));
        cJSON_AddStringToObject(o, "mime", format_mime(fmts[i]));
        cJSON_AddBoolToObject(o, "binary", !strcmp(fmts[i], "xlsx") || !strcmp(fmts[i], "pdf"));
        /* json and csv always emit every item; the rest honour item_limit. */
        cJSON_AddBoolToObject(o, "exports_all_items",
                              !strcmp(fmts[i], "json") || !strcmp(fmts[i], "csv"));
        cJSON_AddBoolToObject(o, "section_aware", strcmp(fmts[i], "csv") != 0);
        cJSON_AddItemToArray(fa, o);
    }
    cJSON_AddItemToObject(root, "formats", fa);

    static const char *const langs[] = { "en", "zh" };
    cJSON *la = cJSON_CreateArray();
    for (size_t i = 0; i < 2; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", langs[i]);
        cJSON_AddStringToObject(o, "label", !strcmp(langs[i], "zh") ? "简体中文" : "English");
        cJSON_AddItemToArray(la, o);
    }
    cJSON_AddItemToObject(root, "languages", la);

    static const char *const colors[] = { "auto", "always", "never" };
    cJSON_AddItemToObject(root, "color_modes",
                          str_array(colors, 3));

    static const char *const modes[] = { "health", "trends", "release" };
    cJSON *ma = cJSON_CreateArray();
    for (size_t i = 0; i < 3; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", modes[i]);
        cJSON_AddStringToObject(o, "hint", !strcmp(modes[i], "health")
            ? "Activity level, focus areas, notable changes"
            : !strcmp(modes[i], "trends")
            ? "Gaining/losing attention, shifts over the period"
            : "Release notes highlights and outstanding risks");
        cJSON_AddItemToArray(ma, o);
    }
    cJSON_AddItemToObject(root, "llm_modes", ma);

    /* Named colors accepted wherever a color is configurable. Values match
     * NAMED_COLORS in cmd_common.c so "#rrggbb", "r,g,b" and a name all work. */
    static const struct { const char *name; int rgb[3]; } named[] = {
        { "black", {0,0,0} }, { "white", {255,255,255} }, { "red", {220,38,41} },
        { "green", {26,127,55} }, { "blue", {37,86,196} }, { "yellow", {212,167,21} },
        { "orange", {230,126,34} }, { "purple", {130,80,223} }, { "gray", {119,119,119} },
        { "grey", {119,119,119} }, { "pink", {234,120,181} }, { "brown", {121,85,72} },
        { "cyan", {23,178,178} }, { "magenta", {186,59,212} }, { "gold", {180,140,20} },
        { "navy", {16,32,96} }, { "teal", {0,96,96} }, { "silver", {192,192,192} },
        { "maroon", {128,0,0} }, { "olive", {107,107,0} }
    };
    cJSON *nca = cJSON_CreateArray();
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                 named[i].rgb[0], named[i].rgb[1], named[i].rgb[2]);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", named[i].name);
        cJSON_AddStringToObject(o, "hex", hex);
        cJSON_AddItemToArray(nca, o);
    }
    cJSON_AddItemToObject(root, "named_colors", nca);

    /* The `--export key=value` names the CLI accepts. The table in config.c is
     * the same one the CLI validates against, so the console can list exactly
     * the keys that exist instead of guessing. */
    cJSON *eka = cJSON_CreateArray();
    for (int i = 0; i < config_export_override_count(); i++) {
        const char *name = config_export_override_name(i);
        if (name) cJSON_AddItemToArray(eka, cJSON_CreateString(name));
    }
    cJSON_AddItemToObject(root, "export_keys", eka);

    cJSON_AddNumberToObject(root, "batch_max_threads", GS_BATCH_MAX_THREADS);
    cJSON_AddNumberToObject(root, "max_repos", 16);
    cJSON_AddNumberToObject(root, "max_subscriptions", MAX_SUBSCRIPTIONS);
    cJSON_AddNumberToObject(root, "max_llms", MAX_LLMS);
    cJSON_AddNumberToObject(root, "filter_max_per_kind", 8);
    cJSON_AddNumberToObject(root, "filter_max_len", 64);
    cJSON_AddNumberToObject(root, "report_langs_max", REPORT_LANGS_MAX);

    cJSON_AddStringToObject(root, "cache_ttl_note",
                            "Responses are cached for 300s unless a run disables it");
    res_json_obj(res, 200, root);
}

/* GET /api/meta/i18n
 *
 * The report body's translation table, so the browser renders section headings
 * in the same language the exporters do. Keys carrying a "[XX] " prefix are
 * disambiguation tags for the terminal; the web view strips them. */
static void handle_i18n(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "entries", gs_i18n_table_json());
    res_json_obj(res, 200, root);
}

/* GET /api/quota */
static void handle_quota(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    long rem = -1, rst = 0;
    http_quota(&rem, &rst);
    cJSON *root = cJSON_CreateObject();
    /* The quota view is process-wide and advisory: anonymous and
     * authenticated callers share it, so it can lag behind any one request. */
    if (rem >= 0) {
        cJSON_AddBoolToObject(root, "known", 1);
        cJSON_AddNumberToObject(root, "remaining", rem);
        cJSON_AddNumberToObject(root, "reset_at", (double)rst);
        cJSON_AddNumberToObject(root, "resets_in_seconds", (double)(rst - time(NULL)));
    } else {
        cJSON_AddBoolToObject(root, "known", 0);
    }
    res_json_obj(res, 200, root);
}

/* GET /api/cache */
static void handle_cache_get(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    char dir[512] = "";
    long ttl = 0;
    int enabled = 0;
    http_cache_info(dir, sizeof(dir), &ttl, &enabled);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "dir", dir);
    cJSON_AddNumberToObject(root, "ttl_seconds", ttl);
    cJSON_AddBoolToObject(root, "enabled", enabled);
    res_json_obj(res, 200, root);
}

/* GET /api/meta/stats
 *
 * What the daemon has spent on the network since it started. A run that
 * feels slow is either waiting on the network or working through the items,
 * and these numbers say which. */
static void handle_stats(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    HttpStats st;
    http_stats(&st);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "requests", (double)st.requests);
    cJSON_AddNumberToObject(root, "pages", (double)st.pages);
    cJSON_AddNumberToObject(root, "cache_hits", (double)st.cache_hits);
    cJSON_AddNumberToObject(root, "cache_misses", (double)st.cache_misses);
    cJSON_AddNumberToObject(root, "retries", (double)st.retries);
    cJSON_AddNumberToObject(root, "bytes", (double)st.bytes);
    res_json_obj(res, 200, root);
}

/* DELETE /api/cache */
static void handle_cache_clear(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    char dir[512] = "";
    http_cache_info(dir, sizeof(dir), NULL, NULL);
    if (dir[0]) {
        char cmd[1200];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
        if (system(cmd) != 0) {
            res_error(res, 500, "cache_clear_failed", "could not remove the cache directory");
            return;
        }
    }
    res_no_content(res);
}

void api_register_meta(Router *r) {
    router_add(r, "GET",  "/api/health", handle_health, NULL);
    router_add(r, "GET",  "/api/meta/options", handle_options, NULL);
    router_add(r, "GET",  "/api/meta/stats", handle_stats, NULL);
    router_add(r, "GET",  "/api/meta/i18n", handle_i18n, NULL);
    router_add(r, "GET",  "/api/quota", handle_quota, NULL);
    router_add(r, "GET",  "/api/cache", handle_cache_get, NULL);
    router_add(r, "DELETE", "/api/cache", handle_cache_clear, NULL);
}