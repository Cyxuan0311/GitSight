#include "gitsight/config.h"
#include "gitsight/error.h"
#include "gitsight/color.h"
#include "gitsight/tui.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

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

void config_init(Config *cfg) {
    memset(cfg, 0, sizeof(Config));
    snprintf(cfg->platform, sizeof(cfg->platform), "github");
    snprintf(cfg->api_base, sizeof(cfg->api_base), "https://api.github.com");
    cfg->default_days = 7;
    cfg->max_pages = 20;
    cfg->color_mode = 0;
    snprintf(cfg->format, sizeof(cfg->format), "md");

    cfg->show_prs = cfg->show_commits = cfg->show_issues = 1;
    cfg->show_summary = cfg->show_top_labels = 1;
    cfg->show_releases = cfg->show_contributors = 1;
    cfg->item_limit = 10;
    cfg->md_table_width = 60;
    cfg->txt_table_width = 56;
    cfg->xlsx_font_size = 11;
    snprintf(cfg->xlsx_font_name, sizeof(cfg->xlsx_font_name), "Calibri");
    cfg->xlsx_col_a_width = 20.0f;
    cfg->xlsx_col_b_width = 60.0f;
    cfg->xlsx_border = 1;
    snprintf(cfg->xlsx_sheet_name, sizeof(cfg->xlsx_sheet_name), "Report");
    cfg->pdf_margin = 50.0f;
    cfg->pdf_line_height = 1.4f;
    cfg->pdf_col2_padding = 12.0f;
    snprintf(cfg->pdf_title_color, sizeof(cfg->pdf_title_color), "25,55,160");
    snprintf(cfg->pdf_header_color, sizeof(cfg->pdf_header_color), "25,55,160");
    snprintf(cfg->pdf_label_color, sizeof(cfg->pdf_label_color), "90,90,90");

    cfg->subscription_count = 0;
}

