#define _GNU_SOURCE

#include "api_util.h"
#include "jobs.h"

#include "gitsight/anim.h"
#include "gitsight/i18n.h"

#include <cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* Job submission and retrieval. Analysis runs asynchronously because a single
 * repository with an LLM summary can take minutes; the browser opens an SSE
 * stream on the returned id and re-renders progress as it arrives. */

/* Pulls the repository list out of a request body. Accepts "repo" (single),
 * "repos" (array), or a newline-separated "repos_text" for paste-heavy use. */
static int collect_repos(const cJSON *body, char list[][256], int max, char *err, size_t errlen) {
    int count = 0;
    const cJSON *repos = cJSON_GetObjectItem(body, "repos");
    cJSON *item = NULL;
    if (cJSON_IsArray(repos)) {
        cJSON_ArrayForEach(item, repos) {
            if (count >= max) break;
            if (cJSON_IsString(item) && item->valuestring && item->valuestring[0])
                snprintf(list[count++], 256, "%s", item->valuestring);
        }
    }
    char single[256] = "";
    if (count == 0 && json_str(body, "repo", single, sizeof(single)) && single[0])
        snprintf(list[count++], 256, "%s", single);

    if (count == 0) {
        char text[4096] = "";
        if (json_str(body, "repos_text", text, sizeof(text)) && text[0]) {
            char *save = NULL;
            for (char *tok = strtok_r(text, "\r\n, \t", &save);
                 tok && count < max; tok = strtok_r(NULL, "\r\n, \t", &save)) {
                if (!*tok) continue;
                snprintf(list[count++], 256, "%s", tok);
            }
        }
    }

    if (count == 0) {
        snprintf(err, errlen, "provide repo or repos");
        return 0;
    }

    /* Reject what the CLI would refuse before a job is ever created: otherwise
     * the browser gets a green "done" job holding an empty report instead of a
     * 400 naming the offending argument. */
    for (int i = 0; i < count; i++) {
        char owner[128], name[128], platform[16];
        if (repo_spec_resolve(list[i], owner, sizeof(owner), name, sizeof(name),
                              platform, sizeof(platform)) != 0) {
            snprintf(err, errlen,
                     "invalid repository '%s'; use owner/repo or a full URL",
                     list[i]);
            return 0;
        }
    }
    return count;
}

/* POST /api/jobs/analyze -- one or more repositories, run in parallel. */
static void handle_analyze(Request *req, Response *res, void *user) {
    (void)user;
    cJSON *body = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!body) { res_error(res, 400, "bad_json", "request body is not valid JSON"); return; }

    char repos[16][256];
    char err[512] = "";
    int count = collect_repos(body, repos, 16, err, sizeof(err));
    if (count == 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }

    Config cfg;
    server_load_config(&cfg);
    Options o;
    options_from_config(&o, &cfg);
    if (options_apply_json(body, &o, err, sizeof(err)) != 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }

    char title[300];
    if (count == 1) snprintf(title, sizeof(title), "%s", repos[0]);
    else snprintf(title, sizeof(title), "%d repositories", count);

    Job *job = job_create(JOB_KIND_ANALYZE, title);
    if (!job) { cJSON_Delete(body); res_error(res, 500, "internal", "could not create the job"); return; }
    job_set_options(job, &o);
    for (int i = 0; i < count; i++) job_add_repo(job, repos[i], NULL);
    cJSON_Delete(body);

    if (jobs_submit(job) != 0) {
        res_error(res, 500, "internal", "could not start the job");
        return;
    }
    res_json_obj(res, 202, job_to_json(job));
}

