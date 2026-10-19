#include "api_util.h"
#include "server.h"

#include <cJSON.h>
#include <stdio.h>
#include <string.h>

/* LLM provider CRUD. config_set() writes one `llms.<name>.<field>` key at a
 * time, which is exactly the shape this API accepts, so every mutation reuses
 * the CLI's validation instead of duplicating it. Deleting a provider rewrites
 * the llms object directly because there is no --set form for that. */

static cJSON *llm_json(const Config *cfg, const LLMConfig *llm) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", llm->name);
    cJSON_AddBoolToObject(o, "active",
                          strcmp(cfg->active_llm, llm->name) == 0);
    cJSON_AddStringToObject(o, "protocol", llm->protocol[0] ? llm->protocol : "openai");
    cJSON_AddStringToObject(o, "model", llm->model);
    cJSON_AddStringToObject(o, "api_base", llm->api_base);

    /* Keys are write-only: the UI shows whether one is set, never its value. */
    cJSON *key = cJSON_CreateObject();
    cJSON_AddBoolToObject(key, "set", llm->api_key[0] != '\0');
    cJSON_AddItemToObject(o, "api_key", key);

    cJSON_AddNumberToObject(o, "timeout_secs", llm->timeout_secs);
    cJSON_AddNumberToObject(o, "max_tokens", llm->max_tokens);
    cJSON_AddNumberToObject(o, "temperature", llm->temperature);
    cJSON_AddStringToObject(o, "system_prompt_file", llm->system_prompt_file);
    cJSON_AddNumberToObject(o, "system_prompt_len", (double)strlen(llm->system_prompt));
    /* A configured provider is only usable when both endpoint fields exist. */
    cJSON_AddBoolToObject(o, "usable",
                          llm->api_key[0] != '\0' && llm->api_base[0] != '\0');
    return o;
}

static cJSON *llm_list(const Config *cfg) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < cfg->llm_count; i++)
        cJSON_AddItemToArray(arr, llm_json(cfg, &cfg->llms[i]));
    return arr;
}

static void llm_list_response(Response *res) {
    Config cfg;
    server_load_config(&cfg);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "llms", llm_list(&cfg));
    cJSON_AddStringToObject(root, "active_llm", cfg.active_llm);
    cJSON_AddNumberToObject(root, "max", MAX_LLMS);
    res_json_obj(res, 200, root);
}

/* GET /api/llms */
static void handle_list(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    llm_list_response(res);
}

/* Reads the current provider so a partial PATCH keeps the untouched fields.
 * Returns 0 when the provider exists. */
static int llm_find(const Config *cfg, const char *name, LLMConfig *out) {
    for (int i = 0; i < cfg->llm_count; i++)
        if (strcmp(cfg->llms[i].name, name) == 0) { *out = cfg->llms[i]; return 1; }
    return 0;
}

/* Copies one field from the request onto the LLMConfig. A JSON null clears it. */
static int llm_field_from_json(const cJSON *body, const char *field, LLMConfig *llm) {
    const cJSON *v = cJSON_GetObjectItem(body, field);
    if (!v) return 0;

    if (strcmp(field, "timeout_secs") == 0 || strcmp(field, "max_tokens") == 0) {
        int *dst = strcmp(field, "max_tokens") == 0 ? &llm->max_tokens : &llm->timeout_secs;
        if (cJSON_IsNull(v)) { *dst = 0; return 1; }
        if (!cJSON_IsNumber(v)) return -1;
        *dst = (int)v->valuedouble;
        return 1;
    }
    if (strcmp(field, "temperature") == 0) {
        if (cJSON_IsNull(v)) { llm->temperature = 0.0f; return 1; }
        if (!cJSON_IsNumber(v)) return -1;
        llm->temperature = (float)v->valuedouble;
        return 1;
    }

    char *dst = NULL;
    size_t cap = 0;
    if (strcmp(field, "api_key") == 0)             { dst = llm->api_key;             cap = sizeof(llm->api_key); }
    else if (strcmp(field, "api_base") == 0)       { dst = llm->api_base;            cap = sizeof(llm->api_base); }
    else if (strcmp(field, "model") == 0)          { dst = llm->model;               cap = sizeof(llm->model); }
    else if (strcmp(field, "protocol") == 0)       { dst = llm->protocol;            cap = sizeof(llm->protocol); }
    else if (strcmp(field, "system_prompt") == 0)  { dst = llm->system_prompt;       cap = sizeof(llm->system_prompt); }
    else if (strcmp(field, "system_prompt_file") == 0) { dst = llm->system_prompt_file; cap = sizeof(llm->system_prompt_file); }
    else return -1;

    if (cJSON_IsNull(v)) { dst[0] = '\0'; return 1; }
    if (!cJSON_IsString(v) || !v->valuestring) return -1;
    /* Over-long values would be silently truncated by snprintf, so reject them. */
    if (strlen(v->valuestring) >= cap) return -1;
    snprintf(dst, cap, "%s", v->valuestring);
    return 1;
}