static void parse_llm_object(cJSON *obj, LLMConfig *llm) {
    cJSON *item;

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

    item = cJSON_GetObjectItem(json, "github_token");
    if (item && cJSON_IsString(item))
        snprintf(cfg->github_token, sizeof(cfg->github_token), "%s", item->valuestring);

    item = cJSON_GetObjectItem(json, "gitlab_token");
    if (item && cJSON_IsString(item))
        snprintf(cfg->gitlab_token, sizeof(cfg->gitlab_token), "%s", item->valuestring);

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

    /* Parse export config */
    cJSON *export_obj = cJSON_GetObjectItem(json, "export");
    if (export_obj && cJSON_IsObject(export_obj)) {
        item = cJSON_GetObjectItem(export_obj, "show_prs");
        if (item && cJSON_IsBool(item)) cfg->show_prs = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(export_obj, "show_commits");
        if (item && cJSON_IsBool(item)) cfg->show_commits = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(export_obj, "show_issues");
        if (item && cJSON_IsBool(item)) cfg->show_issues = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(export_obj, "show_summary");
        if (item && cJSON_IsBool(item)) cfg->show_summary = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(export_obj, "show_top_labels");
        if (item && cJSON_IsBool(item)) cfg->show_top_labels = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(export_obj, "show_releases");
        if (item && cJSON_IsBool(item)) cfg->show_releases = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(export_obj, "show_contributors");
        if (item && cJSON_IsBool(item)) cfg->show_contributors = cJSON_IsTrue(item);

        item = cJSON_GetObjectItem(export_obj, "item_limit");
        if (item && cJSON_IsNumber(item)) cfg->item_limit = item->valueint;

        cJSON *sub;
        sub = cJSON_GetObjectItem(export_obj, "md");
        if (sub && cJSON_IsObject(sub)) {
            item = cJSON_GetObjectItem(sub, "table_width");
            if (item && cJSON_IsNumber(item)) cfg->md_table_width = item->valueint;
        }
        sub = cJSON_GetObjectItem(export_obj, "txt");
        if (sub && cJSON_IsObject(sub)) {
            item = cJSON_GetObjectItem(sub, "table_width");
            if (item && cJSON_IsNumber(item)) cfg->txt_table_width = item->valueint;
        }
        sub = cJSON_GetObjectItem(export_obj, "xlsx");
        if (sub && cJSON_IsObject(sub)) {
            item = cJSON_GetObjectItem(sub, "font_size");
            if (item && cJSON_IsNumber(item)) cfg->xlsx_font_size = item->valueint;
            item = cJSON_GetObjectItem(sub, "font_name");
            if (item && cJSON_IsString(item)) snprintf(cfg->xlsx_font_name, sizeof(cfg->xlsx_font_name), "%s", item->valuestring);
            item = cJSON_GetObjectItem(sub, "col_a_width");
            if (item && cJSON_IsNumber(item)) cfg->xlsx_col_a_width = (float)item->valuedouble;
            item = cJSON_GetObjectItem(sub, "col_b_width");
            if (item && cJSON_IsNumber(item)) cfg->xlsx_col_b_width = (float)item->valuedouble;
            item = cJSON_GetObjectItem(sub, "border");
            if (item && cJSON_IsBool(item)) cfg->xlsx_border = cJSON_IsTrue(item);
            item = cJSON_GetObjectItem(sub, "sheet_name");
            if (item && cJSON_IsString(item)) snprintf(cfg->xlsx_sheet_name, sizeof(cfg->xlsx_sheet_name), "%s", item->valuestring);
        }
        sub = cJSON_GetObjectItem(export_obj, "pdf");
        if (sub && cJSON_IsObject(sub)) {
            item = cJSON_GetObjectItem(sub, "margin");
            if (item && cJSON_IsNumber(item)) cfg->pdf_margin = (float)item->valuedouble;
            item = cJSON_GetObjectItem(sub, "line_height");
            if (item && cJSON_IsNumber(item)) cfg->pdf_line_height = (float)item->valuedouble;
            item = cJSON_GetObjectItem(sub, "col2_padding");
            if (item && cJSON_IsNumber(item)) cfg->pdf_col2_padding = (float)item->valuedouble;
            item = cJSON_GetObjectItem(sub, "title_color");
            if (item && cJSON_IsString(item)) snprintf(cfg->pdf_title_color, sizeof(cfg->pdf_title_color), "%s", item->valuestring);
            item = cJSON_GetObjectItem(sub, "header_color");
            if (item && cJSON_IsString(item)) snprintf(cfg->pdf_header_color, sizeof(cfg->pdf_header_color), "%s", item->valuestring);
            item = cJSON_GetObjectItem(sub, "label_color");
            if (item && cJSON_IsString(item)) snprintf(cfg->pdf_label_color, sizeof(cfg->pdf_label_color), "%s", item->valuestring);
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
            if (sub->repo[0] == '\0') continue;
            cfg->subscription_count++;
        }
    }

    cJSON_Delete(json);
    return GS_OK;
}

