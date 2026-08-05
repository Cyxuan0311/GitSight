#include "gitsight/platform/gitlab.h"
#include "gitsight/models.h"
#include "gitsight/error.h"
#include "gitsight/platform/http.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define GITLAB_ACCEPT "Accept: application/json"

static void build_since_date(int since_days, char *buf, size_t len) {
    time_t now = time(NULL);
    time_t since_ts = now - (time_t)since_days * 86400;
    struct tm *tm = gmtime(&since_ts);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", tm);
}

static int date_in_range(const char *date_str, const char *since_str) {
    if (!date_str || date_str[0] == '\0') return 0;
    return strcmp(date_str, since_str) >= 0;
}

static void encode_project_path(const char *owner, const char *repo,
                                char *out, size_t len)
{
    snprintf(out, len, "%s%%2F%s", owner, repo);
}

static void extract_labels(cJSON *labels_json, char *buf, size_t len) {
    buf[0] = '\0';
    if (!cJSON_IsArray(labels_json)) return;

    int first = 1;
    cJSON *label;
    cJSON_ArrayForEach(label, labels_json) {
        const char *name = NULL;
        if (cJSON_IsString(label)) {
            name = label->valuestring;
        } else {
            cJSON *n = cJSON_GetObjectItem(label, "title");
            if (n && cJSON_IsString(n)) name = n->valuestring;
        }
        if (name) {
            if (!first) strncat(buf, ", ", len - strlen(buf) - 1);
            strncat(buf, name, len - strlen(buf) - 1);
            first = 0;
        }
    }
}

static void set_error(GitLabContext *ctx, long http_code) {
    if (!ctx->error_buf) return;
    if (http_code == 401)
        snprintf(ctx->error_buf, ctx->error_len,
            "HTTP 401 (bad credentials). Check your GITLAB_TOKEN.");
    else if (http_code == 403)
        snprintf(ctx->error_buf, ctx->error_len,
            "HTTP 403 (rate limited). Set GITLAB_TOKEN in config or env.");
    else if (http_code == 404)
        snprintf(ctx->error_buf, ctx->error_len,
            "HTTP 404. Project '%s/%s' not found.", ctx->owner, ctx->repo);
    else
        snprintf(ctx->error_buf, ctx->error_len, "HTTP %ld error.", http_code);
}

typedef struct {
    Vector *vec;
    char since_str[MAX_DATE_LEN];
} PageCtx;