/* POST /api/jobs/compare */
static void handle_compare(Request *req, Response *res, void *user) {
    (void)user;
    cJSON *body = cJSON_Parse(req->body && req->body_len ? req->body : "{}");
    if (!body) { res_error(res, 400, "bad_json", "request body is not valid JSON"); return; }

    char repo[256] = "";
    if (!json_str(body, "repo", repo, sizeof(repo)) || !repo[0]) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", "repo is required");
        return;
    }
    {
        char owner[128], name[128], platform[16];
        if (repo_spec_resolve(repo, owner, sizeof(owner), name, sizeof(name),
                              platform, sizeof(platform)) != 0) {
            cJSON_Delete(body);
            res_error(res, 400, "bad_request",
                      "invalid repository; use owner/repo or a full URL");
            return;
        }
    }
    int a = 7, b = 30;
    json_int(body, "days_a", &a);
    json_int(body, "days_b", &b);
    if (a <= 0 || b <= 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", "days_a and days_b must be positive");
        return;
    }

    Config cfg;
    server_load_config(&cfg);
    Options o;
    options_from_config(&o, &cfg);
    char err[512] = "";
    if (options_apply_json(body, &o, err, sizeof(err)) != 0) {
        cJSON_Delete(body);
        res_error(res, 400, "bad_request", err);
        return;
    }

    char title[300];
    snprintf(title, sizeof(title), "%s %d vs %d", repo, a, b);

    Job *job = job_create(JOB_KIND_COMPARE, title);
    if (!job) { cJSON_Delete(body); res_error(res, 500, "internal", "could not create the job"); return; }
    job_set_options(job, &o);
    job_add_repo(job, repo, NULL);
    job_set_compare(job, a, b);
    cJSON_Delete(body);

    if (jobs_submit(job) != 0) {
        res_error(res, 500, "internal", "could not start the job");
        return;
    }
    res_json_obj(res, 202, job_to_json(job));
}

/* GET /api/jobs?limit=20 */
static void handle_list(Request *req, Response *res, void *user) {
    (void)user;
    char buf[32];
    int limit = 20;
    if (req_query(req, "limit", buf, sizeof(buf)) && buf[0]) {
        limit = atoi(buf);
        if (limit < 1) limit = 1;
        if (limit > GS_JOBS_MAX) limit = GS_JOBS_MAX;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "jobs", job_list_json(limit));
    cJSON_AddNumberToObject(root, "active", jobs_active_count());
    res_json_obj(res, 200, root);
}

/* GET /api/jobs/:id */
static void handle_get(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    char id[64];
    url_decode(rt->params[0], id, sizeof(id));

    Job *job = job_find(id);
    if (!job) {
        res_error(res, 404, "not_found", "no such job (it may have been evicted)");
        return;
    }
    res_json_obj(res, 200, job_to_json(job));
}

/* ?lang= picks the language this one response is rendered in, overriding the
 * snapshot the job captured when it ran. The exporters read the active
 * language from a thread-local rather than from Config, so this has to set both
 * or the override would silently do nothing. */
static void apply_lang_override(Request *req, Config *cfg) {
    char lang[16] = "";
    if (!req_query(req, "lang", lang, sizeof(lang)) || !lang[0]) return;
    snprintf(cfg->lang, sizeof(cfg->lang), "%s", lang);
    gs_set_lang_str(cfg->lang);
}

/* GET /api/jobs/:id/result -- rendered from the stored analysis, so this never
 * refetches. */
static void handle_result(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    char id[64];
    url_decode(rt->params[0], id, sizeof(id));

    Job *job = job_find(id);
    if (!job) {
        res_error(res, 404, "not_found", "no such job");
        return;
    }
    if (job->state == JOB_QUEUED || job->state == JOB_RUNNING) {
        res_error(res, 409, "not_ready", "the job is still running");
        return;
    }
    if (job->state == JOB_CANCELLED) {
        res_error(res, 499, "cancelled", job->error_text);
        return;
    }
    if (job->state == JOB_FAILED) {
        res_error(res, job->error == GS_OK ? 500 : http_status_for_error(job->error),
                  job->error_text[0] ? job->error_text : "analysis_failed",
                  "the analysis did not complete");
        return;
    }

    /* An optional ?lang=... overrides the snapshot for the response only. */
    Config cfg = job->export_cfg;
    apply_lang_override(req, &cfg);

    char *text = job_result_text(job, &cfg);
    if (!text) {
        res_error(res, 500, "internal", "no result available");
        return;
    }
    res_json(res, 200, text);
    free(text);
}

/* Locates the report a download refers to.
 *
 * The job stores each repository exactly as it was submitted, but a browser
 * identifies a report by the owner/repo pair the report itself renders. Those
 * two spellings diverge whenever the input was a forge URL, an SSH address, or
 * carried a ".git" suffix, so both are accepted: a strict comparison against the
 * raw string made every download of a URL-submitted repository fail with
 * "that repository is not part of this job". */