static int parse_bool(const char *s) {
    return strcmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcmp(s, "yes") == 0;
}

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

    if (strcmp(key, "show_prs") == 0) cfg->show_prs = parse_bool(val);
    else if (strcmp(key, "show_commits") == 0) cfg->show_commits = parse_bool(val);
    else if (strcmp(key, "show_issues") == 0) cfg->show_issues = parse_bool(val);
    else if (strcmp(key, "show_summary") == 0) cfg->show_summary = parse_bool(val);
    else if (strcmp(key, "show_top_labels") == 0) cfg->show_top_labels = parse_bool(val);
    else if (strcmp(key, "show_releases") == 0) cfg->show_releases = parse_bool(val);
    else if (strcmp(key, "show_contributors") == 0) cfg->show_contributors = parse_bool(val);
    else if (strcmp(key, "item_limit") == 0) { int v = atoi(val); if (v >= 0) cfg->item_limit = v; }
    else if (strcmp(key, "md.table_width") == 0) { int v = atoi(val); if (v > 0) cfg->md_table_width = v; }
    else if (strcmp(key, "txt.table_width") == 0) { int v = atoi(val); if (v > 0) cfg->txt_table_width = v; }
    else if (strcmp(key, "xlsx.font_size") == 0) { int v = atoi(val); if (v > 0) cfg->xlsx_font_size = v; }
    else if (strcmp(key, "xlsx.font_name") == 0) snprintf(cfg->xlsx_font_name, sizeof(cfg->xlsx_font_name), "%s", val);
    else if (strcmp(key, "xlsx.col_a_width") == 0) { float v = (float)atof(val); if (v > 0) cfg->xlsx_col_a_width = v; }
    else if (strcmp(key, "xlsx.col_b_width") == 0) { float v = (float)atof(val); if (v > 0) cfg->xlsx_col_b_width = v; }
    else if (strcmp(key, "xlsx.border") == 0) cfg->xlsx_border = parse_bool(val);
    else if (strcmp(key, "xlsx.sheet_name") == 0) snprintf(cfg->xlsx_sheet_name, sizeof(cfg->xlsx_sheet_name), "%s", val);
    else if (strcmp(key, "pdf.margin") == 0) { float v = (float)atof(val); if (v > 0) cfg->pdf_margin = v; }
    else if (strcmp(key, "pdf.line_height") == 0) { float v = (float)atof(val); if (v > 0) cfg->pdf_line_height = v; }
    else if (strcmp(key, "pdf.col2_padding") == 0) { float v = (float)atof(val); if (v >= 0) cfg->pdf_col2_padding = v; }
    else if (strcmp(key, "pdf.title_color") == 0) snprintf(cfg->pdf_title_color, sizeof(cfg->pdf_title_color), "%s", val);
    else if (strcmp(key, "pdf.header_color") == 0) snprintf(cfg->pdf_header_color, sizeof(cfg->pdf_header_color), "%s", val);
    else if (strcmp(key, "pdf.label_color") == 0) snprintf(cfg->pdf_label_color, sizeof(cfg->pdf_label_color), "%s", val);
}

