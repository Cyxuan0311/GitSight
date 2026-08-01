#include "gitsight/platform/github.h"
#include "gitsight/models.h"
#include "gitsight/error.h"
#include "gitsight/platform/http.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define GITHUB_ACCEPT "Accept: application/vnd.github.v3+json"

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

static void extract_labels(cJSON *labels_json, char *buf, size_t len) {
    buf[0] = '\0';
    if (!cJSON_IsArray(labels_json)) return;

    int first = 1;
    cJSON *label;
    cJSON_ArrayForEach(label, labels_json) {
        cJSON *name = cJSON_GetObjectItem(label, "name");
        if (name && cJSON_IsString(name)) {
            if (!first) strncat(buf, ", ", len - strlen(buf) - 1);
            strncat(buf, name->valuestring, len - strlen(buf) - 1);
            first = 0;
        }
    }
}

static void set_http_error(GitHubContext *gh, long http_code) {
    if (!gh->error_buf) return;
    if (http_code == 401)
        snprintf(gh->error_buf, gh->error_len,
            "HTTP 401 (bad credentials). Check your GITHUB_TOKEN.");
    else if (http_code == 403)
        snprintf(gh->error_buf, gh->error_len,
            "HTTP 403 (rate limited). Set GITHUB_TOKEN in config or env.");
    else if (http_code == 404)
        snprintf(gh->error_buf, gh->error_len,
            "HTTP 404. Repository '%s/%s' not found.", gh->owner, gh->repo);
    else
        snprintf(gh->error_buf, gh->error_len, "HTTP %ld error.", http_code);
}

typedef struct {
    Vector *vec;
    char since_str[MAX_DATE_LEN];
} PageCtx;