static int report_index_for(const Job *job, const char *which) {
    if (!job || !which || !job->report_repo) return -1;
    for (int i = 0; i < job->report_count; i++) {
        if (strcmp(job->report_repo[i], which) == 0) return i;

        char owner[128], name[128], platform[16];
        if (repo_spec_resolve(job->report_repo[i], owner, sizeof(owner),
                              name, sizeof(name), platform, sizeof(platform)) != 0)
            continue;
        char pair[256];
        snprintf(pair, sizeof(pair), "%s/%s", owner, name);
        if (strcmp(pair, which) == 0) return i;
    }
    return -1;
}

/* GET /api/jobs/:id/export?format=pdf
 *
 * The whole reason a finished job keeps its AnalysisResult: any of the seven
 * formats can be produced on demand, without another network round trip. The
 * exporter settings come from the snapshot taken when the job ran. */
static void handle_export(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    char id[64];
    url_decode(rt->params[0], id, sizeof(id));

    char fmt[32] = "";
    if (!req_query(req, "format", fmt, sizeof(fmt)) || !fmt[0]) {
        res_error(res, 400, "bad_request", "format is required");
        return;
    }
    if (!format_is_known(fmt)) {
        res_error(res, 400, "unsupported_format",
                  "formats are json, md, txt, html, csv, xlsx and pdf");
        return;
    }
    /* As wide as a stored repository spec: a URL or SSH address is longer than a
 * bare owner/repo, and truncating it here would make it match nothing. */
char which[256] = "";
    req_query(req, "which", which, sizeof(which));

    Job *job = job_find(id);
    if (!job) { res_error(res, 404, "not_found", "no such job"); return; }
    if (job->state == JOB_QUEUED || job->state == JOB_RUNNING) {
        res_error(res, 409, "not_ready", "wait for the job to finish");
        return;
    }
    if (job->state != JOB_DONE) {
        res_error(res, 409, "not_available", "the job produced no exportable report");
        return;
    }

    Config cfg = job->export_cfg;
    apply_lang_override(req, &cfg);

    char *buf = NULL;
    size_t len = 0;
    ErrorCode e;
    int idx = -1;
    if (job->kind == JOB_KIND_COMPARE) {
        if (strcmp(fmt, "json") != 0 && strcmp(fmt, "md") != 0) {
            res_error(res, 400, "unsupported_format",
                      "comparisons export as json or md");
            return;
        }
        e = compare_render(&job->compare, fmt, &buf, &len);
    } else {
        if (which[0]) {
            idx = report_index_for(job, which);
            /* A job that produced one report has only one thing `which` could
             * have meant, so an unmatched spelling is not worth a 404. */
            if (idx < 0 && job->report_count == 1) idx = 0;
            if (idx < 0) {
                res_error(res, 404, "not_found", "that repository is not part of this job");
                return;
            }
        } else if (job->report_count > 1) {
            res_error(res, 400, "ambiguous",
                      "a multi-repository job needs ?which=<repo>");
            return;
        } else {
            idx = 0;
        }
        if (job->report_status[idx] != 0) {
            res_error(res, 409, "not_available",
                      "that repository's analysis failed");
            return;
        }
        e = analysis_render(&job->reports[idx], &cfg, fmt, &buf, &len);
    }

    if (e != GS_OK || !buf) {
        free(buf);
        res_error_code(res, e == GS_OK ? ERR_EXPORT : e, fmt);
        return;
    }

    char base[256] = "report";
    if (job->kind != JOB_KIND_COMPARE && job->report_count > 0) {
        /* Name the file after the repository the report shows, so a URL or SSH
         * submission does not produce "https:__github.com_owner_repo.pdf". */
        char owner[128], name[128], platform[16];
        if (repo_spec_resolve(job->report_repo[idx], owner, sizeof(owner),
                              name, sizeof(name), platform, sizeof(platform)) == 0)
            snprintf(base, sizeof(base), "%s_%s", owner, name);
        else
            snprintf(base, sizeof(base), "%s", job->report_repo[idx]);
        for (char *p = base; *p; p++) if (*p == '/' || *p == '\\') *p = '_';
    } else if (job->owner[0]) {
        snprintf(base, sizeof(base), "%s_%s", job->owner, job->repo);
    }

    /* Content-Disposition carries both forms: a readable ASCII fallback and
     * the RFC 5987 encoded form for non-ASCII repository names. */
    char disp[1024];
    snprintf(disp, sizeof(disp),
             "Content-Disposition: attachment; filename=\"%s.%s\"; "
             "filename*=UTF-8''%s.%s",
             base, fmt, base, fmt);
    res_header(res, disp);
    res_bytes(res, 200, format_mime(fmt), buf, len);
    free(buf);
}

