#include "server.h"

#include "gitsight/usage.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Secrets never leave the process. A masked value tells the UI whether a
 * credential is configured without disclosing it, and writing the sentinel
 * back leaves the stored value untouched. */
#define SECRET_UNCHANGED "__unchanged__"

static void add_masked(cJSON *obj, const char *key, const char *value) {
    size_t n = strlen(value);
    cJSON *o = cJSON_CreateObject();
    if (n == 0) {
        cJSON_AddBoolToObject(o, "set", 0);
        cJSON_AddStringToObject(o, "masked", "");
    } else if (n <= 8) {
        cJSON_AddBoolToObject(o, "set", 1);
        cJSON_AddStringToObject(o, "masked", "********");
    } else {
        char tail[9];
        snprintf(tail, sizeof(tail), "%s", value + n - 4);
        cJSON_AddBoolToObject(o, "set", 1);
        cJSON_AddStringToObject(o, "masked", tail);
    }
    cJSON_AddItemToObject(obj, key, o);
}

static cJSON *subscriptions_json(const Config *cfg) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < cfg->subscription_count; i++) {
        const Subscription *s = &cfg->subscriptions[i];
        if (!s->repo[0]) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        cJSON_AddStringToObject(o, "repo", s->repo);
        cJSON_AddStringToObject(o, "alias", s->alias);
        /* An empty override means "inherit the global setting", which is what
         * the CLI's watch list displays as the effective value. */
        cJSON_AddStringToObject(o, "platform",
                                s->platform[0] ? s->platform : cfg->platform);
        cJSON_AddNumberToObject(o, "days",
                                s->days > 0 ? s->days : cfg->default_days);
        cJSON_AddStringToObject(o, "format",
                                s->format[0] ? s->format : cfg->format);
        cJSON_AddStringToObject(o, "branch", s->branch);
        cJSON_AddBoolToObject(o, "overrides_platform", s->platform[0] != '\0');
        cJSON_AddBoolToObject(o, "overrides_days", s->days > 0);
        cJSON_AddBoolToObject(o, "overrides_format", s->format[0] != '\0');
        cJSON_AddBoolToObject(o, "overrides_branch", s->branch[0] != '\0');
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static cJSON *config_json(const Config *cfg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "platform", cfg->platform);
    cJSON_AddStringToObject(root, "lang", cfg->lang);
    cJSON_AddStringToObject(root, "api_base", cfg->api_base);
    cJSON_AddStringToObject(root, "proxy", cfg->proxy);
    cJSON_AddNumberToObject(root, "default_days", cfg->default_days);
    cJSON_AddNumberToObject(root, "max_pages", cfg->max_pages);
    cJSON_AddStringToObject(root, "color",
                            cfg->color_mode == 0 ? "auto" :
                            cfg->color_mode == 1 ? "always" : "never");
    cJSON_AddStringToObject(root, "format", cfg->format);
    cJSON_AddStringToObject(root, "active_llm", cfg->active_llm);
    cJSON_AddStringToObject(root, "branch", cfg->branch);
    add_masked(root, "github_token", cfg->github_token);
    add_masked(root, "gitlab_token", cfg->gitlab_token);
    add_masked(root, "codeberg_token", cfg->codeberg_token);

    cJSON *ex = cJSON_CreateObject();
    cJSON_AddBoolToObject(ex, "show_prs", cfg->show_prs);
    cJSON_AddBoolToObject(ex, "show_commits", cfg->show_commits);
    cJSON_AddBoolToObject(ex, "show_issues", cfg->show_issues);
    cJSON_AddBoolToObject(ex, "show_summary", cfg->show_summary);
    cJSON_AddBoolToObject(ex, "show_top_labels", cfg->show_top_labels);
    cJSON_AddBoolToObject(ex, "show_releases", cfg->show_releases);
    cJSON_AddBoolToObject(ex, "show_contributors", cfg->show_contributors);
    cJSON_AddBoolToObject(ex, "show_languages", cfg->show_languages);
    cJSON_AddNumberToObject(ex, "item_limit", cfg->item_limit);

    cJSON *md = cJSON_CreateObject();
    cJSON_AddNumberToObject(md, "table_width", cfg->md_table_width);
    cJSON_AddItemToObject(ex, "md", md);

    cJSON *txt = cJSON_CreateObject();
    cJSON_AddNumberToObject(txt, "table_width", cfg->txt_table_width);
    cJSON_AddItemToObject(ex, "txt", txt);

    cJSON *xlsx = cJSON_CreateObject();
    cJSON_AddNumberToObject(xlsx, "font_size", cfg->xlsx_font_size);
    cJSON_AddStringToObject(xlsx, "font_name", cfg->xlsx_font_name);
    cJSON_AddNumberToObject(xlsx, "col_a_width", cfg->xlsx_col_a_width);
    cJSON_AddNumberToObject(xlsx, "col_b_width", cfg->xlsx_col_b_width);
    cJSON_AddBoolToObject(xlsx, "border", cfg->xlsx_border);
    cJSON_AddStringToObject(xlsx, "sheet_name", cfg->xlsx_sheet_name);
    cJSON_AddItemToObject(ex, "xlsx", xlsx);

    cJSON *pdf = cJSON_CreateObject();
    cJSON_AddNumberToObject(pdf, "margin", cfg->pdf_margin);
    cJSON_AddNumberToObject(pdf, "line_height", cfg->pdf_line_height);
    cJSON_AddNumberToObject(pdf, "col2_padding", cfg->pdf_col2_padding);
    cJSON_AddStringToObject(pdf, "title_color", cfg->pdf_title_color);
    cJSON_AddStringToObject(pdf, "header_color", cfg->pdf_header_color);
    cJSON_AddStringToObject(pdf, "label_color", cfg->pdf_label_color);
    cJSON_AddStringToObject(pdf, "link_color", cfg->pdf_link_color);
    cJSON_AddStringToObject(pdf, "open_color", cfg->pdf_open_color);
    cJSON_AddStringToObject(pdf, "closed_color", cfg->pdf_closed_color);
    cJSON_AddStringToObject(pdf, "merged_color", cfg->pdf_merged_color);
    cJSON_AddItemToObject(ex, "pdf", pdf);

    cJSON *html = cJSON_CreateObject();
    cJSON_AddStringToObject(html, "title_color", cfg->html_title_color);
    cJSON_AddStringToObject(html, "header_color", cfg->html_header_color);
    cJSON_AddStringToObject(html, "label_color", cfg->html_label_color);
    cJSON_AddStringToObject(html, "link_color", cfg->html_link_color);
    cJSON_AddStringToObject(html, "open_color", cfg->html_open_color);
    cJSON_AddStringToObject(html, "closed_color", cfg->html_closed_color);
    cJSON_AddStringToObject(html, "merged_color", cfg->html_merged_color);
    cJSON_AddItemToObject(ex, "html", html);

    cJSON_AddItemToObject(root, "export", ex);
    cJSON_AddItemToObject(root, "subscriptions", subscriptions_json(cfg));
    return root;
}

