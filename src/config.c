#include "gitsight/config.h"
#include "gitsight/error.h"
#include "gitsight/color.h"
#include "gitsight/tui.h"
#include "gitsight/platform/profile.h"
#include <cJSON.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

static int platform_is_gitea_family(const char *p) {
    return strcmp(p, "codeberg") == 0 || strcmp(p, "gitea") == 0 ||
           strcmp(p, "forgejo") == 0;
}

/* True when the configured api_base was never customized (still a built-in
   platform default), so a platform switch may replace it. */
static int api_base_is_default(const char *base) {
    if (base[0] == '\0') return 1;
    if (strcmp(base, "https://api.github.com") == 0) return 1;
    if (strcmp(base, "https://gitlab.com/api/v4") == 0) return 1;
    if (strcmp(base, "https://codeberg.org/api/v1") == 0) return 1;
    if (strcmp(base, "https://gitea.com/api/v1") == 0) return 1;
    return 0;
}

/* If the active platform has its own default API base and the configured one
   was never customized (empty or still a built-in default), switch to it. */
static void apply_platform_defaults(Config *cfg) {
    const PlatformProfile *pf = platform_profile(cfg->platform);
    if (pf && api_base_is_default(cfg->api_base))
        snprintf(cfg->api_base, sizeof(cfg->api_base), "%s", pf->default_api_base);
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

void config_get_default_path(char *buf, size_t len) {
    const char *home = getenv("XDG_CONFIG_HOME");
    if (!home || home[0] == '\0') {
        home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : ".";
        }
        snprintf(buf, len, "%s/%s/%s", home, CONFIG_DIR, CONFIG_FILE);
    } else {
        snprintf(buf, len, "%s/%s/%s", home, "gitsight", CONFIG_FILE);
    }
}

/* ---- Table-driven export configuration ----
 * Every export.* field is declared once here; config_init (defaults),
 * config_load (JSON), apply_export_override (CLI k=v) and config_save
 * (JSON out) all iterate this table, so adding a field means adding one row. */
typedef enum { CF_BOOL, CF_INT, CF_FLOAT, CF_STR } CfgType;

typedef struct {
    const char *section;   /* NULL = directly under "export", else md/txt/xlsx/pdf */
    const char *key;       /* JSON key; CLI name is "section.key" or "key" */
    size_t offset;         /* offsetof(Config, field) */
    size_t size;           /* sizeof field for CF_STR, else 0 */
    CfgType type;
    int check;             /* CLI numeric validation: 0 = any, 1 = > 0, 2 = >= 0 */
    const char *dstr;      /* CF_STR default */
    double dnum;           /* numeric/bool default */
} ExportField;

#define EF(sec, k, fld, ty, chk, dnum) \
    { sec, k, offsetof(Config, fld), sizeof(((Config *)0)->fld), ty, chk, NULL, (double)(dnum) }
#define EFS(sec, k, fld, dstr) \
    { sec, k, offsetof(Config, fld), sizeof(((Config *)0)->fld), CF_STR, 0, (dstr), 0.0 }

static const ExportField EXPORT_FIELDS[] = {
    EF(NULL,   "show_prs",          show_prs,          CF_BOOL,  0, 1),
    EF(NULL,   "show_commits",      show_commits,      CF_BOOL,  0, 1),
    EF(NULL,   "show_issues",       show_issues,       CF_BOOL,  0, 1),
    EF(NULL,   "show_summary",      show_summary,      CF_BOOL,  0, 1),
    EF(NULL,   "show_top_labels",   show_top_labels,   CF_BOOL,  0, 1),
    EF(NULL,   "show_releases",     show_releases,     CF_BOOL,  0, 1),
    EF(NULL,   "show_contributors", show_contributors, CF_BOOL,  0, 1),
    EF(NULL,   "show_languages",    show_languages,    CF_BOOL,  0, 1),
    EF(NULL,   "item_limit",        item_limit,        CF_INT,   2, 10),
    EF("md",   "table_width",       md_table_width,    CF_INT,   1, 60),
    EF("txt",  "table_width",       txt_table_width,   CF_INT,   1, 56),
    EF("xlsx", "font_size",         xlsx_font_size,    CF_INT,   1, 11),
    EFS("xlsx", "font_name",        xlsx_font_name,    "Calibri"),
    EF("xlsx", "col_a_width",       xlsx_col_a_width,  CF_FLOAT, 1, 20.0),
    EF("xlsx", "col_b_width",       xlsx_col_b_width,  CF_FLOAT, 1, 60.0),
    EF("xlsx", "border",            xlsx_border,       CF_BOOL,  0, 1),
    EFS("xlsx", "sheet_name",       xlsx_sheet_name,   "Report"),
    EF("pdf",  "margin",            pdf_margin,        CF_FLOAT, 1, 50.0),
    EF("pdf",  "line_height",       pdf_line_height,   CF_FLOAT, 1, 1.4),
    EF("pdf",  "col2_padding",      pdf_col2_padding,  CF_FLOAT, 2, 12.0),
    EFS("pdf",  "title_color",      pdf_title_color,   "25,55,160"),
    EFS("pdf",  "header_color",     pdf_header_color,  "25,55,160"),
    EFS("pdf",  "label_color",      pdf_label_color,   "90,90,90"),
    EFS("pdf",  "link_color",       pdf_link_color,    "17,85,204"),
    EFS("pdf",  "open_color",       pdf_open_color,    "26,127,55"),
    EFS("pdf",  "closed_color",     pdf_closed_color,  "207,34,46"),
    EFS("pdf",  "merged_color",     pdf_merged_color,  "130,80,223"),
    EFS("html", "title_color",      html_title_color,   "25,55,160"),
    EFS("html", "header_color",     html_header_color,  "25,55,160"),
    EFS("html", "label_color",      html_label_color,   "90,90,90"),
    EFS("html", "link_color",       html_link_color,    "17,85,204"),
    EFS("html", "open_color",       html_open_color,    "26,127,55"),
    EFS("html", "closed_color",     html_closed_color,  "207,34,46"),
    EFS("html", "merged_color",     html_merged_color,  "130,80,223"),
};
#define N_EXPORT_FIELDS (sizeof(EXPORT_FIELDS) / sizeof(EXPORT_FIELDS[0]))