static const char *const k_llm_fields[] = {
    "api_key", "api_base", "model", "protocol",
    "system_prompt", "system_prompt_file",
    "timeout_secs", "max_tokens", "temperature"
};

static int llm_apply_fields(const cJSON *body, LLMConfig *llm, char *err, size_t errlen) {
    int touched = 0;
    for (size_t i = 0; i < sizeof(k_llm_fields) / sizeof(k_llm_fields[0]); i++) {
        int rc = llm_field_from_json(body, k_llm_fields[i], llm);
        if (rc < 0) {
            snprintf(err, errlen, "field '%s' is missing, mistyped, or too long",
                     k_llm_fields[i]);
            return -1;
        }
        touched += rc;
    }
    /* `timeout` is the config file's spelling; accept it as an alias. */
    const cJSON *t = cJSON_GetObjectItem(body, "timeout");
    if (t) {
        if (!cJSON_IsNumber(t)) { snprintf(err, errlen, "timeout must be a number"); return -1; }
        llm->timeout_secs = (int)t->valuedouble;
        touched++;
    }
    return touched;
}

static int llm_persist(const LLMConfig *llm, ErrorCode *out) {
    server_lock();
    *out = config_set_llm(g_server.config_path, llm);
    server_unlock();
    return *out == GS_OK;
}

/* POST /api/llms -- create or update in one call. */
static void handle_upsert(Request *req, Response *res, void *user) {
    (void)user;
    cJSON *body = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!body) { res_error(res, 400, "bad_json", "request body is not valid JSON"); return; }

    char name[64] = "";
    if (!json_str(body, "name", name, sizeof(name))) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", "name is required");
        return;
    }

    Config cfg;
    server_load_config(&cfg);
    LLMConfig llm;
    memset(&llm, 0, sizeof(llm));
    int exists = llm_find(&cfg, name, &llm);
    if (exists) snprintf(llm.name, sizeof(llm.name), "%s", name);

    char err[512] = "";
    int touched = llm_apply_fields(body, &llm, err, sizeof(err));
    if (touched < 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }
    if (touched == 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", "no writable fields were supplied");
        return;
    }
    snprintf(llm.name, sizeof(llm.name), "%s", name);

    int make_active = 0;
    json_bool(body, "active", &make_active);
    cJSON_Delete(body);

    ErrorCode e = GS_OK;
    if (!llm_persist(&llm, &e)) {
        if (e == ERR_INVALID_ARGS)
            res_error(res, 400, "validation_failed",
                      "unsupported name or protocol, or a numeric setting is out of range");
        else
            res_error_code(res, e, "could not save the provider");
        return;
    }

    if (make_active) {
        server_lock();
        config_set_active_llm(g_server.config_path, name);
        server_unlock();
    }
    llm_list_response(res);
}

/* PATCH /api/llms/:name -- partial update; absent fields keep their value. */
static void handle_patch(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing provider name");
        return;
    }
    char name[64];
    url_decode(rt->params[0], name, sizeof(name));

    Config cfg;
    server_load_config(&cfg);
    LLMConfig llm;
    if (!llm_find(&cfg, name, &llm)) {
        res_error(res, 404, "not_found", "no such LLM provider");
        return;
    }

    cJSON *body = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!body) { res_error(res, 400, "bad_json", "request body is not valid JSON"); return; }

    char err[512] = "";
    int touched = llm_apply_fields(body, &llm, err, sizeof(err));
    if (touched < 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }

    int make_active = 0;
    json_bool(body, "active", &make_active);
    cJSON_Delete(body);

    if (!touched && !make_active) {
        res_error(res, 400, "bad_request", "no writable fields were supplied");
        return;
    }

    if (touched) {
        ErrorCode e = GS_OK;
        if (!llm_persist(&llm, &e)) {
            if (e == ERR_INVALID_ARGS)
                res_error(res, 400, "validation_failed",
                          "unsupported protocol, or a numeric setting is out of range");
            else
                res_error_code(res, e, "could not save the provider");
            return;
        }
    }
    if (make_active) {
        server_lock();
        config_set_active_llm(g_server.config_path, name);
        server_unlock();
    }
    llm_list_response(res);
}

