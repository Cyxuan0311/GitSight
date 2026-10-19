#include "api_util.h"
#include "jobs.h"

#include <cJSON.h>
#include <stdio.h>
#include <string.h>

/* Subscriptions are the browser-facing view of `gs watch`. Every mutation goes
 * through the same config_* subscription helpers the CLI uses, so uniqueness
 * rules and the repo/alias reference matching stay in one place. */

static void list_response(Response *res) {
    Config cfg;
    server_load_config(&cfg);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < cfg.subscription_count; i++) {
        const Subscription *s = &cfg.subscriptions[i];
        if (!s->repo[0]) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        cJSON_AddStringToObject(o, "repo", s->repo);
        cJSON_AddStringToObject(o, "alias", s->alias);
        cJSON_AddStringToObject(o, "platform",
                                s->platform[0] ? s->platform : cfg.platform);
        cJSON_AddNumberToObject(o, "days",
                                s->days > 0 ? s->days : cfg.default_days);
        cJSON_AddStringToObject(o, "format",
                                s->format[0] ? s->format : cfg.format);
        cJSON_AddStringToObject(o, "branch", s->branch);
        cJSON_AddBoolToObject(o, "overrides_platform", s->platform[0] != '\0');
        cJSON_AddBoolToObject(o, "overrides_days", s->days > 0);
        cJSON_AddBoolToObject(o, "overrides_format", s->format[0] != '\0');
        cJSON_AddBoolToObject(o, "overrides_branch", s->branch[0] != '\0');
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "subscriptions", arr);
    cJSON_AddNumberToObject(root, "total", cfg.subscription_count);
    cJSON_AddNumberToObject(root, "max", MAX_SUBSCRIPTIONS);
    res_json_obj(res, 200, root);
}

static void handle_list(Request *req, Response *res, void *user) {
    (void)req; (void)user;
    list_response(res);
}

/* Reads the subscription fields shared by POST and PATCH. An absent key means
 * "leave inherited", which the config layer stores as an empty string. */
static int sub_from_json(const cJSON *body, Subscription *out, char *err, size_t errlen) {
    const cJSON *repo = cJSON_GetObjectItem(body, "repo");
    if (repo && cJSON_IsString(repo) && repo->valuestring[0])
        snprintf(out->repo, sizeof(out->repo), "%s", repo->valuestring);

    const cJSON *alias = cJSON_GetObjectItem(body, "alias");
    if (alias && cJSON_IsString(alias)) snprintf(out->alias, sizeof(out->alias), "%s", alias->valuestring);

    const cJSON *plat = cJSON_GetObjectItem(body, "platform");
    if (plat && cJSON_IsString(plat)) snprintf(out->platform, sizeof(out->platform), "%s", plat->valuestring);
    else if (plat && cJSON_IsNull(plat)) out->platform[0] = '\0';

    const cJSON *fmt = cJSON_GetObjectItem(body, "format");
    if (fmt && cJSON_IsString(fmt)) snprintf(out->format, sizeof(out->format), "%s", fmt->valuestring);
    else if (fmt && cJSON_IsNull(fmt)) out->format[0] = '\0';

    const cJSON *branch = cJSON_GetObjectItem(body, "branch");
    if (branch && cJSON_IsString(branch)) snprintf(out->branch, sizeof(out->branch), "%s", branch->valuestring);
    else if (branch && cJSON_IsNull(branch)) out->branch[0] = '\0';

    const cJSON *days = cJSON_GetObjectItem(body, "days");
    if (days && cJSON_IsNumber(days)) {
        int d = (int)days->valuedouble;
        if (d < 0) {
            snprintf(err, errlen, "days must be positive");
            return -1;
        }
        out->days = d;
    } else if (days && cJSON_IsNull(plat)) {
        out->days = 0;
    }

    if (out->format[0] && !format_is_known(out->format)) {
        snprintf(err, errlen, "unsupported format '%s'", out->format);
        return -1;
    }
    return 0;
}

/* POST /api/subscriptions */
static void handle_add(Request *req, Response *res, void *user) {
    (void)user;
    cJSON *body = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!body) { res_error(res, 400, "bad_json", "request body is not valid JSON"); return; }

    Subscription sub;
    memset(&sub, 0, sizeof(sub));
    char err[256] = "";
    if (sub_from_json(body, &sub, err, sizeof(err)) != 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }
    if (!sub.repo[0]) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", "repo is required (owner/repo or a URL)");
        return;
    }
    cJSON_Delete(body);

    server_lock();
    ErrorCode e = config_add_subscription(g_server.config_path, &sub);
    server_unlock();
    if (e != GS_OK) {
        res_error(res, e == ERR_INVALID_ARGS ? 409 : 400,
                  "subscribe_failed",
                  "cannot subscribe; it may already exist or the list may be full");
        return;
    }
    list_response(res);
}

static const Route *route_of(const Request *req) {
    return (const Route *)req->route;
}