/* ---- Pull Requests ---- */
static int parse_prs_page(void *userdata, const char *body, const HttpResult *res) {
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
            return 1; /* items sorted desc; rest are older too */
        }

        PR *pr = pr_create();
        cJSON *n;
        n = cJSON_GetObjectItem(item, "number");
        if (n && cJSON_IsNumber(n)) pr->number = n->valueint;
        n = cJSON_GetObjectItem(item, "title");
        if (n && cJSON_IsString(n)) snprintf(pr->title, sizeof(pr->title), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "state");
        if (n && cJSON_IsString(n)) snprintf(pr->state, sizeof(pr->state), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "created_at");
        if (n && cJSON_IsString(n)) snprintf(pr->created_at, sizeof(pr->created_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "merged_at");
        if (n && cJSON_IsString(n)) snprintf(pr->merged_at, sizeof(pr->merged_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "html_url");
        if (n && cJSON_IsString(n)) snprintf(pr->html_url, sizeof(pr->html_url), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "user");
        if (n && cJSON_IsObject(n)) {
            cJSON *login = cJSON_GetObjectItem(n, "login");
            if (login && cJSON_IsString(login))
                snprintf(pr->author, sizeof(pr->author), "%s", login->valuestring);
        }
        n = cJSON_GetObjectItem(item, "labels");
        if (n) extract_labels(n, pr->labels, sizeof(pr->labels));

        vector_push(ctx->vec, pr);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_prs_impl(void *ctx, Vector *prs, int since_days) {
    GitHubContext *gh = (GitHubContext *)ctx;

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1024];
    snprintf(url, sizeof(url), "%s/repos/%s/%s/pulls?state=all&sort=updated&direction=desc&per_page=100",
             gh->api_base, gh->owner, gh->repo);

    HttpClient hc;
    char auth[600] = "";
    if (gh->token[0])
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", gh->token);
    http_client_init(&hc, auth, gh->proxy, GITHUB_ACCEPT, gh->timeout_secs, gh->max_pages);

    PageCtx pctx = { prs, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_prs_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_http_error(gh, last_http);
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
        cJSON *commit_obj = cJSON_GetObjectItem(item, "commit");
        cJSON *date = NULL;
        if (commit_obj && cJSON_IsObject(commit_obj)) {
            cJSON *author = cJSON_GetObjectItem(commit_obj, "author");
            if (author && cJSON_IsObject(author))
                date = cJSON_GetObjectItem(author, "date");
        }
        if (!date || !cJSON_IsString(date)) continue;
        if (!date_in_range(date->valuestring, ctx->since_str)) {
            cJSON_Delete(json);
            return 1;
        }

        Commit *c = commit_create();
        cJSON *n;

        n = cJSON_GetObjectItem(item, "sha");
        if (n && cJSON_IsString(n)) {
            snprintf(c->sha, sizeof(c->sha), "%s", n->valuestring);
            if (strlen(c->sha) > 7) c->sha[7] = '\0';
        }
        n = cJSON_GetObjectItem(item, "html_url");
        if (n && cJSON_IsString(n)) snprintf(c->html_url, sizeof(c->html_url), "%s", n->valuestring);

        if (commit_obj && cJSON_IsObject(commit_obj)) {
            cJSON *msg = cJSON_GetObjectItem(commit_obj, "message");
            if (msg && cJSON_IsString(msg)) {
                const char *src = msg->valuestring;
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
            cJSON *author = cJSON_GetObjectItem(commit_obj, "author");
            if (author && cJSON_IsObject(author)) {
                cJSON *name = cJSON_GetObjectItem(author, "name");
                if (name && cJSON_IsString(name))
                    snprintf(c->author, sizeof(c->author), "%s", name->valuestring);
                if (date && cJSON_IsString(date))
                    snprintf(c->date, sizeof(c->date), "%s", date->valuestring);
            }
        }

        vector_push(ctx->vec, c);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_commits_impl(void *ctx, Vector *commits, int since_days) {
    GitHubContext *gh = (GitHubContext *)ctx;

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1152];
    snprintf(url, sizeof(url), "%s/repos/%s/%s/commits?since=%s&per_page=100",
             gh->api_base, gh->owner, gh->repo, since_str);

    HttpClient hc;
    char auth[600] = "";
    if (gh->token[0])
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", gh->token);
    http_client_init(&hc, auth, gh->proxy, GITHUB_ACCEPT, gh->timeout_secs, gh->max_pages);

    PageCtx pctx = { commits, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_commits_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_http_error(gh, last_http);
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
        n = cJSON_GetObjectItem(item, "number");
        if (n && cJSON_IsNumber(n)) iss->number = n->valueint;
        n = cJSON_GetObjectItem(item, "title");
        if (n && cJSON_IsString(n)) snprintf(iss->title, sizeof(iss->title), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "state");
        if (n && cJSON_IsString(n)) snprintf(iss->state, sizeof(iss->state), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "created_at");
        if (n && cJSON_IsString(n)) snprintf(iss->created_at, sizeof(iss->created_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "closed_at");
        if (n && cJSON_IsString(n)) snprintf(iss->closed_at, sizeof(iss->closed_at), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "html_url");
        if (n && cJSON_IsString(n)) snprintf(iss->html_url, sizeof(iss->html_url), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "user");
        if (n && cJSON_IsObject(n)) {
            cJSON *login = cJSON_GetObjectItem(n, "login");
            if (login && cJSON_IsString(login))
                snprintf(iss->author, sizeof(iss->author), "%s", login->valuestring);
        }
        n = cJSON_GetObjectItem(item, "labels");
        if (n) extract_labels(n, iss->labels, sizeof(iss->labels));

        vector_push(ctx->vec, iss);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_issues_impl(void *ctx, Vector *issues, int since_days) {
    GitHubContext *gh = (GitHubContext *)ctx;

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1024];
    snprintf(url, sizeof(url), "%s/repos/%s/%s/issues?state=all&sort=updated&direction=desc&per_page=100",
             gh->api_base, gh->owner, gh->repo);

    HttpClient hc;
    char auth[600] = "";
    if (gh->token[0])
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", gh->token);
    http_client_init(&hc, auth, gh->proxy, GITHUB_ACCEPT, gh->timeout_secs, gh->max_pages);

    PageCtx pctx = { issues, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_issues_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_http_error(gh, last_http);
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
        cJSON *draft = cJSON_GetObjectItem(item, "draft");
        if (draft && cJSON_IsTrue(draft)) continue;

        cJSON *published = cJSON_GetObjectItem(item, "published_at");
        if (!published || !cJSON_IsString(published)) continue;
        if (!date_in_range(published->valuestring, ctx->since_str)) {
            cJSON_Delete(json);
            return 1;
        }

        Release *rel = release_create();
        cJSON *n;
        n = cJSON_GetObjectItem(item, "tag_name");
        if (n && cJSON_IsString(n)) snprintf(rel->tag_name, sizeof(rel->tag_name), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "name");
        if (n && cJSON_IsString(n)) snprintf(rel->name, sizeof(rel->name), "%s", n->valuestring);
        n = cJSON_GetObjectItem(item, "html_url");
        if (n && cJSON_IsString(n)) snprintf(rel->html_url, sizeof(rel->html_url), "%s", n->valuestring);
        snprintf(rel->published_at, sizeof(rel->published_at), "%s", published->valuestring);
        n = cJSON_GetObjectItem(item, "author");
        if (n && cJSON_IsObject(n)) {
            cJSON *login = cJSON_GetObjectItem(n, "login");
            if (login && cJSON_IsString(login))
                snprintf(rel->author, sizeof(rel->author), "%s", login->valuestring);
        }
        n = cJSON_GetObjectItem(item, "body");
        if (n && cJSON_IsString(n)) snprintf(rel->body, sizeof(rel->body), "%s", n->valuestring);

        vector_push(ctx->vec, rel);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_releases_impl(void *ctx, Vector *releases, int since_days) {
    GitHubContext *gh = (GitHubContext *)ctx;

    char since_str[MAX_DATE_LEN];
    build_since_date(since_days, since_str, sizeof(since_str));

    char url[1024];
    snprintf(url, sizeof(url), "%s/repos/%s/%s/releases?per_page=100",
             gh->api_base, gh->owner, gh->repo);

    HttpClient hc;
    char auth[600] = "";
    if (gh->token[0])
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", gh->token);
    http_client_init(&hc, auth, gh->proxy, GITHUB_ACCEPT, gh->timeout_secs, gh->max_pages);

    PageCtx pctx = { releases, "" };
    snprintf(pctx.since_str, sizeof(pctx.since_str), "%s", since_str);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_releases_page, &pctx, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS && last_http == 404) return GS_OK; /* no releases */
        if (err == ERR_HTTP_STATUS) set_http_error(gh, last_http);
        return err;
    }
    return GS_OK;
}

static ErrorCode fetch_meta_impl(void *ctx, RepoMeta *meta) {
    GitHubContext *gh = (GitHubContext *)ctx;

    char url[1024];
    snprintf(url, sizeof(url), "%s/repos/%s/%s", gh->api_base, gh->owner, gh->repo);

    HttpClient hc;
    char auth[600] = "";
    if (gh->token[0])
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", gh->token);
    http_client_init(&hc, auth, gh->proxy, GITHUB_ACCEPT, gh->timeout_secs, gh->max_pages);

    HttpBuffer buf;
    HttpResult res;
    http_buffer_init(&buf);
    ErrorCode err = http_get(&hc, url, &buf, &res);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_http_error(gh, res.http_code);
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
    n = cJSON_GetObjectItem(json, "language");
    if (n && cJSON_IsString(n)) snprintf(meta->language, sizeof(meta->language), "%s", n->valuestring);
    n = cJSON_GetObjectItem(json, "default_branch");
    if (n && cJSON_IsString(n)) snprintf(meta->default_branch, sizeof(meta->default_branch), "%s", n->valuestring);
    n = cJSON_GetObjectItem(json, "stargazers_count");
    if (n && cJSON_IsNumber(n)) meta->stars = n->valuedouble;
    n = cJSON_GetObjectItem(json, "forks_count");
    if (n && cJSON_IsNumber(n)) meta->forks = n->valuedouble;
    n = cJSON_GetObjectItem(json, "watchers_count");
    if (n && cJSON_IsNumber(n)) meta->watchers = n->valuedouble;
    n = cJSON_GetObjectItem(json, "open_issues_count");
    if (n && cJSON_IsNumber(n)) meta->open_issues = n->valuedouble;
    n = cJSON_GetObjectItem(json, "created_at");
    if (n && cJSON_IsString(n)) snprintf(meta->created_at, sizeof(meta->created_at), "%s", n->valuestring);
    n = cJSON_GetObjectItem(json, "pushed_at");
    if (n && cJSON_IsString(n)) snprintf(meta->pushed_at, sizeof(meta->pushed_at), "%s", n->valuestring);
    n = cJSON_GetObjectItem(json, "license");
    if (n && cJSON_IsObject(n)) {
        cJSON *spdx = cJSON_GetObjectItem(n, "spdx_id");
        if (spdx && cJSON_IsString(spdx))
            snprintf(meta->license, sizeof(meta->license), "%s", spdx->valuestring);
    }

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
        cJSON *login = cJSON_GetObjectItem(item, "login");
        if (!login || !cJSON_IsString(login) || !login->valuestring[0]) continue;

        Contributor *ctb = contributor_create();
        snprintf(ctb->login, sizeof(ctb->login), "%s", login->valuestring);
        cJSON *contrib = cJSON_GetObjectItem(item, "contributions");
        if (contrib && cJSON_IsNumber(contrib)) ctb->contributions = contrib->valueint;
        vector_push(vec, ctb);
    }

    cJSON_Delete(json);
    return 0;
}

static ErrorCode fetch_contributors_impl(void *ctx, Vector *contributors) {
    GitHubContext *gh = (GitHubContext *)ctx;

    char url[1024];
    snprintf(url, sizeof(url), "%s/repos/%s/%s/contributors?per_page=100&anon=1",
             gh->api_base, gh->owner, gh->repo);

    HttpClient hc;
    char auth[600] = "";
    if (gh->token[0])
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", gh->token);
    http_client_init(&hc, auth, gh->proxy, GITHUB_ACCEPT, gh->timeout_secs, gh->max_pages);

    long last_http = 0;
    ErrorCode err = http_get_paginated(&hc, url, parse_contributors_page, contributors, &last_http);
    if (err != GS_OK) {
        if (err == ERR_HTTP_STATUS) set_http_error(gh, last_http);
        return err;
    }
    return GS_OK;
}

static void github_destroy(void *ctx) {
    free(ctx);
}

ErrorCode github_client_init(PlatformClient *client, const Config *cfg,
                             const char *owner, const char *repo)
{
    GitHubContext *ctx = calloc(1, sizeof(GitHubContext));
    if (!ctx) return ERR_MEMORY;

    snprintf(ctx->token, sizeof(ctx->token), "%s", cfg->github_token);
    snprintf(ctx->api_base, sizeof(ctx->api_base), "%s", cfg->api_base);
    snprintf(ctx->proxy, sizeof(ctx->proxy), "%s", cfg->proxy);
    snprintf(ctx->owner, sizeof(ctx->owner), "%s", owner);
    snprintf(ctx->repo, sizeof(ctx->repo), "%s", repo);
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
    client->fetch_prs = fetch_prs_impl;
    client->fetch_commits = fetch_commits_impl;
    client->fetch_issues = fetch_issues_impl;
    client->fetch_releases = fetch_releases_impl;
    client->fetch_meta = fetch_meta_impl;
    client->fetch_contributors = fetch_contributors_impl;
    client->destroy = github_destroy;

    return GS_OK;
}
