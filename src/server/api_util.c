#include "api_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILTER_MAX 8
#define FILTER_VAL_LEN 64

int json_str(const cJSON *obj, const char *key, char *out, size_t len) {
    if (!obj || !key || !out || !len) return 0;
    out[0] = '\0';
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0]) return 0;
    snprintf(out, len, "%s", v->valuestring);
    return 1;
}

int json_int(const cJSON *obj, const char *key, int *out) {
    if (!obj || !key || !out) return 0;
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(v)) return 0;
    *out = (int)v->valuedouble;
    return 1;
}

int json_bool(const cJSON *obj, const char *key, int *out) {
    if (!obj || !key || !out) return 0;
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) return 0;
    if (cJSON_IsBool(v)) { *out = cJSON_IsTrue(v) ? 1 : 0; return 1; }
    if (cJSON_IsNumber(v)) { *out = v->valuedouble != 0; return 1; }
    return 0;
}

void options_from_config(Options *o, const Config *cfg) {
    if (!o || !cfg) return;
    memset(o, 0, sizeof(*o));
    o->color_mode = 2;            /* no terminal, so never emit ANSI */
    o->quiet = 1;
    o->max_pages = cfg->max_pages;
    o->since_days = cfg->default_days;
    snprintf(o->format, sizeof(o->format), "%s", cfg->format);
    snprintf(o->platform, sizeof(o->platform), "%s", cfg->platform);
    snprintf(o->lang, sizeof(o->lang), "%s", cfg->lang);
    snprintf(o->proxy, sizeof(o->proxy), "%s", cfg->proxy);
    snprintf(o->api_base, sizeof(o->api_base), "%s", cfg->api_base);
    snprintf(o->branch, sizeof(o->branch), "%s", cfg->branch);
    snprintf(o->llm_name, sizeof(o->llm_name), "%s", cfg->active_llm);
}

/* Replaces the existing contents of a newline-joined list field. */
static void list_append(char *dst, size_t cap, const char *value) {
    size_t used = strlen(dst);
    if (used == 0) { snprintf(dst, cap, "%s", value); return; }
    snprintf(dst + used, cap - used, "\n%s", value);
}

int options_export_overrides(const cJSON *body, char *out, size_t outlen,
                             char *err, size_t errlen) {
    if (!out || !outlen) return -1;
    out[0] = '\0';
    if (!body) return 0;

    const cJSON *node = cJSON_GetObjectItem(body, "export");
    if (!node) return 0;

    /* Array form: ["pdf.margin=40", "show_commits=false"] */
    if (cJSON_IsArray(node)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, node) {
            if (!cJSON_IsString(item) || !item->valuestring) continue;
            if (!strchr(item->valuestring, '=')) {
                snprintf(err, errlen, "export override '%s' is missing '='",
                         item->valuestring);
                return -1;
            }
            list_append(out, outlen, item->valuestring);
        }
        return 0;
    }

    /* Object form: {"pdf": {"margin": 40}, "show_commits": false} */
    if (!cJSON_IsObject(node)) {
        snprintf(err, errlen, "export overrides must be an object or an array");
        return -1;
    }
    char flat[2048];
    flat[0] = '\0';
    cJSON *group = NULL;
    cJSON_ArrayForEach(group, node) {
        if (!group->string) continue;
        if (cJSON_IsObject(group)) {
            cJSON *leaf = NULL;
            cJSON_ArrayForEach(leaf, group) {
                if (!leaf->string) continue;
                char value[256];
                if (cJSON_IsString(leaf)) snprintf(value, sizeof(value), "%s", leaf->valuestring);
                else if (cJSON_IsBool(leaf)) snprintf(value, sizeof(value), "%s", cJSON_IsTrue(leaf) ? "true" : "false");
                else if (cJSON_IsNumber(leaf)) snprintf(value, sizeof(value), "%.10g", leaf->valuedouble);
                else continue;
                char pair[320];
                snprintf(pair, sizeof(pair), "%s.%s=%s", group->string, leaf->string, value);
                list_append(flat, sizeof(flat), pair);
            }
        } else if (cJSON_IsString(group)) {
            char pair[320];
            snprintf(pair, sizeof(pair), "%s=%s", group->string, group->valuestring);
            list_append(flat, sizeof(flat), pair);
        }
    }
    snprintf(out, outlen, "%s", flat);
    return 0;
}