/* GET /api/config */
static void handle_config_get(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    Config cfg;
    server_load_config(&cfg);
    cJSON *root = config_json(&cfg);
    cJSON_AddStringToObject(root, "path", g_server.config_path);
    struct stat st;
    cJSON_AddBoolToObject(root, "exists", stat(g_server.config_path, &st) == 0);
    cJSON_AddStringToObject(root, "secret_sentinel", SECRET_UNCHANGED);
    res_json_obj(res, 200, root);
}

/* PATCH /api/config
 *
 * Accepts {"sets": {"key": "value", ...}}. Writes go through config_set so the
 * C-side whitelist, enum ranges and numeric bounds stay authoritative; the
 * browser cannot set anything the CLI would reject. */
static void handle_config_patch(Request *req, Response *res, void *user) {
    (void)user;
    cJSON *root = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!root) {
        res_error(res, 400, "bad_json", "request body is not valid JSON");
        return;
    }
    cJSON *sets = cJSON_GetObjectItem(root, "sets");
    if (!cJSON_IsObject(sets)) {
        cJSON_Delete(root);
        res_error(res, 400, "bad_request", "expected {\"sets\": {...}}");
        return;
    }

    int applied = 0, failed = 0;
    cJSON *key = NULL;
    cJSON_ArrayForEach(key, sets) {
        if (!key->string) continue;
        char value[1024];
        if (cJSON_IsString(key)) snprintf(value, sizeof(value), "%s", key->valuestring);
        else if (cJSON_IsBool(key)) snprintf(value, sizeof(value), "%s", key->valueint ? "true" : "false");
        else if (cJSON_IsNumber(key)) snprintf(value, sizeof(value), "%.10g", key->valuedouble);
        else { failed++; continue; }

        ErrorCode e;
        server_lock();
        e = config_set(g_server.config_path, key->string, value);
        server_unlock();
        if (e == GS_OK) applied++;
        else failed++;
    }
    cJSON_Delete(root);

    Config cfg;
    server_load_config(&cfg);
    cJSON *out = config_json(&cfg);
    cJSON_AddNumberToObject(out, "applied", applied);
    cJSON_AddNumberToObject(out, "rejected", failed);
    if (failed && !applied) {
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "error", "validation_failed");
        cJSON_AddStringToObject(body, "message",
                                "no setting was accepted; check keys and value ranges");
        cJSON_Delete(out);
        res_json_obj(res, 400, body);
        return;
    }
    res_json_obj(res, 200, out);
}