static int cli_num_ok(int check, double v) {
    return check == 0 || (check == 1 && v > 0) || (check == 2 && v >= 0);
}

void config_init(Config *cfg) {
    memset(cfg, 0, sizeof(Config));
    snprintf(cfg->platform, sizeof(cfg->platform), "github");
    snprintf(cfg->api_base, sizeof(cfg->api_base), "https://api.github.com");
    cfg->default_days = 7;
    cfg->max_pages = 20;
    cfg->color_mode = 0;
    snprintf(cfg->format, sizeof(cfg->format), "md");
    snprintf(cfg->lang, sizeof(cfg->lang), "en");

    for (size_t i = 0; i < N_EXPORT_FIELDS; i++) {
        const ExportField *f = &EXPORT_FIELDS[i];
        char *p = (char *)cfg + f->offset;
        switch (f->type) {
        case CF_BOOL:
        case CF_INT:   *(int *)p = (int)f->dnum; break;
        case CF_FLOAT: *(float *)p = (float)f->dnum; break;
        case CF_STR:   snprintf(p, f->size, "%s", f->dstr); break;
        }
    }

    cfg->subscription_count = 0;
}

static void parse_llm_object(cJSON *obj, LLMConfig *llm) {
    cJSON *item;

    item = cJSON_GetObjectItem(obj, "protocol");
    if (item && cJSON_IsString(item))
        snprintf(llm->protocol, sizeof(llm->protocol), "%s", item->valuestring);

    item = cJSON_GetObjectItem(obj, "api_key");
    if (item && cJSON_IsString(item))
        snprintf(llm->api_key, sizeof(llm->api_key), "%s", item->valuestring);

    item = cJSON_GetObjectItem(obj, "api_base");
    if (item && cJSON_IsString(item))
        snprintf(llm->api_base, sizeof(llm->api_base), "%s", item->valuestring);

    item = cJSON_GetObjectItem(obj, "model");
    if (item && cJSON_IsString(item))
        snprintf(llm->model, sizeof(llm->model), "%s", item->valuestring);

    item = cJSON_GetObjectItem(obj, "system_prompt");
    if (item && cJSON_IsString(item))
        snprintf(llm->system_prompt, sizeof(llm->system_prompt), "%s", item->valuestring);

    item = cJSON_GetObjectItem(obj, "system_prompt_file");
    if (item && cJSON_IsString(item))
        snprintf(llm->system_prompt_file, sizeof(llm->system_prompt_file), "%s", item->valuestring);

    item = cJSON_GetObjectItem(obj, "timeout");
    if (item && cJSON_IsNumber(item)) llm->timeout_secs = item->valueint;
    item = cJSON_GetObjectItem(obj, "max_tokens");
    if (item && cJSON_IsNumber(item)) llm->max_tokens = item->valueint;
    item = cJSON_GetObjectItem(obj, "temperature");
    if (item && cJSON_IsNumber(item)) llm->temperature = (float)item->valuedouble;
}