/* PATCH /api/subscriptions/:ref */
static void handle_update(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = route_of(req);
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing subscription reference");
        return;
    }
    char ref[256];
    url_decode(rt->params[0], ref, sizeof(ref));

    cJSON *body = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!body) { res_error(res, 400, "bad_json", "request body is not valid JSON"); return; }

    Subscription sub;
    memset(&sub, 0, sizeof(sub));
    char err[256] = "";
    if (sub_from_json(body, &sub, err, sizeof(err)) != 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }
    cJSON_Delete(body);

    server_lock();
    ErrorCode e = config_update_subscription(g_server.config_path, ref, &sub);
    server_unlock();
    if (e != GS_OK) {
        res_error(res, e == ERR_INVALID_ARGS ? 409 : 404, "update_failed",
                  "no subscription matched, or the new values conflict");
        return;
    }
    list_response(res);
}

/* DELETE /api/subscriptions/:ref */
static void handle_remove(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = route_of(req);
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing subscription reference");
        return;
    }
    char ref[256];
    url_decode(rt->params[0], ref, sizeof(ref));

    server_lock();
    ErrorCode e = config_remove_subscription(g_server.config_path, ref);
    server_unlock();
    if (e != GS_OK) {
        res_error(res, 404, "not_found", "no subscription matched");
        return;
    }
    list_response(res);
}

/* POST /api/subscriptions/:ref/analyze -- analyze one subscription now. */
static void handle_analyze_one(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = route_of(req);
    if (!rt || rt->param_count < 1) {
        res_error(res, 400, "bad_request", "missing subscription reference");
        return;
    }
    char ref[256];
    url_decode(rt->params[0], ref, sizeof(ref));

    Config cfg;
    server_load_config(&cfg);
    int idx = config_find_subscription(&cfg, ref);
    if (idx < 0) {
        res_error(res, 404, "not_found", "no subscription matched");
        return;
    }

    Options o;
    options_from_config(&o, &cfg);
    options_apply_subscription(&o, &cfg, &cfg.subscriptions[idx]);

    char title[300];
    snprintf(title, sizeof(title), "watch %s", cfg.subscriptions[idx].repo);

    Job *job = job_create(JOB_KIND_ANALYZE, title);
    if (!job) { res_error(res, 500, "internal", "could not create the job"); return; }
    job_set_options(job, &o);
    job_add_repo(job, cfg.subscriptions[idx].repo, &o);
    if (jobs_submit(job) != 0) {
        res_error(res, 500, "internal", "could not start the job");
        return;
    }
    cJSON *out = job_to_json(job);
    res_json_obj(res, 202, out);
}

/* POST /api/subscriptions/analyze -- analyze every subscription as one batch,
 * with cross-repository parallelism and per-entry overrides. */
static void handle_analyze_all(Request *req, Response *res, void *user) {
    (void)user;
    cJSON *body = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!body) { res_error(res, 400, "bad_json", "request body is not valid JSON"); return; }

    Config cfg;
    server_load_config(&cfg);

    int count = 0;
    for (int i = 0; i < cfg.subscription_count; i++) if (cfg.subscriptions[i].repo[0]) count++;
    if (count == 0) {
        cJSON_Delete(body);
        res_error(res, 400, "no_subscriptions", "no subscriptions to analyze");
        return;
    }

    Options base;
    options_from_config(&base, &cfg);
    char err[256] = "";
    if (options_apply_json(body, &base, err, sizeof(err)) != 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }

    /* Optional per-repo overrides, e.g. switching one repo to a different
     * window or platform for this run only. */
    const cJSON *per_repo = cJSON_GetObjectItem(body, "repos");
    cJSON *entry = NULL;

    char title[128];
    snprintf(title, sizeof(title), "%d subscription%s",
             count, count == 1 ? "" : "s");

    Job *job = job_create(JOB_KIND_SUBSCRIPTIONS, title);
    if (!job) {
        cJSON_Delete(body);
        res_error(res, 500, "internal", "could not create the job");
        return;
    }
    job_set_options(job, &base);

    for (int i = 0; i < cfg.subscription_count; i++) {
        const Subscription *s = &cfg.subscriptions[i];
        if (!s->repo[0]) continue;

        Options o = base;
        options_apply_subscription(&o, &cfg, s);

        if (cJSON_IsArray(per_repo)) {
            cJSON_ArrayForEach(entry, per_repo) {
                char r[256] = "";
                if (json_str(entry, "repo", r, sizeof(r)) && strcmp(r, s->repo) == 0) {
                    options_apply_json(entry, &o, NULL, 0);
                    break;
                }
            }
        }
        job_add_repo(job, s->repo, &o);
    }
    cJSON_Delete(body);

    if (jobs_submit(job) != 0) {
        res_error(res, 500, "internal", "could not start the job");
        return;
    }
    res_json_obj(res, 202, job_to_json(job));
}

void api_register_subscriptions(Router *r) {
    router_add(r, "GET",    "/api/subscriptions", handle_list, NULL);
    router_add(r, "POST",   "/api/subscriptions", handle_add, NULL);
    router_add(r, "POST",   "/api/subscriptions/analyze", handle_analyze_all, NULL);
    router_add(r, "PATCH",  "/api/subscriptions/:ref", handle_update, NULL);
    router_add(r, "DELETE", "/api/subscriptions/:ref", handle_remove, NULL);
    router_add(r, "POST",   "/api/subscriptions/:ref/analyze", handle_analyze_one, NULL);
}