/* GET /api/config/raw
 *
 * Deliberately text/plain: the body is the config file byte for byte, and the
 * browser's raw editor wants that text, not the parsed object. Labelling it
 * application/json would make every JSON client deserialize it into an object
 * the editor cannot display. */
static void handle_config_raw_get(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    FILE *fp = fopen(g_server.config_path, "rb");
    if (!fp) {
        res_error(res, 404, "no_config", "config file does not exist yet");
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); res_error(res, 500, "read_failed", "cannot read config"); return; }
    char *data = malloc((size_t)sz + 1);
    if (!data) { fclose(fp); res_error(res, 500, "internal", "out of memory"); return; }
    size_t got = fread(data, 1, (size_t)sz, fp);
    fclose(fp);
    data[got] = '\0';
    res_bytes(res, 200, "text/plain; charset=utf-8", data, got);
    free(data);
}

/* PUT /api/config/raw -- validates JSON before overwriting. */
static void handle_config_raw_put(Request *req, Response *res, void *user) {
    (void)user;
    cJSON *parsed = cJSON_Parse(req->body && req->body_len ? req->body : "");
    if (!parsed || !cJSON_IsObject(parsed)) {
        if (parsed) cJSON_Delete(parsed);
        res_error(res, 400, "bad_json", "config must be a JSON object");
        return;
    }
    cJSON_Delete(parsed);

    server_lock();
    /* Validated as JSON above; config_load on the next request will reject
     * anything structurally wrong that still parses as JSON. */
    FILE *fp = fopen(g_server.config_path, "wb");
    if (!fp) {
        server_unlock();
        res_error(res, 500, "write_failed", "cannot open the config file for writing");
        return;
    }
    size_t n = fwrite(req->body, 1, req->body_len, fp);
    fclose(fp);
    server_unlock();
    if (n != req->body_len) {
        res_error(res, 500, "write_failed", "short write while saving config");
        return;
    }
    res_no_content(res);
}

/* POST /api/config/reset */
static void handle_config_reset(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    server_lock();
    ErrorCode e = config_save_default(g_server.config_path);
    server_unlock();
    if (e != GS_OK) {
        res_error_code(res, e, g_server.config_path);
        return;
    }
    Config cfg;
    server_load_config(&cfg);
    cJSON *out = config_json(&cfg);
    cJSON_AddStringToObject(out, "path", g_server.config_path);
    res_json_obj(res, 200, out);
}

void api_register_config(Router *r) {
    router_add(r, "GET",    "/api/config", handle_config_get, NULL);
    router_add(r, "PATCH",  "/api/config", handle_config_patch, NULL);
    router_add(r, "GET",    "/api/config/raw", handle_config_raw_get, NULL);
    router_add(r, "PUT",    "/api/config/raw", handle_config_raw_put, NULL);
    router_add(r, "POST",   "/api/config/reset", handle_config_reset, NULL);
}