ErrorCode config_load(Config *cfg, const char *path) {
    char config_path[1024];
    if (path && path[0]) {
        snprintf(config_path, sizeof(config_path), "%s", path);
    } else {
        config_get_default_path(config_path, sizeof(config_path));
    }

    FILE *fp = fopen(config_path, "r");
    if (!fp) return GS_OK;

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);

    if (len <= 0) { fclose(fp); return GS_OK; }

    char *content = malloc((size_t)len + 1);
    if (!content) { fclose(fp); return ERR_MEMORY; }

    size_t read_len = fread(content, 1, (size_t)len, fp);
    content[read_len] = '\0';
    fclose(fp);

    cJSON *json = cJSON_Parse(content);
    free(content);
    if (!json) return ERR_JSON_PARSE;

    cJSON *item;

    item = cJSON_GetObjectItem(json, "platform");
    if (item && cJSON_IsString(item))
        snprintf(cfg->platform, sizeof(cfg->platform), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "lang");
    if (item && cJSON_IsString(item))
        snprintf(cfg->lang, sizeof(cfg->lang), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "github_token");
    if (item && cJSON_IsString(item))
        snprintf(cfg->github_token, sizeof(cfg->github_token), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "gitlab_token");
    if (item && cJSON_IsString(item))
        snprintf(cfg->gitlab_token, sizeof(cfg->gitlab_token), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "codeberg_token");
    if (item && cJSON_IsString(item))
        snprintf(cfg->codeberg_token, sizeof(cfg->codeberg_token), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "api_base");
    if (item && cJSON_IsString(item))
        snprintf(cfg->api_base, sizeof(cfg->api_base), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "proxy");
    if (item && cJSON_IsString(item))
        snprintf(cfg->proxy, sizeof(cfg->proxy), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "default_days");
    if (item && cJSON_IsNumber(item))
        cfg->default_days = item->valueint;

    item = cJSON_GetObjectItem(json, "max_pages");
    if (item && cJSON_IsNumber(item))
        cfg->max_pages = item->valueint;

    item = cJSON_GetObjectItem(json, "color");
    if (item && cJSON_IsString(item)) {
        if (strcmp(item->valuestring, "always") == 0) cfg->color_mode = 1;
        else if (strcmp(item->valuestring, "never") == 0) cfg->color_mode = 2;
    }

    item = cJSON_GetObjectItem(json, "format");
    if (item && cJSON_IsString(item))
        snprintf(cfg->format, sizeof(cfg->format), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "active_llm");
    if (item && cJSON_IsString(item))
        snprintf(cfg->active_llm, sizeof(cfg->active_llm), "%s", item->valuestring);

    cJSON *llms_item = cJSON_GetObjectItem(json, "llms");
    if (llms_item && cJSON_IsObject(llms_item)) {
        cfg->llm_count = 0;
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, llms_item) {
            if (cfg->llm_count >= MAX_LLMS) break;
            if (!cJSON_IsObject(child)) continue;
            LLMConfig *llm = &cfg->llms[cfg->llm_count];
            memset(llm, 0, sizeof(LLMConfig));
            snprintf(llm->name, sizeof(llm->name), "%s", child->string);
            parse_llm_object(child, llm);
            cfg->llm_count++;
        }
    } else {
        cJSON *old_llm = cJSON_GetObjectItem(json, "llm");
        if (old_llm && cJSON_IsObject(old_llm)) {
            cfg->llm_count = 1;
            memset(cfg->llms, 0, sizeof(LLMConfig));
            snprintf(cfg->llms[0].name, sizeof(cfg->llms[0].name), "default");
            parse_llm_object(old_llm, &cfg->llms[0]);
        }
    }

    if (!cfg->active_llm[0] && cfg->llm_count > 0) {
        snprintf(cfg->active_llm, sizeof(cfg->active_llm), "%s", cfg->llms[0].name);
    }

    /* Parse export config (table-driven: see EXPORT_FIELDS) */
    cJSON *export_obj = cJSON_GetObjectItem(json, "export");
    if (export_obj && cJSON_IsObject(export_obj)) {
        for (size_t i = 0; i < N_EXPORT_FIELDS; i++) {
            const ExportField *f = &EXPORT_FIELDS[i];
            cJSON *parent = export_obj;
            if (f->section) {
                parent = cJSON_GetObjectItem(export_obj, f->section);
                if (!parent || !cJSON_IsObject(parent)) continue;
            }
            item = cJSON_GetObjectItem(parent, f->key);
            if (!item) continue;
            char *p = (char *)cfg + f->offset;
            switch (f->type) {
            case CF_BOOL:
                if (cJSON_IsBool(item)) *(int *)p = cJSON_IsTrue(item);
                break;
            case CF_INT:
                if (cJSON_IsNumber(item)) *(int *)p = item->valueint;
                break;
            case CF_FLOAT:
                if (cJSON_IsNumber(item)) *(float *)p = (float)item->valuedouble;
                break;
            case CF_STR:
                if (cJSON_IsString(item)) snprintf(p, f->size, "%s", item->valuestring);
                break;
            }
        }
    }

    /* Parse subscriptions */
    cfg->subscription_count = 0;
    cJSON *subs_item = cJSON_GetObjectItem(json, "subscriptions");
    if (subs_item && cJSON_IsArray(subs_item)) {
        cJSON *s = NULL;
        cJSON_ArrayForEach(s, subs_item) {
            if (cfg->subscription_count >= MAX_SUBSCRIPTIONS) break;
            if (!cJSON_IsObject(s)) continue;
            Subscription *sub = &cfg->subscriptions[cfg->subscription_count];
            memset(sub, 0, sizeof(Subscription));
            cJSON *f = cJSON_GetObjectItem(s, "repo");
            if (f && cJSON_IsString(f))
                snprintf(sub->repo, sizeof(sub->repo), "%s", f->valuestring);
            f = cJSON_GetObjectItem(s, "alias");
            if (f && cJSON_IsString(f))
                snprintf(sub->alias, sizeof(sub->alias), "%s", f->valuestring);
            f = cJSON_GetObjectItem(s, "platform");
            if (f && cJSON_IsString(f))
                snprintf(sub->platform, sizeof(sub->platform), "%s", f->valuestring);
            f = cJSON_GetObjectItem(s, "days");
            if (f && cJSON_IsNumber(f)) sub->days = f->valueint;
            f = cJSON_GetObjectItem(s, "format");
            if (f && cJSON_IsString(f))
                snprintf(sub->format, sizeof(sub->format), "%s", f->valuestring);
            f = cJSON_GetObjectItem(s, "branch");
            if (f && cJSON_IsString(f))
                snprintf(sub->branch, sizeof(sub->branch), "%s", f->valuestring);
            if (sub->repo[0] == '\0') continue;
            cfg->subscription_count++;
        }
    }

    cJSON_Delete(json);
    apply_platform_defaults(cfg);
    return GS_OK;
}

static int parse_bool(const char *s) {
    return strcmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcmp(s, "yes") == 0;
}

/* Apply one "key=value" (or "section.key=value") export override from
   --export / `gs config set export.*`, validated via EXPORT_FIELDS. */