int options_apply_json(const cJSON *body, Options *o, char *err, size_t errlen) {
    if (!o) return -1;
    if (!body) return 0;

    char buf[1024];
    int n;

    if (json_int(body, "since_days", &n)) {
        if (n <= 0) { snprintf(err, errlen, "since_days must be positive"); return -1; }
        o->since_days = n;
    }
    if (json_int(body, "max_pages", &n)) {
        if (n <= 0) { snprintf(err, errlen, "max_pages must be positive"); return -1; }
        o->max_pages = n;
    }
    if (json_int(body, "parallel", &n)) o->parallel = n;
    if (json_bool(body, "no_cache", &n)) o->no_cache = n;
    if (json_bool(body, "suppress_llm", &n)) o->suppress_llm = n;
    if (json_bool(body, "quiet", &n)) o->quiet = n;

    if (json_str(body, "from", buf, sizeof(buf))) {
        if (strlen(buf) != 10 || buf[4] != '-' || buf[7] != '-') {
            snprintf(err, errlen, "from expects YYYY-MM-DD");
            return -1;
        }
        snprintf(o->from, sizeof(o->from), "%s", buf);
    }
    if (json_str(body, "to", buf, sizeof(buf))) {
        if (strlen(buf) != 10 || buf[4] != '-' || buf[7] != '-') {
            snprintf(err, errlen, "to expects YYYY-MM-DD");
            return -1;
        }
        snprintf(o->to, sizeof(o->to), "%s", buf);
    }

    /* formats: ["json","pdf"] collapses to the comma list the CLI parses. */
    const cJSON *formats = cJSON_GetObjectItem(body, "formats");
    const cJSON *format = cJSON_GetObjectItem(body, "format");
    if (cJSON_IsArray(formats) || (cJSON_IsString(format) && strchr(format->valuestring, ','))) {
        char joined[256];
        joined[0] = '\0';
        if (cJSON_IsArray(formats)) {
            cJSON *f = NULL;
            cJSON_ArrayForEach(f, formats) {
                if (!cJSON_IsString(f) || !f->valuestring) continue;
                if (!format_is_known(f->valuestring)) {
                    snprintf(err, errlen, "unsupported format '%s'", f->valuestring);
                    return -1;
                }
                char next[256];
                snprintf(next, sizeof(next), "%s%s", joined[0] ? "," : "", f->valuestring);
                snprintf(joined, sizeof(joined), "%s", next);
            }
        } else {
            snprintf(joined, sizeof(joined), "%s", format->valuestring);
        }
        snprintf(o->format, sizeof(o->format), "%s", joined);
    } else if (json_str(body, "format", buf, sizeof(buf))) {
        if (!format_is_known(buf)) {
            snprintf(err, errlen, "unsupported format '%s'", buf);
            return -1;
        }
        snprintf(o->format, sizeof(o->format), "%s", buf);
    }

    if (json_str(body, "platform", buf, sizeof(buf))) {
        static const char *const known[] = { "github", "gitlab", "codeberg", "gitea", "forgejo" };
        int ok = 0;
        for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
            if (strcmp(buf, known[i]) == 0) { ok = 1; break; }
        if (!ok) {
            snprintf(err, errlen, "unknown platform '%s'", buf);
            return -1;
        }
        snprintf(o->platform, sizeof(o->platform), "%s", buf);
    }
    if (json_str(body, "lang", buf, sizeof(buf))) {
        snprintf(o->lang, sizeof(o->lang), "%s", buf);
    }
    if (json_str(body, "branch", buf, sizeof(buf))) {
        snprintf(o->branch, sizeof(o->branch), "%s", buf);
    }
    if (json_str(body, "api_base", buf, sizeof(buf))) {
        snprintf(o->api_base, sizeof(o->api_base), "%s", buf);
    }
    if (json_str(body, "proxy", buf, sizeof(buf))) {
        snprintf(o->proxy, sizeof(o->proxy), "%s", buf);
    }
    if (json_str(body, "token", buf, sizeof(buf))) {
        snprintf(o->token, sizeof(o->token), "%s", buf);
        o->set_token = 1;
    }
    if (json_str(body, "llm", buf, sizeof(buf))) {
        snprintf(o->llm_name, sizeof(o->llm_name), "%s", buf);
    }
    if (json_str(body, "llm_mode", buf, sizeof(buf))) {
        static const char *const modes[] = { "health", "trends", "release" };
        int ok = 0;
        for (size_t i = 0; i < 3; i++) if (strcmp(buf, modes[i]) == 0) { ok = 1; break; }
        if (!ok) {
            snprintf(err, errlen, "unknown llm_mode '%s'", buf);
            return -1;
        }
        snprintf(o->llm_mode, sizeof(o->llm_mode), "%s", buf);
    }

    /* filters: ["author:alice", "label:bug", "regression"] */
    const cJSON *filters = cJSON_GetObjectItem(body, "filters");
    if (cJSON_IsArray(filters)) {
        o->filters[0] = '\0';
        int count = 0;
        cJSON *f = NULL;
        cJSON_ArrayForEach(f, filters) {
            if (!cJSON_IsString(f) || !f->valuestring) continue;
            if (count >= FILTER_MAX * 3) {
                snprintf(err, errlen, "too many filters");
                return -1;
            }
            if (strlen(f->valuestring) > FILTER_VAL_LEN - 1) {
                snprintf(err, errlen, "filter '%s' is longer than %d characters",
                         f->valuestring, FILTER_VAL_LEN - 1);
                return -1;
            }
            list_append(o->filters, sizeof(o->filters), f->valuestring);
            count++;
        }
    }

    return options_export_overrides(body, o->export_extra,
                                    sizeof(o->export_extra), err, errlen);
}

void options_apply_subscription(Options *o, const Config *cfg, const Subscription *s) {
    if (!o || !cfg || !s) return;
    if (s->days > 0) o->since_days = s->days;
    if (s->platform[0]) snprintf(o->platform, sizeof(o->platform), "%s", s->platform);
    if (s->format[0]) snprintf(o->format, sizeof(o->format), "%s", s->format);
    if (s->branch[0]) snprintf(o->branch, sizeof(o->branch), "%s", s->branch);
}