/* POST /api/jobs/:id/cancel */
static void handle_cancel(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    char id[64];
    url_decode(rt->params[0], id, sizeof(id));

    if (job_cancel(id) != 0) {
        res_error(res, 404, "not_found", "no such job");
        return;
    }
    res_no_content(res);
}

/* GET /api/jobs/:id/events -- server-sent events.
 *
 * Replays the progress log so a client that connects late still sees the full
 * history, then tails new entries until the job reaches a terminal state. A
 * comment heartbeat keeps intermediaries from closing an idle connection. */
static void handle_events(Request *req, Response *res, void *user) {
    (void)user;
    const Route *rt = (const Route *)req->route;
    char id[64];
    url_decode(rt->params[0], id, sizeof(id));

    Job *job = job_find(id);
    if (!job) {
        res_error(res, 404, "not_found", "no such job");
        return;
    }

    SseStream *stream = NULL;
    if (!sse_begin(req, res, &stream)) {
        res_error(res, 500, "stream_failed", "could not open the event stream");
        return;
    }
    sse_attach(stream, id);

    /* Tell the client which job it is watching before any progress arrives. */
    const char *state_name = job->state == JOB_DONE ? "done"
                           : job->state == JOB_FAILED ? "failed"
                           : job->state == JOB_CANCELLED ? "cancelled"
                           : job->state == JOB_RUNNING ? "running" : "queued";
    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "id", job->id);
    cJSON_AddStringToObject(hello, "state", state_name);
    char *htext = cJSON_PrintUnformatted(hello);
    cJSON_Delete(hello);
    if (htext) {
        sse_event(stream, "hello", htext);
        free(htext);
    }

    int last_seq = 0;
    JobState st = JOB_QUEUED;
    time_t deadline = time(NULL) + 3600;
    int beats = 0;

    while (time(NULL) < deadline) {
        cJSON *batch = job_progress_since(job, &last_seq, &st);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, batch) {
            const cJSON *state = cJSON_GetObjectItem(item, "state");
            int sv = state ? state->valueint : 0;
            char *text = cJSON_PrintUnformatted(item);
            if (text) {
                sse_event(stream,
                          sv == ANIM_PROG_OK ? "stage-done" :
                          sv == ANIM_PROG_FAIL ? "stage-failed" : "progress",
                          text);
                free(text);
            }
        }
        cJSON_Delete(batch);

        if (st == JOB_DONE || st == JOB_FAILED || st == JOB_CANCELLED) break;

        struct timespec nap = { 0, 200 * 1000 * 1000 };
        nanosleep(&nap, NULL);

        if (++beats % 75 == 0) sse_comment(stream, "ping");
    }

    /* Emit the terminal event here rather than from job_finish, so a client
     * that attached after the work already completed still gets one. */
    {
        JobState final_state = JOB_QUEUED;
        cJSON *final = job_terminal_json(job, &final_state);
        if (final) {
            char *text = cJSON_PrintUnformatted(final);
            cJSON_Delete(final);
            if (text) {
                sse_event(stream,
                          final_state == JOB_DONE ? "done" :
                          final_state == JOB_CANCELLED ? "cancelled" : "error",
                          text);
                free(text);
            }
        }
    }

    sse_detach(stream, id);
    sse_end(stream);
}

void api_register_jobs(Router *r) {
    router_add(r, "POST", "/api/jobs/analyze", handle_analyze, NULL);
    router_add(r, "POST", "/api/jobs/compare", handle_compare, NULL);
    router_add(r, "GET",  "/api/jobs", handle_list, NULL);
    router_add(r, "GET",  "/api/jobs/:id", handle_get, NULL);
    router_add(r, "GET",  "/api/jobs/:id/result", handle_result, NULL);
    router_add(r, "GET",  "/api/jobs/:id/events", handle_events, NULL);
    router_add(r, "GET",  "/api/jobs/:id/export", handle_export, NULL);
    router_add(r, "POST", "/api/jobs/:id/cancel", handle_cancel, NULL);
}