static void apply_export_override(Config *cfg, const char *kv) {
    const char *eq = strchr(kv, '=');
    if (!eq) return;
    size_t klen = (size_t)(eq - kv);
    const char *val = eq + 1;
    if (*val == '\0') return;

    char key[128];
    if (klen >= sizeof(key)) return;
    memcpy(key, kv, klen);
    key[klen] = '\0';

    for (size_t i = 0; i < N_EXPORT_FIELDS; i++) {
        const ExportField *f = &EXPORT_FIELDS[i];
        char name[160];
        if (f->section) snprintf(name, sizeof(name), "%s.%s", f->section, f->key);
        else snprintf(name, sizeof(name), "%s", f->key);
        if (strcmp(name, key) != 0) continue;

        char *p = (char *)cfg + f->offset;
        switch (f->type) {
        case CF_BOOL:
            *(int *)p = parse_bool(val);
            break;
        case CF_INT: {
            int v = atoi(val);
            if (cli_num_ok(f->check, v)) *(int *)p = v;
            break;
        }
        case CF_FLOAT: {
            float v = (float)atof(val);
            if (cli_num_ok(f->check, v)) *(float *)p = v;
            break;
        }
        case CF_STR:
            snprintf(p, f->size, "%s", val);
            break;
        }
        return;
    }
}

int config_export_override_count(void) {
    return (int)N_EXPORT_FIELDS;
}

const char *config_export_override_name(int index) {
    static char name[160];
    if (index < 0 || index >= (int)N_EXPORT_FIELDS) return NULL;
    const ExportField *f = &EXPORT_FIELDS[index];
    if (f->section) snprintf(name, sizeof(name), "%s.%s", f->section, f->key);
    else snprintf(name, sizeof(name), "%s", f->key);
    return name;
}

/* Applies the `key=value` pairs collected in cfg->export_extra to the fields
 * they name. The pairs are newline-separated, which is what both the CLI's
 * repeatable --export and the server's request body produce. */
void config_apply_export_overrides(Config *cfg) {
    if (!cfg || !cfg->export_extra[0]) return;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", cfg->export_extra);
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        while (*line == ' ') line++;
        if (*line) apply_export_override(cfg, line);
        line = nl ? nl + 1 : NULL;
    }
}