/* ---- Merge Requests ---- */
static int parse_mrs_page(void *userdata, const char *body, const HttpResult *res) {
    (void)res;
    PageCtx *ctx = (PageCtx *)userdata;

    cJSON *json = cJSON_Parse(body);
    if (!json) return 1;
    if (!cJSON_IsArray(json)) { cJSON_Delete(json); return 1; }

    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        cJSON *updated = cJSON_GetObjectItem(item, "updated_at");
        if (!updated || !cJSON_IsString(updated)) continue;
        if (!date_in_range(updated->valuestring, ctx->since_str)) {
            cJSON_Delete(json);
            return 1;
        }

        PR *pr = pr_create();
        cJSON *n;
        n = cJSON_GetObjectItem(item, "iid");
        if (n && cJSON_IsNumber(n)) pr->number = n->valueint;
        n = cJSON_GetObjectItem(item, "title");
        if (n && cJSON_IsString(n)) snprintf(pr->title, sizeof(pr->title), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "state");
        if (n && cJSON_IsString(n)) {
            if (strcmp(n->valuestring, "merged") == 0)
                snprintf(pr->state, sizeof(pr->state), "closed");
            else
                snprintf(pr->state, sizeof(pr->state), "%s", n->valuestring);
        }
        n = cJSON_GetObjectItem(item, "created_at");
        if (n && cJSON_IsString(n)) snprintf(pr->created_at, sizeof(pr->created_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "merged_at");
        if (n && cJSON_IsString(n)) snprintf(pr->merged_at, sizeof(pr->merged_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "web_url");
        if (n && cJSON_IsString(n)) snprintf(pr->html_url, sizeof(pr->html_url), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "author");
        if (n && cJSON_IsObject(n)) {
            cJSON *name = cJSON_GetObjectItem(n, "name");
            if (name && cJSON_IsString(name))
                snprintf(pr->author, sizeof(pr->author), "%s", name->valuestring);
        }
        n = cJSON_GetObjectItem(item, "labels");
        if (n) extract_labels(n, pr->labels, sizeof(pr->labels));

        vector_push(ctx->vec, pr);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_mrs_impl(void *ctx, Vector *prs, int since_days) {
    GitLabContext *gl = (GitLabContext *)ctx;
    char project[512];
    encode_project_path(gl->owner, gl->repo, project, sizeof(project));

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1152];
    snprintf(url, sizeof(url), "%s/projects/%s/merge_requests?state=all&order_by=updated_at&sort=desc&per_page=100",
             gl->api_base, project);

    HttpClient hc;
    char auth[600] = "";
    if (gl->token[0])
        snprintf(auth, sizeof(auth), "PRIVATE-TOKEN: %s", gl->token);
    http_client_init(&hc, auth, gl->proxy, GITLAB_ACCEPT, gl->timeout_secs, gl->max_pages);

    PageCtx pctx = { prs, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_mrs_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_error(gl, last_http);
        return err;
    }
    return GS_OK;
}

/* ---- Commits ---- */
static int parse_commits_page(void *userdata, const char *body, const HttpResult *res) {
    (void)res;
    PageCtx *ctx = (PageCtx *)userdata;

    cJSON *json = cJSON_Parse(body);
    if (!json) return 1;
    if (!cJSON_IsArray(json)) { cJSON_Delete(json); return 1; }

    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        cJSON *date = cJSON_GetObjectItem(item, "committed_date");
        if (!date || !cJSON_IsString(date)) continue;
        if (!date_in_range(date->valuestring, ctx->since_str)) {
            cJSON_Delete(json);
            return 1;
        }

        Commit *c = commit_create();
        cJSON *n;

        n = cJSON_GetObjectItem(item, "id");
        if (n && cJSON_IsString(n)) {
            snprintf(c->sha, sizeof(c->sha), "%s", n->valuestring);
            if (strlen(c->sha) > 7) c->sha[7] = '\0';
        }
        n = cJSON_GetObjectItem(item, "web_url");
        if (n && cJSON_IsString(n)) snprintf(c->html_url, sizeof(c->html_url), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "message");
        if (n && cJSON_IsString(n)) {
            const char *src = n->valuestring;
            char *dst = c->message;
            int max = sizeof(c->message) - 1;
            int i = 0;
            while (*src && i < max) {
                if (*src == '\n') {
                    *dst++ = ' ';
                    src++;
                    while (*src == '\n') src++;
                } else {
                    *dst++ = *src++;
                }
                i++;
            }
            *dst = '\0';
        }
        n = cJSON_GetObjectItem(item, "committer_name");
        if (n && cJSON_IsString(n))
            snprintf(c->author, sizeof(c->author), "%s", n->valuestring);
        if (date && cJSON_IsString(date))
            snprintf(c->date, sizeof(c->date), "%s", date->valuestring);

        vector_push(ctx->vec, c);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_commits_impl(void *ctx, Vector *commits, int since_days) {
    GitLabContext *gl = (GitLabContext *)ctx;
    char project[512];
    encode_project_path(gl->owner, gl->repo, project, sizeof(project));

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1280];
    snprintf(url, sizeof(url), "%s/projects/%s/repository/commits?since=%s&per_page=100",
             gl->api_base, project, since_str);
    if (gl->branch[0]) {
        char enc[128];
        url_encode(gl->branch, enc, sizeof(enc));
        snprintf(url + strlen(url), sizeof(url) - strlen(url), "&ref_name=%s", enc);
    }

    HttpClient hc;
    char auth[600] = "";
    if (gl->token[0])
        snprintf(auth, sizeof(auth), "PRIVATE-TOKEN: %s", gl->token);
    http_client_init(&hc, auth, gl->proxy, GITLAB_ACCEPT, gl->timeout_secs, gl->max_pages);

    PageCtx pctx = { commits, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_commits_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_error(gl, last_http);
        return err;
    }
    return GS_OK;
}

/* ---- Issues ---- */
static int parse_issues_page(void *userdata, const char *body, const HttpResult *res) {
    (void)res;
    PageCtx *ctx = (PageCtx *)userdata;

    cJSON *json = cJSON_Parse(body);
    if (!json) return 1;
    if (!cJSON_IsArray(json)) { cJSON_Delete(json); return 1; }

    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        cJSON *pull_request = cJSON_GetObjectItem(item, "pull_request");
        if (pull_request) continue;

        cJSON *updated = cJSON_GetObjectItem(item, "updated_at");
        if (!updated || !cJSON_IsString(updated)) continue;
        if (!date_in_range(updated->valuestring, ctx->since_str)) {
            cJSON_Delete(json);
            return 1;
        }

        Issue *iss = issue_create();
        cJSON *n;
        n = cJSON_GetObjectItem(item, "iid");
        if (n && cJSON_IsNumber(n)) iss->number = n->valueint;
        n = cJSON_GetObjectItem(item, "title");
        if (n && cJSON_IsString(n)) snprintf(iss->title, sizeof(iss->title), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "state");
        if (n && cJSON_IsString(n)) snprintf(iss->state, sizeof(iss->state), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "created_at");
        if (n && cJSON_IsString(n)) snprintf(iss->created_at, sizeof(iss->created_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "closed_at");
        if (n && cJSON_IsString(n)) snprintf(iss->closed_at, sizeof(iss->closed_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "web_url");
        if (n && cJSON_IsString(n)) snprintf(iss->html_url, sizeof(iss->html_url), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "author");
        if (n && cJSON_IsObject(n)) {
            cJSON *name = cJSON_GetObjectItem(n, "name");
            if (name && cJSON_IsString(name))
                snprintf(iss->author, sizeof(iss->author), "%s", name->valuestring);
        }
        n = cJSON_GetObjectItem(item, "labels");
        if (n) extract_labels(n, iss->labels, sizeof(iss->labels));

        vector_push(ctx->vec, iss);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_issues_impl(void *ctx, Vector *issues, int since_days) {
    GitLabContext *gl = (GitLabContext *)ctx;
    char project[512];
    encode_project_path(gl->owner, gl->repo, project, sizeof(project));

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1152];
    snprintf(url, sizeof(url), "%s/projects/%s/issues?state=all&order_by=updated_at&sort=desc&per_page=100",
             gl->api_base, project);

    HttpClient hc;
    char auth[600] = "";
    if (gl->token[0])
        snprintf(auth, sizeof(auth), "PRIVATE-TOKEN: %s", gl->token);
    http_client_init(&hc, auth, gl->proxy, GITLAB_ACCEPT, gl->timeout_secs, gl->max_pages);

    PageCtx pctx = { issues, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_issues_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_error(gl, last_http);
        return err;
    }
    return GS_OK;
}

/* ---- Releases ---- */
static int parse_releases_page(void *userdata, const char *body, const HttpResult *res) {
    (void)res;
    PageCtx *ctx = (PageCtx *)userdata;

    cJSON *json = cJSON_Parse(body);
    if (!json) return 1;
    if (!cJSON_IsArray(json)) { cJSON_Delete(json); return 1; }

    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        cJSON *released = cJSON_GetObjectItem(item, "released_at");
        if (!released || !cJSON_IsString(released)) continue;
        if (!date_in_range(released->valuestring, ctx->since_str)) {
            cJSON_Delete(json);
            return 1;
        }

        Release *rel = release_create();
        cJSON *n;
        n = cJSON_GetObjectItem(item, "tag_name");
        if (n && cJSON_IsString(n)) snprintf(rel->tag_name, sizeof(rel->tag_name), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "name");
        if (n && cJSON_IsString(n)) snprintf(rel->name, sizeof(rel->name), "%s", n->valuestring);
        snprintf(rel->published_at, sizeof(rel->published_at), "%s", released->valuestring);
        n = cJSON_GetObjectItem(item, "_links");
        if (n && cJSON_IsObject(n)) {
            cJSON *self = cJSON_GetObjectItem(n, "self");
            if (self && cJSON_IsString(self))
                snprintf(rel->html_url, sizeof(rel->html_url), "%s", self->valuestring);
        }
        n = cJSON_GetObjectItem(item, "author");
        if (n && cJSON_IsObject(n)) {
            cJSON *name = cJSON_GetObjectItem(n, "name");
            if (name && cJSON_IsString(name))
                snprintf(rel->author, sizeof(rel->author), "%s", name->valuestring);
        }
        n = cJSON_GetObjectItem(item, "description");
        if (n && cJSON_IsString(n)) snprintf(rel->body, sizeof(rel->body), "%s", n->valuestring);

        vector_push(ctx->vec, rel);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_releases_impl(void *ctx, Vector *releases, int since_days) {
    GitLabContext *gl = (GitLabContext *)ctx;
    char project[512];
    encode_project_path(gl->owner, gl->repo, project, sizeof(project));

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1152];
    snprintf(url, sizeof(url), "%s/projects/%s/releases?per_page=100",
             gl->api_base, project);

    HttpClient hc;
    char auth[600] = "";
    if (gl->token[0])
        snprintf(auth, sizeof(auth), "PRIVATE-TOKEN: %s", gl->token);
    http_client_init(&hc, auth, gl->proxy, GITLAB_ACCEPT, gl->timeout_secs, gl->max_pages);

    PageCtx pctx = { releases, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_releases_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS && last_http == 404) return GS_OK; /* no releases */
        if (err == ERR_HTTP_STATUS) set_error(gl, last_http);
        return err;
    }
    return GS_OK;
}

static ErrorCode fetch_meta_impl(void *ctx, RepoMeta *meta) {
    GitLabContext *gl = (GitLabContext *)ctx;
    char project[512];
    encode_project_path(gl->owner, gl->repo, project, sizeof(project));

    char url[1152];
    snprintf(url, sizeof(url), "%s/projects/%s", gl->api_base, project);

    HttpClient hc;
    char auth[600] = "";
    if (gl->token[0])
        snprintf(auth, sizeof(auth), "PRIVATE-TOKEN: %s", gl->token);
    http_client_init(&hc, auth, gl->proxy, GITLAB_ACCEPT, gl->timeout_secs, gl->max_pages);

    HttpBuffer buf;
    HttpResult res;
    http_buffer_init(&buf);
    ErrorCode err = http_get(&hc, url, &buf, &res);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_error(gl, res.http_code);
        http_buffer_free(&buf);
        return err;
    }

    cJSON *json = cJSON_Parse(buf.data);
    http_buffer_free(&buf);
    if (!json) return ERR_JSON_PARSE;

    memset(meta, 0, sizeof(*meta));
    cJSON *n;
    n = cJSON_GetObjectItem(json, "description");
    if (n && cJSON_IsString(n)) snprintf(meta->description, sizeof(meta->description), "%s", n->valuestring);
    n = cJSON_GetObjectItem(json, "default_branch");
    if (n && cJSON_IsString(n)) snprintf(meta->default_branch, sizeof(meta->default_branch), "%s", n->valuestring);
    n = cJSON_GetObjectItem(json, "star_count");
    if (n && cJSON_IsNumber(n)) meta->stars = n->valuedouble;
    n = cJSON_GetObjectItem(json, "forks_count");
    if (n && cJSON_IsNumber(n)) meta->forks = n->valuedouble;
    n = cJSON_GetObjectItem(json, "open_issues_count");
    if (n && cJSON_IsNumber(n)) meta->open_issues = n->valuedouble;
    n = cJSON_GetObjectItem(json, "created_at");
    if (n && cJSON_IsString(n)) snprintf(meta->created_at, sizeof(meta->created_at), "%s", n->valuestring);
    n = cJSON_GetObjectItem(json, "last_activity_at");
    if (n && cJSON_IsString(n)) snprintf(meta->pushed_at, sizeof(meta->pushed_at), "%s", n->valuestring);

    meta->has_meta = 1;
    cJSON_Delete(json);
    return GS_OK;
}

/* ---- Contributors ---- */
static int parse_contributors_page(void *userdata, const char *body, const HttpResult *res) {
    (void)res;
    Vector *vec = (Vector *)userdata;

    cJSON *json = cJSON_Parse(body);
    if (!json) return 1;
    if (!cJSON_IsArray(json)) { cJSON_Delete(json); return 1; }

    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        cJSON *name = cJSON_GetObjectItem(item, "name");
        if (!name || !cJSON_IsString(name) || !name->valuestring[0]) continue;

        Contributor *ctb = contributor_create();
        snprintf(ctb->login, sizeof(ctb->login), "%s", name->valuestring);
        cJSON *commits = cJSON_GetObjectItem(item, "commits");
        if (commits && cJSON_IsNumber(commits)) ctb->contributions = commits->valueint;
        vector_push(vec, ctb);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_contributors_impl(void *ctx, Vector *contributors) {
    GitLabContext *gl = (GitLabContext *)ctx;
    char project[512];
    encode_project_path(gl->owner, gl->repo, project, sizeof(project));

    char url[1152];
    snprintf(url, sizeof(url), "%s/projects/%s/repository/contributors", gl->api_base, project);

    HttpClient hc;
    char auth[600] = "";
    if (gl->token[0])
        snprintf(auth, sizeof(auth), "PRIVATE-TOKEN: %s", gl->token);
    http_client_init(&hc, auth, gl->proxy, GITLAB_ACCEPT, gl->timeout_secs, gl->max_pages);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_contributors_page, contributors, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_error(gl, last_http);
        return err;
    }
    return GS_OK;
}

static void gitlab_destroy(void *ctx) {
    free(ctx);
}

ErrorCode gitlab_client_init(PlatformClient *client, const Config *cfg,
                             const char *owner, const char *repo)
{
    GitLabContext *ctx = calloc(1, sizeof(GitLabContext));
    if (!ctx) return ERR_MEMORY;

    snprintf(ctx->token, sizeof(ctx->token), "%s", cfg->gitlab_token);
    snprintf(ctx->api_base, sizeof(ctx->api_base), "%s", cfg->api_base);
    snprintf(ctx->proxy, sizeof(ctx->proxy), "%s", cfg->proxy);
    snprintf(ctx->owner, sizeof(ctx->owner), "%s", owner);
    snprintf(ctx->repo, sizeof(ctx->repo), "%s", repo);
    snprintf(ctx->branch, sizeof(ctx->branch), "%s", cfg->branch);
    ctx->timeout_secs = 30;
    ctx->max_pages = cfg->max_pages;
    ctx->error_buf = client->last_error;
    ctx->error_len = sizeof(client->last_error);
    client->last_error[0] = '\0';

    snprintf(client->owner, sizeof(client->owner), "%s", owner);
    snprintf(client->repo, sizeof(client->repo), "%s", repo);
    snprintf(client->api_base, sizeof(client->api_base), "%s", cfg->api_base);
    snprintf(client->proxy, sizeof(client->proxy), "%s", cfg->proxy);
    client->timeout_secs = 30;
    client->ctx = ctx;
    client->fetch_prs = fetch_mrs_impl;
    client->fetch_commits = fetch_commits_impl;
    client->fetch_issues = fetch_issues_impl;
    client->fetch_releases = fetch_releases_impl;
    client->fetch_meta = fetch_meta_impl;
    client->fetch_contributors = fetch_contributors_impl;
    client->destroy = gitlab_destroy;

    return GS_OK;
}