void config_merge(Config *cfg,
                  int since_days, int max_pages, const char *format,
                  const char *output, const char *proxy,
                  const char *api_base, int color_mode,
                  const char *token, const char *llm_name,
                  const char *platform)
{
    if (since_days > 0) cfg->default_days = since_days;
    if (max_pages > 0) cfg->max_pages = max_pages;
    if (format && format[0]) snprintf(cfg->format, sizeof(cfg->format), "%s", format);
    if (output && output[0]) snprintf(cfg->output, sizeof(cfg->output), "%s", output);
    if (proxy && proxy[0])   snprintf(cfg->proxy, sizeof(cfg->proxy), "%s", proxy);
    if (api_base && api_base[0]) snprintf(cfg->api_base, sizeof(cfg->api_base), "%s", api_base);
    if (color_mode >= 0) cfg->color_mode = color_mode;
    if (platform && platform[0]) snprintf(cfg->platform, sizeof(cfg->platform), "%s", platform);

    if (strcmp(cfg->platform, "gitlab") == 0) {
        if (token && token[0])
            snprintf(cfg->gitlab_token, sizeof(cfg->gitlab_token), "%s", token);
    } else {
        if (token && token[0])
            snprintf(cfg->github_token, sizeof(cfg->github_token), "%s", token);
    }

    const char *env_token = getenv("GITHUB_TOKEN");
    const char *env_gl_token = getenv("GITLAB_TOKEN");
    const char *env_proxy = getenv("HTTPS_PROXY");
    const char *env_api   = getenv("GS_API_BASE");

    if (strcmp(cfg->platform, "gitlab") == 0) {
        if (!cfg->gitlab_token[0] && env_gl_token)
            snprintf(cfg->gitlab_token, sizeof(cfg->gitlab_token), "%s", env_gl_token);
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

    if (env_llm_key || env_llm_base || env_llm_model) {
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
    if (cfg->export_extra[0]) {
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
    const char *cells[4];
    tui_table_init(&t, 4);
    tui_table_header(&t, 0, "Active");
    tui_table_header(&t, 1, "Name");
    tui_table_header(&t, 2, "Model");
    tui_table_header(&t, 3, "API Base");
    tui_table_align(&t, 0, TUI_ALIGN_CENTER);
    tui_table_cell_color(&t, 1, CLR_GREEN);
    for (int i = 0; i < cfg->llm_count; i++) {
        const LLMConfig *l = &cfg->llms[i];
        int is_active = strcmp(l->name, cfg->active_llm) == 0;
        snprintf(active[i], sizeof(active[i]), "%s", is_active ? "*" : "");
        cells[0] = active[i];
        cells[1] = l->name;
        cells[2] = l->model[0] ? l->model : "-";
        cells[3] = l->api_base[0] ? l->api_base : "-";
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
    cJSON_AddStringToObject(json, "github_token", cfg->github_token);
    cJSON_AddStringToObject(json, "gitlab_token", cfg->gitlab_token);
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
        cJSON_AddBoolToObject(exp, "show_prs", cfg->show_prs);
        cJSON_AddBoolToObject(exp, "show_commits", cfg->show_commits);
        cJSON_AddBoolToObject(exp, "show_issues", cfg->show_issues);
        cJSON_AddBoolToObject(exp, "show_summary", cfg->show_summary);
        cJSON_AddBoolToObject(exp, "show_top_labels", cfg->show_top_labels);
        cJSON_AddBoolToObject(exp, "show_releases", cfg->show_releases);
        cJSON_AddBoolToObject(exp, "show_contributors", cfg->show_contributors);
        cJSON_AddNumberToObject(exp, "item_limit", cfg->item_limit);

        cJSON *md = cJSON_AddObjectToObject(exp, "md");
        cJSON_AddNumberToObject(md, "table_width", cfg->md_table_width);

        cJSON *txt = cJSON_AddObjectToObject(exp, "txt");
        cJSON_AddNumberToObject(txt, "table_width", cfg->txt_table_width);

        cJSON *xlsx = cJSON_AddObjectToObject(exp, "xlsx");
        cJSON_AddNumberToObject(xlsx, "font_size", cfg->xlsx_font_size);
        cJSON_AddStringToObject(xlsx, "font_name", cfg->xlsx_font_name);
        cJSON_AddNumberToObject(xlsx, "col_a_width", cfg->xlsx_col_a_width);
        cJSON_AddNumberToObject(xlsx, "col_b_width", cfg->xlsx_col_b_width);
        cJSON_AddBoolToObject(xlsx, "border", cfg->xlsx_border);
        cJSON_AddStringToObject(xlsx, "sheet_name", cfg->xlsx_sheet_name);

        cJSON *pdf = cJSON_AddObjectToObject(exp, "pdf");
        cJSON_AddNumberToObject(pdf, "margin", cfg->pdf_margin);
        cJSON_AddNumberToObject(pdf, "line_height", cfg->pdf_line_height);
        cJSON_AddNumberToObject(pdf, "col2_padding", cfg->pdf_col2_padding);
        cJSON_AddStringToObject(pdf, "title_color", cfg->pdf_title_color);
        cJSON_AddStringToObject(pdf, "header_color", cfg->pdf_header_color);
        cJSON_AddStringToObject(pdf, "label_color", cfg->pdf_label_color);
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
        if (strcmp(val, "github") != 0 && strcmp(val, "gitlab") != 0) return 0;
        snprintf(cfg->platform, sizeof(cfg->platform), "%s", val);
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
            strcmp(val, "xlsx") && strcmp(val, "pdf"))
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

    return config_save(path, &cfg);
}