void config_merge(Config *cfg,
                   int since_days, int max_pages, const char *format,
                   const char *output, const char *proxy,
                   const char *api_base, int color_mode,
                   const char *token, const char *llm_name,
                   const char *platform, const char *branch)
{
    if (since_days > 0) cfg->default_days = since_days;
    if (max_pages > 0) cfg->max_pages = max_pages;
    if (format && format[0]) snprintf(cfg->format, sizeof(cfg->format), "%s", format);
    if (output && output[0]) snprintf(cfg->output, sizeof(cfg->output), "%s", output);
    if (proxy && proxy[0])   snprintf(cfg->proxy, sizeof(cfg->proxy), "%s", proxy);
    if (api_base && api_base[0]) snprintf(cfg->api_base, sizeof(cfg->api_base), "%s", api_base);
    if (color_mode >= 0) cfg->color_mode = color_mode;
    if (platform && platform[0]) snprintf(cfg->platform, sizeof(cfg->platform), "%s", platform);
    if (branch && branch[0]) snprintf(cfg->branch, sizeof(cfg->branch), "%s", branch);

    if (strcmp(cfg->platform, "gitlab") == 0) {
        if (token && token[0])
            snprintf(cfg->gitlab_token, sizeof(cfg->gitlab_token), "%s", token);
    } else if (platform_is_gitea_family(cfg->platform)) {
        if (token && token[0])
            snprintf(cfg->codeberg_token, sizeof(cfg->codeberg_token), "%s", token);
    } else {
        if (token && token[0])
            snprintf(cfg->github_token, sizeof(cfg->github_token), "%s", token);
    }

    const char *env_token = getenv("GITHUB_TOKEN");
    const char *env_gl_token = getenv("GITLAB_TOKEN");
    const char *env_cb_token = getenv("CODEBERG_TOKEN");
    if (!env_cb_token || !env_cb_token[0]) env_cb_token = getenv("GS_CODEBERG_TOKEN");
    const char *env_proxy = getenv("HTTPS_PROXY");
    const char *env_api   = getenv("GS_API_BASE");

    if (strcmp(cfg->platform, "gitlab") == 0) {
        if (!cfg->gitlab_token[0] && env_gl_token)
            snprintf(cfg->gitlab_token, sizeof(cfg->gitlab_token), "%s", env_gl_token);
    } else if (platform_is_gitea_family(cfg->platform)) {
        if (!cfg->codeberg_token[0] && env_cb_token)
            snprintf(cfg->codeberg_token, sizeof(cfg->codeberg_token), "%s", env_cb_token);
    } else {
        if (!cfg->github_token[0] && env_token)
            snprintf(cfg->github_token, sizeof(cfg->github_token), "%s", env_token);
    }
    if (!cfg->proxy[0] && env_proxy)
        snprintf(cfg->proxy, sizeof(cfg->proxy), "%s", env_proxy);
    if (!cfg->proxy[0]) {
        env_proxy = getenv("HTTP_PROXY");
        if (env_proxy) snprintf(cfg->proxy, sizeof(cfg->proxy), "%s", env_proxy);
    }
    if (!cfg->api_base[0] && env_api)
        snprintf(cfg->api_base, sizeof(cfg->api_base), "%s", env_api);

    apply_platform_defaults(cfg);

    if (llm_name && llm_name[0]) {
        int found = 0;
        for (int i = 0; i < cfg->llm_count; i++) {
            if (strcmp(cfg->llms[i].name, llm_name) == 0) {
                snprintf(cfg->active_llm, sizeof(cfg->active_llm), "%s", llm_name);
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Warning: LLM provider '%s' not found, using active.\n", llm_name);
        }
    }

    const char *env_llm_key   = getenv("LLM_API_KEY");
    const char *env_llm_base  = getenv("LLM_API_BASE");
    const char *env_llm_model = getenv("LLM_MODEL");
    const char *env_llm_proto = getenv("LLM_PROTOCOL");

    if (env_llm_key || env_llm_base || env_llm_model || env_llm_proto) {
        int idx = -1;
        for (int i = 0; i < cfg->llm_count; i++) {
            if (strcmp(cfg->llms[i].name, cfg->active_llm) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0 && cfg->llm_count < MAX_LLMS) {
            idx = cfg->llm_count++;
            memset(&cfg->llms[idx], 0, sizeof(LLMConfig));
            snprintf(cfg->llms[idx].name, sizeof(cfg->llms[idx].name), "default");
            snprintf(cfg->active_llm, sizeof(cfg->active_llm), "default");
        }
        if (idx >= 0) {
            if (!cfg->llms[idx].api_key[0] && env_llm_key)
                snprintf(cfg->llms[idx].api_key, sizeof(cfg->llms[idx].api_key), "%s", env_llm_key);
            if (!cfg->llms[idx].api_base[0] && env_llm_base)
                snprintf(cfg->llms[idx].api_base, sizeof(cfg->llms[idx].api_base), "%s", env_llm_base);
            if (!cfg->llms[idx].model[0] && env_llm_model)
                snprintf(cfg->llms[idx].model, sizeof(cfg->llms[idx].model), "%s", env_llm_model);
            if (env_llm_proto && env_llm_proto[0])
                snprintf(cfg->llms[idx].protocol, sizeof(cfg->llms[idx].protocol), "%s", env_llm_proto);
            const char *env_timeout = getenv("LLM_TIMEOUT");
            const char *env_maxtok  = getenv("LLM_MAX_TOKENS");
            const char *env_temp    = getenv("LLM_TEMPERATURE");
            if (env_timeout && atoi(env_timeout) > 0)
                cfg->llms[idx].timeout_secs = atoi(env_timeout);
            if (env_maxtok && atoi(env_maxtok) > 0)
                cfg->llms[idx].max_tokens = atoi(env_maxtok);
            if (env_temp && atof(env_temp) > 0.0)
                cfg->llms[idx].temperature = (float)atof(env_temp);
        }
    }

    /* Parse --export overrides */
    config_apply_export_overrides(cfg);
}

const LLMConfig *config_get_active_llm(const Config *cfg) {
    if (cfg->llm_count == 0) return NULL;

    if (cfg->active_llm[0]) {
        for (int i = 0; i < cfg->llm_count; i++) {
            if (strcmp(cfg->llms[i].name, cfg->active_llm) == 0)
                return &cfg->llms[i];
        }
    }

    return &cfg->llms[0];
}

void config_resolve_prompts(Config *cfg, const char *config_path) {
    char base[1024];
    snprintf(base, sizeof(base), "%s", config_path);
    char *slash = strrchr(base, '/');
    if (slash) *slash = '\0'; else snprintf(base, sizeof(base), ".");

    for (int i = 0; i < cfg->llm_count; i++) {
        if (cfg->llms[i].system_prompt_file[0] == '\0')
            continue;

        char filepath[2048];
        snprintf(filepath, sizeof(filepath), "%s/prompts/%s", base, cfg->llms[i].system_prompt_file);

        FILE *fp = fopen(filepath, "r");
        if (fp) {
            size_t n = fread(cfg->llms[i].system_prompt, 1, MAX_SYSTEM_PROMPT - 1, fp);
            cfg->llms[i].system_prompt[n] = '\0';
            fclose(fp);
        } else if (cfg->llms[i].system_prompt[0] == '\0') {
            fprintf(stderr, "Warning: prompt file not found: %s\n", filepath);
        }
    }
}

void config_list_llms(const Config *cfg) {
    TuiTable t;
    char active[MAX_LLMS][16];
    const char *cells[5];
    tui_table_init(&t, 5);
    tui_table_header(&t, 0, "Active");
    tui_table_header(&t, 1, "Name");
    tui_table_header(&t, 2, "Protocol");
    tui_table_header(&t, 3, "Model");
    tui_table_header(&t, 4, "API Base");
    tui_table_align(&t, 0, TUI_ALIGN_CENTER);
    tui_table_cell_color(&t, 1, CLR_GREEN);
    for (int i = 0; i < cfg->llm_count; i++) {
        const LLMConfig *l = &cfg->llms[i];
        int is_active = strcmp(l->name, cfg->active_llm) == 0;
        snprintf(active[i], sizeof(active[i]), "%s", is_active ? "*" : "");
        cells[0] = active[i];
        cells[1] = l->name;
        cells[2] = llm_protocol(l);
        cells[3] = l->model[0] ? l->model : "-";
        cells[4] = l->api_base[0] ? l->api_base : "-";
        tui_table_row(&t, cells);
    }
    t.use_color = color_active();
    t.border = TUI_BORDER_ASCII;
    t.zebra = 1;
    tui_table_render(&t, stderr);
}

ErrorCode config_save(const char *path, const Config *cfg) {
    ensure_dir(path);

    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "platform", cfg->platform);
    cJSON_AddStringToObject(json, "lang", cfg->lang);
    cJSON_AddStringToObject(json, "github_token", cfg->github_token);
    cJSON_AddStringToObject(json, "gitlab_token", cfg->gitlab_token);
    cJSON_AddStringToObject(json, "codeberg_token", cfg->codeberg_token);
    cJSON_AddStringToObject(json, "api_base", cfg->api_base);
    cJSON_AddStringToObject(json, "proxy", cfg->proxy);
    cJSON_AddNumberToObject(json, "default_days", cfg->default_days);
    cJSON_AddNumberToObject(json, "max_pages", cfg->max_pages);
    cJSON_AddStringToObject(json, "color",
        cfg->color_mode == 1 ? "always" :
        cfg->color_mode == 2 ? "never"  : "auto");
    cJSON_AddStringToObject(json, "format", cfg->format);

    cJSON *llms = cJSON_AddObjectToObject(json, "llms");
    for (int i = 0; i < cfg->llm_count; i++) {
        cJSON *llm = cJSON_CreateObject();
        if (cfg->llms[i].protocol[0])
            cJSON_AddStringToObject(llm, "protocol", cfg->llms[i].protocol);
        cJSON_AddStringToObject(llm, "api_key", cfg->llms[i].api_key);
        cJSON_AddStringToObject(llm, "api_base", cfg->llms[i].api_base);
        cJSON_AddStringToObject(llm, "model", cfg->llms[i].model);
        if (cfg->llms[i].timeout_secs > 0)
            cJSON_AddNumberToObject(llm, "timeout", cfg->llms[i].timeout_secs);
        if (cfg->llms[i].max_tokens > 0)
            cJSON_AddNumberToObject(llm, "max_tokens", cfg->llms[i].max_tokens);
        if (cfg->llms[i].temperature > 0.0f)
            cJSON_AddNumberToObject(llm, "temperature", cfg->llms[i].temperature);
        if (cfg->llms[i].system_prompt[0])
            cJSON_AddStringToObject(llm, "system_prompt", cfg->llms[i].system_prompt);
        if (cfg->llms[i].system_prompt_file[0])
            cJSON_AddStringToObject(llm, "system_prompt_file", cfg->llms[i].system_prompt_file);
        cJSON_AddItemToObject(llms, cfg->llms[i].name, llm);
    }

    if (cfg->active_llm[0])
        cJSON_AddStringToObject(json, "active_llm", cfg->active_llm);

    {
        cJSON *exp = cJSON_AddObjectToObject(json, "export");
        cJSON *sec_obj = NULL;
        const char *sec_name = NULL;
        for (size_t i = 0; i < N_EXPORT_FIELDS; i++) {
            const ExportField *f = &EXPORT_FIELDS[i];
            cJSON *parent = exp;
            if (f->section) {   /* table is grouped: one sub-object per section run */
                if (!sec_name || strcmp(sec_name, f->section) != 0) {
                    sec_obj = cJSON_AddObjectToObject(exp, f->section);
                    sec_name = f->section;
                }
                parent = sec_obj;
            }
            const char *p = (const char *)cfg + f->offset;
            switch (f->type) {
            case CF_BOOL:  cJSON_AddBoolToObject(parent, f->key, *(const int *)p); break;
            case CF_INT:   cJSON_AddNumberToObject(parent, f->key, *(const int *)p); break;
            case CF_FLOAT: cJSON_AddNumberToObject(parent, f->key, *(const float *)p); break;
            case CF_STR:   cJSON_AddStringToObject(parent, f->key, p); break;
            }
        }
    }

    if (cfg->subscription_count > 0) {
        cJSON *subs = cJSON_AddArrayToObject(json, "subscriptions");
        for (int i = 0; i < cfg->subscription_count; i++) {
            const Subscription *s = &cfg->subscriptions[i];
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "repo", s->repo);
            if (s->alias[0]) cJSON_AddStringToObject(o, "alias", s->alias);
            if (s->platform[0]) cJSON_AddStringToObject(o, "platform", s->platform);
            if (s->days > 0) cJSON_AddNumberToObject(o, "days", s->days);
            if (s->format[0]) cJSON_AddStringToObject(o, "format", s->format);
            if (s->branch[0]) cJSON_AddStringToObject(o, "branch", s->branch);
            cJSON_AddItemToArray(subs, o);
        }
    }

    FILE *fp = fopen(path, "w");
    if (!fp) { cJSON_Delete(json); return ERR_CONFIG_WRITE; }

    char *str = cJSON_Print(json);
    fprintf(fp, "%s\n", str);
    free(str);
    cJSON_Delete(json);
    fclose(fp);
    return GS_OK;
}