/* DELETE /api/llms/:name
 *
 * Rewrites the llms object because config_set has no delete form. The active
 * pointer is moved to a surviving provider first, since config_set would
 * otherwise refuse to leave it dangling. */
static void handle_delete(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing provider name");
        return;
    }
    char name[64];
    url_decode(rt->params[0], name, sizeof(name));

    Config cfg;
    server_load_config(&cfg);
    int idx = -1;
    for (int i = 0; i < cfg.llm_count; i++)
        if (strcmp(cfg.llms[i].name, name) == 0) idx = i;
    if (idx < 0) {
        res_error(res, 404, "not_found", "no such LLM provider");
        return;
    }

    int next_active = -1;
    for (int i = 0; i < cfg.llm_count; i++) {
        if (i == idx) continue;
        if (strcmp(cfg.active_llm, cfg.llms[i].name) == 0 && next_active < 0) next_active = i;
    }
    if (next_active < 0) {
        for (int i = 0; i < cfg.llm_count; i++)
            if (i != idx) { next_active = i; break; }
    }

    Config rebuilt = cfg;
    int w = 0;
    for (int i = 0; i < cfg.llm_count; i++) {
        if (i == idx) continue;
        rebuilt.llms[w++] = cfg.llms[i];
    }
    rebuilt.llm_count = w;
    if (next_active >= 0) snprintf(rebuilt.active_llm, sizeof(rebuilt.active_llm), "%s",
                                   cfg.llms[next_active].name);
    else rebuilt.active_llm[0] = '\0';

    server_lock();
    ErrorCode e = config_save(g_server.config_path, &rebuilt);
    server_unlock();
    if (e != GS_OK) {
        res_error_code(res, e, "could not persist the provider list");
        return;
    }
    llm_list_response(res);
}

/* POST /api/llms/:name/activate */
static void handle_activate(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing provider name");
        return;
    }
    char name[64];
    url_decode(rt->params[0], name, sizeof(name));

    server_lock();
    ErrorCode e = config_set_active_llm(g_server.config_path, name);
    server_unlock();
    if (e != GS_OK) {
        res_error(res, 404, "not_found", "no such LLM provider");
        return;
    }
    llm_list_response(res);
}

/* POST /api/llms/:name/test -- a minimal generation round-trip. Confirms the
 * key, endpoint and protocol actually work together, which config validation
 * alone cannot tell you. */
static void handle_test(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing provider name");
        return;
    }
    char name[64];
    url_decode(rt->params[0], name, sizeof(name));

    Config cfg;
    server_load_config(&cfg);
    LLMConfig found;
    if (!llm_find(&cfg, name, &found)) {
        res_error(res, 404, "not_found", "no such LLM provider");
        return;
    }
    const LLMConfig *llm = &found;
    if (!llm->api_key[0] || !llm->api_base[0]) {
        res_error(res, 424, "not_configured",
                  "the provider needs both an API key and an API base");
        return;
    }

    char out[1024] = "";
    ErrorCode e = llm_generate(llm, "Reply with the single word: ok",
                               out, sizeof(out));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "protocol", llm_protocol(llm));
    cJSON_AddBoolToObject(root, "ok", e == GS_OK && out[0] != '\0');
    if (e == GS_OK) cJSON_AddStringToObject(root, "reply", out);
    else cJSON_AddStringToObject(root, "error", gs_error_string(e));
    res_json_obj(res, e == GS_OK ? 200 : 502, root);
}

void api_register_llms(Router *r) {
    router_add(r, "GET",    "/api/llms", handle_list, NULL);
    router_add(r, "POST",   "/api/llms", handle_upsert, NULL);
    router_add(r, "PATCH",  "/api/llms/:name", handle_patch, NULL);
    router_add(r, "DELETE", "/api/llms/:name", handle_delete, NULL);
    router_add(r, "POST",   "/api/llms/:name/activate", handle_activate, NULL);
    router_add(r, "POST",   "/api/llms/:name/test", handle_test, NULL);
}