ErrorCode config_save_default(const char *path) {
    Config cfg;
    config_init(&cfg);
    cfg.llm_count = 2;

    snprintf(cfg.llms[0].name, sizeof(cfg.llms[0].name), "default");
    snprintf(cfg.llms[0].api_base, sizeof(cfg.llms[0].api_base), "https://api.openai.com/v1");
    snprintf(cfg.llms[0].model, sizeof(cfg.llms[0].model), "gpt-4o-mini");
    cfg.llms[0].timeout_secs = 60;
    cfg.llms[0].max_tokens = 1024;
    cfg.llms[0].temperature = 0.3f;

    snprintf(cfg.llms[1].name, sizeof(cfg.llms[1].name), "local");
    snprintf(cfg.llms[1].api_key, sizeof(cfg.llms[1].api_key), "not-needed");
    snprintf(cfg.llms[1].api_base, sizeof(cfg.llms[1].api_base), "http://localhost:11434/v1");
    snprintf(cfg.llms[1].model, sizeof(cfg.llms[1].model), "llama3.2");
    cfg.llms[1].timeout_secs = 120;
    cfg.llms[1].max_tokens = 1024;
    cfg.llms[1].temperature = 0.3f;

    snprintf(cfg.active_llm, sizeof(cfg.active_llm), "default");

    return config_save(path, &cfg);
}

ErrorCode config_set_token(const char *path, const char *token) {
    Config cfg;
    config_init(&cfg);
    config_load(&cfg, path);
    snprintf(cfg.github_token, sizeof(cfg.github_token), "%s", token);
    return config_save(path, &cfg);
}

ErrorCode config_set_active_llm(const char *path, const char *name) {
    Config cfg;
    config_init(&cfg);
    config_load(&cfg, path);

    int found = 0;
    for (int i = 0; i < cfg.llm_count; i++) {
        if (strcmp(cfg.llms[i].name, name) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) return ERR_LLM_CONFIG;

    snprintf(cfg.active_llm, sizeof(cfg.active_llm), "%s", name);
    return config_save(path, &cfg);
}

static int set_top_level(Config *cfg, const char *key, const char *val) {
    if (strcmp(key, "platform") == 0) {
        if (strcmp(val, "github") != 0 && strcmp(val, "gitlab") != 0 &&
            strcmp(val, "codeberg") != 0 && strcmp(val, "gitea") != 0 &&
            strcmp(val, "forgejo") != 0)
            return 0;
        snprintf(cfg->platform, sizeof(cfg->platform), "%s", val);
        apply_platform_defaults(cfg);
    } else if (strcmp(key, "lang") == 0) {
        if (strcmp(val, "en") != 0 && strcmp(val, "zh") != 0) return 0;
        snprintf(cfg->lang, sizeof(cfg->lang), "%s", val);
    } else if (strcmp(key, "default_days") == 0) {
        int v = atoi(val);
        if (v <= 0) return 0;
        cfg->default_days = v;
    } else if (strcmp(key, "max_pages") == 0) {
        int v = atoi(val);
        if (v <= 0) return 0;
        cfg->max_pages = v;
    } else if (strcmp(key, "format") == 0) {
        if (strcmp(val, "json") && strcmp(val, "md") && strcmp(val, "txt") &&
            strcmp(val, "xlsx") && strcmp(val, "pdf") && strcmp(val, "html") &&
            strcmp(val, "csv"))
            return 0;
        snprintf(cfg->format, sizeof(cfg->format), "%s", val);
    } else if (strcmp(key, "color") == 0) {
        if (strcmp(val, "always") == 0) cfg->color_mode = 1;
        else if (strcmp(val, "never") == 0) cfg->color_mode = 2;
        else if (strcmp(val, "auto") == 0) cfg->color_mode = 0;
        else return 0;
    } else if (strcmp(key, "api_base") == 0) {
        snprintf(cfg->api_base, sizeof(cfg->api_base), "%s", val);
    } else if (strcmp(key, "proxy") == 0) {
        snprintf(cfg->proxy, sizeof(cfg->proxy), "%s", val);
    } else if (strcmp(key, "github_token") == 0) {
        snprintf(cfg->github_token, sizeof(cfg->github_token), "%s", val);
    } else if (strcmp(key, "gitlab_token") == 0) {
        snprintf(cfg->gitlab_token, sizeof(cfg->gitlab_token), "%s", val);
    } else if (strcmp(key, "codeberg_token") == 0) {
        snprintf(cfg->codeberg_token, sizeof(cfg->codeberg_token), "%s", val);
    } else if (strcmp(key, "active_llm") == 0) {
        int found = 0;
        for (int i = 0; i < cfg->llm_count; i++)
            if (strcmp(cfg->llms[i].name, val) == 0) found = 1;
        if (!found) return 0;
        snprintf(cfg->active_llm, sizeof(cfg->active_llm), "%s", val);
    } else if (strncmp(key, "llms.", 5) == 0) {
        const char *rest = key + 5;
        const char *dot = strchr(rest, '.');
        if (!dot) return 0;
        char name[64];
        size_t nl = (size_t)(dot - rest);
        if (nl >= sizeof(name)) return 0;
        memcpy(name, rest, nl);
        name[nl] = '\0';
        const char *field = dot + 1;
        for (int i = 0; i < cfg->llm_count; i++) {
            if (strcmp(cfg->llms[i].name, name) != 0) continue;
            if (strcmp(field, "api_key") == 0)
                snprintf(cfg->llms[i].api_key, sizeof(cfg->llms[i].api_key), "%s", val);
            else if (strcmp(field, "api_base") == 0)
                snprintf(cfg->llms[i].api_base, sizeof(cfg->llms[i].api_base), "%s", val);
            else if (strcmp(field, "model") == 0)
                snprintf(cfg->llms[i].model, sizeof(cfg->llms[i].model), "%s", val);
            else if (strcmp(field, "protocol") == 0) {
                if (strcmp(val, "openai") != 0 && strcmp(val, "anthropic") != 0 &&
                    strcmp(val, "gemini") != 0)
                    return 0;
                snprintf(cfg->llms[i].protocol, sizeof(cfg->llms[i].protocol), "%s", val);
            }
            else
                return 0;
            return 1;
        }
        return 0;
    } else {
        return 0;
    }
    return 1;
}

/* Provider names appear as JSON object keys and on the command line, so keep
 * them to a conservative character set. */
static int llm_name_valid(const char *name) {
    if (!name || !*name) return 0;
    size_t n = strlen(name);
    if (n >= sizeof(((LLMConfig *)0)->name)) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

static int llm_protocol_valid(const char *protocol) {
    if (!protocol || !*protocol) return 1;   /* empty means "infer from api_base" */
    return strcmp(protocol, "openai") == 0 ||
           strcmp(protocol, "anthropic") == 0 ||
           strcmp(protocol, "gemini") == 0;
}

ErrorCode config_set_llm(const char *path, const LLMConfig *llm) {
    if (!llm) return ERR_INVALID_ARGS;
    if (!llm_name_valid(llm->name)) return ERR_INVALID_ARGS;
    if (!llm_protocol_valid(llm->protocol)) return ERR_INVALID_ARGS;
    if (llm->timeout_secs < 0 || llm->timeout_secs > 3600) return ERR_INVALID_ARGS;
    if (llm->max_tokens < 0 || llm->max_tokens > 1000000) return ERR_INVALID_ARGS;
    if (!(llm->temperature >= 0.0f && llm->temperature <= 2.0f)) return ERR_INVALID_ARGS;

    Config cfg;
    config_init(&cfg);
    config_load(&cfg, path);

    int idx = -1;
    for (int i = 0; i < cfg.llm_count; i++)
        if (strcmp(cfg.llms[i].name, llm->name) == 0) { idx = i; break; }
    if (idx < 0) {
        if (cfg.llm_count >= MAX_LLMS) return ERR_INVALID_ARGS;
        idx = cfg.llm_count++;
        memset(&cfg.llms[idx], 0, sizeof(cfg.llms[idx]));
        snprintf(cfg.llms[idx].name, sizeof(cfg.llms[idx].name), "%s", llm->name);
        /* A newly created provider with no key cannot be selected yet, so
         * make it active only once it has both endpoint fields. */
        if (llm->api_key[0] && llm->api_base[0] && !cfg.active_llm[0])
            snprintf(cfg.active_llm, sizeof(cfg.active_llm), "%s", llm->name);
    }

    LLMConfig *dst = &cfg.llms[idx];
    snprintf(dst->api_key, sizeof(dst->api_key), "%s", llm->api_key);
    snprintf(dst->api_base, sizeof(dst->api_base), "%s", llm->api_base);
    snprintf(dst->model, sizeof(dst->model), "%s", llm->model);
    snprintf(dst->protocol, sizeof(dst->protocol), "%s", llm->protocol);
    snprintf(dst->system_prompt, sizeof(dst->system_prompt), "%s", llm->system_prompt);
    snprintf(dst->system_prompt_file, sizeof(dst->system_prompt_file), "%s", llm->system_prompt_file);
    dst->timeout_secs = llm->timeout_secs;
    dst->max_tokens = llm->max_tokens;
    dst->temperature = llm->temperature;

    return config_save(path, &cfg);
}

ErrorCode config_set(const char *path, const char *key, const char *value) {
    if (!key || !*key || !value || !*value) return ERR_INVALID_ARGS;

    Config cfg;
    config_init(&cfg);
    config_load(&cfg, path);

    if (strncmp(key, "export.", 7) == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s=%s", key + 7, value);
        apply_export_override(&cfg, buf);
    } else if (!set_top_level(&cfg, key, value)) {
        return ERR_INVALID_ARGS;
    }

    return config_save(path, &cfg);
}

/* ---- Subscriptions ---- */

int config_find_subscription(const Config *cfg, const char *ref) {
    if (!ref || !ref[0]) return -1;
    for (int i = 0; i < cfg->subscription_count; i++) {
        const Subscription *s = &cfg->subscriptions[i];
        if (strcmp(s->repo, ref) == 0) return i;
        if (s->alias[0] && strcmp(s->alias, ref) == 0) return i;
    }
    return -1;
}

ErrorCode config_add_subscription(const char *path, const Subscription *sub) {
    if (!sub || !sub->repo[0]) return ERR_INVALID_ARGS;

    Config cfg;
    config_init(&cfg);
    config_load(&cfg, path);

    if (config_find_subscription(&cfg, sub->repo) >= 0)
        return ERR_INVALID_ARGS;

    if (sub->alias[0] && config_find_subscription(&cfg, sub->alias) >= 0)
        return ERR_INVALID_ARGS;

    if (cfg.subscription_count >= MAX_SUBSCRIPTIONS)
        return ERR_MEMORY;

    cfg.subscriptions[cfg.subscription_count++] = *sub;
    return config_save(path, &cfg);
}

ErrorCode config_remove_subscription(const char *path, const char *ref) {
    Config cfg;
    config_init(&cfg);
    config_load(&cfg, path);

    int idx = config_find_subscription(&cfg, ref);
    if (idx < 0) return ERR_INVALID_ARGS;

    for (int i = idx; i < cfg.subscription_count - 1; i++)
        cfg.subscriptions[i] = cfg.subscriptions[i + 1];
    cfg.subscription_count--;

    return config_save(path, &cfg);
}

ErrorCode config_update_subscription(const char *path, const char *ref,
                                     const Subscription *changes)
{
    Config cfg;
    config_init(&cfg);
    config_load(&cfg, path);

    int idx = config_find_subscription(&cfg, ref);
    if (idx < 0) return ERR_INVALID_ARGS;

    Subscription *s = &cfg.subscriptions[idx];
    if (changes->repo[0] && strcmp(changes->repo, s->repo) != 0) {
        if (config_find_subscription(&cfg, changes->repo) >= 0)
            return ERR_INVALID_ARGS;
        snprintf(s->repo, sizeof(s->repo), "%s", changes->repo);
    }
    if (changes->alias[0]) {
        if (strcmp(changes->alias, "-") == 0)
            s->alias[0] = '\0';
        else
            snprintf(s->alias, sizeof(s->alias), "%s", changes->alias);
    }
    if (changes->platform[0])
        snprintf(s->platform, sizeof(s->platform), "%s", changes->platform);
    if (changes->days > 0)
        s->days = changes->days;
    if (changes->format[0])
        snprintf(s->format, sizeof(s->format), "%s", changes->format);
    if (changes->branch[0])
        snprintf(s->branch, sizeof(s->branch), "%s", changes->branch);

    return config_save(path, &cfg);
}
