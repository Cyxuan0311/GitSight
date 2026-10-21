#define _GNU_SOURCE

#include "jobs.h"

#include "gitsight/anim.h"
#include "gitsight/color.h"
#include "gitsight/cancel.h"
#include "gitsight/config.h"
#include "gitsight/i18n.h"
#include "gitsight/platform/http.h"

#include <cJSON.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- SSE ---- */

struct SseStream {
    int fd;
    int closed;
    int job_held;              /* the job list holds a reference to this stream */
    SseStream *next;
};

/* ---- registry ---- */

typedef struct JobList {
    Job *head;                 /* newest first */
    Job *tail;                 /* oldest, for eviction */
    int count;
} JobList;

static JobList g_jobs;
static pthread_mutex_t g_jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_jobs_ready = PTHREAD_COND_INITIALIZER;
static int g_workers_running = 0;
static int g_shutting_down = 0;
static long g_job_counter = 0;

/* Which job/repo the calling thread is currently analyzing. The animation
 * listener is process-global, so thread-local attribution is what lets several
 * repositories report progress into one job without their events interleaving
 * ambiguously. */
static _Thread_local Job *tls_job;
static _Thread_local const char *tls_repo;

static const char *job_state_name(JobState s) {
    switch (s) {
        case JOB_QUEUED:    return "queued";
        case JOB_RUNNING:   return "running";
        case JOB_DONE:      return "done";
        case JOB_FAILED:    return "failed";
        case JOB_CANCELLED: return "cancelled";
        default:            return "unknown";
    }
}

static const char *job_kind_name(JobKind k) {
    switch (k) {
        case JOB_KIND_COMPARE:        return "compare";
        case JOB_KIND_SUBSCRIPTIONS: return "subscriptions";
        default:                      return "analyze";
    }
}

/* ---- progress ---- */

static void job_emit(Job *j, const char *stage, int cur, int total, int state) {
    if (!j || !stage) return;

    pthread_mutex_lock(&g_jobs_lock);
    if (j->log_count < GS_PROG_MAX) {
        ProgressEntry *e = &j->log[j->log_count++];
        snprintf(e->stage, sizeof(e->stage), "%s", stage);
        e->current = cur;
        e->total = total;
        e->state = state;
        e->at = (long)time(NULL);
    } else {
        j->log_dropped++;
    }
    j->progress_seq++;

    /* Fan out to everyone watching this job. A dead socket is simply dropped;
     * its own thread notices the failure and unsubscribes. */
    for (SseStream *s = (SseStream *)j->subs; s; s = s->next) {
        if (s->closed) continue;
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "stage", stage);
        cJSON_AddNumberToObject(data, "current", cur);
        cJSON_AddNumberToObject(data, "total", total);
        cJSON_AddNumberToObject(data, "seq", j->progress_seq);
        const char *ev = state == ANIM_PROG_START ? "progress"
                        : state == ANIM_PROG_OK    ? "stage-done"
                        : state == ANIM_PROG_FAIL  ? "stage-failed" : "progress";
        char *text = cJSON_PrintUnformatted(data);
        cJSON_Delete(data);
        if (text) {
            char frame[512];
            int n = snprintf(frame, sizeof(frame),
                             "event: %s\nid: %d\ndata: %s\n\n", ev, j->progress_seq, text);
            if (n > 0) {
                size_t left = (size_t)n;
                const char *p = frame;
                while (left > 0) {
                    ssize_t w = write(s->fd, p, left);
                    if (w > 0) { p += w; left -= (size_t)w; continue; }
                    if (w < 0 && errno == EINTR) continue;
                    s->closed = 1;
                    break;
                }
            }
            free(text);
        }
    }
    pthread_mutex_unlock(&g_jobs_lock);
}

static void on_anim_progress(const char *stage, int cur, int total,
                             int state, void *user) {
    (void)user;
    Job *j = tls_job;
    if (!j || !stage) return;

    char full[160];
    if (tls_repo && *tls_repo)
        snprintf(full, sizeof(full), "%s - %s", tls_repo, stage);
    else
        snprintf(full, sizeof(full), "%s", stage);
    job_emit(j, full, cur, total, state);
}

/* ---- lifecycle ---- */

static void job_release_locked(Job *j) {
    /* `reports`, `report_repo` and `report_status` are three flat calloc'd
     * blocks, so only the blocks themselves are freed -- `report_repo[i]` is an
     * offset into one of them, never its own allocation. They are also NULL for
     * a comparison, which sets report_count without ever producing per-repo
     * reports, hence the guard rather than a loop on report_count alone. */
    if (j->reports)
        for (int i = 0; i < j->report_count; i++)
            analysis_result_free(&j->reports[i]);
    free(j->reports);
    free(j->report_repo);
    free(j->report_status);
    j->reports = NULL;
    j->report_repo = NULL;
    j->report_status = NULL;
    j->report_count = 0;
    free(j->repo_list);
    j->repo_list = NULL;
    free(j->opts_list);
    j->opts_list = NULL;
    j->repo_count = 0;

    if (j->has_compare) {
        compare_result_free(&j->compare);
        j->has_compare = 0;
    }
}

static void job_destroy(Job *j) {
    if (!j) return;
    job_release_locked(j);
    free(j);
}

Job *job_create(JobKind kind, const char *title) {
    Job *j = calloc(1, sizeof(Job));
    if (!j) return NULL;
    j->kind = kind;
    j->state = JOB_QUEUED;
    j->created_at = (long)time(NULL);
    snprintf(j->title, sizeof(j->title), "%s", title ? title : "");
    atomic_init(&j->cancel_requested, 0);

    /* Ids are opaque and monotonic within a run; callers never parse them. */
    pthread_mutex_lock(&g_jobs_lock);
    snprintf(j->id, sizeof(j->id), "job_%ld_%u",
             (long)time(NULL) % 100000000, (unsigned)(++g_job_counter % 100000));
    j->next = g_jobs.head;
    if (g_jobs.head) g_jobs.head->prev = j;
    g_jobs.head = j;
    if (!g_jobs.tail) g_jobs.tail = j;
    g_jobs.count++;
    pthread_mutex_unlock(&g_jobs_lock);
    return j;
}

/* Keeps the retained list bounded by dropping finished jobs from the tail. */
static void job_evict_locked(void) {
    while (g_jobs.count > GS_JOBS_MAX && g_jobs.tail) {
        Job *victim = g_jobs.tail;
        if (victim->state == JOB_RUNNING || victim->state == JOB_QUEUED) break;
        g_jobs.tail = victim->prev;
        if (g_jobs.tail) g_jobs.tail->next = NULL;
        else g_jobs.head = NULL;
        g_jobs.count--;
        job_destroy(victim);
    }
}

Job *job_find(const char *id) {
    if (!id) return NULL;
    pthread_mutex_lock(&g_jobs_lock);
    Job *found = NULL;
    for (Job *j = g_jobs.head; j; j = j->next)
        if (strcmp(j->id, id) == 0) { found = j; break; }
    pthread_mutex_unlock(&g_jobs_lock);
    return found;
}

int job_cancel(const char *id) {
    Job *j = job_find(id);
    if (!j) return -1;
    atomic_store(&j->cancel_requested, 1);
    gs_cancel_bind(&j->cancel_requested);
    gs_cancel_request();
    gs_cancel_bind(NULL);

    pthread_mutex_lock(&g_jobs_lock);
    if (j->state == JOB_QUEUED) {
        j->state = JOB_CANCELLED;
        j->finished_at = (long)time(NULL);
        snprintf(j->error_text, sizeof(j->error_text), "Cancelled before it started");
    }
    pthread_mutex_unlock(&g_jobs_lock);
    return 0;
}

/* ---- request-derived state ---- */

void job_set_options(Job *j, const Options *opts) {
    if (j && opts) j->opts = *opts;
}

int job_add_repo(Job *j, const char *repo, const Options *opts) {
    if (!j || !repo || !*repo) return -1;
    if (j->repo_count >= 16) return -1;

    char (*grown)[256] = realloc(j->repo_list, (size_t)(j->repo_count + 1) * 256);
    if (!grown) return -1;
    j->repo_list = grown;
    snprintf(j->repo_list[j->repo_count], 256, "%s", repo);

    if (opts) {
        Options *og = realloc(j->opts_list, (size_t)(j->repo_count + 1) * sizeof(Options));
        if (!og) return -1;
        j->opts_list = og;
        j->opts_list[j->repo_count] = *opts;
    }
    j->repo_count++;
    if (j->repo_count == 1) {
        const char *slash = strchr(repo, '/');
        if (slash) {
            snprintf(j->owner, sizeof(j->owner), "%.*s", (int)(slash - repo), repo);
            snprintf(j->repo, sizeof(j->repo), "%s", slash + 1);
        }
    }
    return 0;
}

void job_set_compare(Job *j, int days_a, int days_b) {
    if (!j) return;
    j->compare_days_a = days_a;
    j->compare_days_b = days_b;
}

/* ---- serialization ---- */

static cJSON *analysis_to_cjson(const AnalysisResult *r, const Config *cfg) {
    char *buf = NULL;
    size_t len = 0;
    if (analysis_render(r, cfg, "json", &buf, &len) != GS_OK || !buf)
        return cJSON_CreateObject();
    cJSON *parsed = cJSON_Parse(buf);
    free(buf);
    return parsed ? parsed : cJSON_CreateObject();
}

/* Fetch failures are warnings: a repository with no releases still produces a
 * useful report, exactly as on the CLI. The array is always present so a
 * client can read `warnings.length` without checking for the key first. */
static void add_warnings(cJSON *parent, const AnalysisResult *r) {
    cJSON *arr = cJSON_AddArrayToObject(parent, "warnings");
    if (!arr) return;
    for (int i = 0; i < ANALYSIS_FETCH_KINDS; i++) {
        if (r->fetch_err[i] == GS_OK) continue;
        cJSON *w = cJSON_CreateObject();
        cJSON_AddStringToObject(w, "resource", analysis_fetch_labels[i]);
        cJSON_AddStringToObject(w, "error", gs_error_string(r->fetch_err[i]));
        cJSON_AddStringToObject(w, "detail", r->fetch_err_text[i]);
        cJSON_AddItemToArray(arr, w);
    }
    if (r->llm_err[0]) {
        cJSON *w = cJSON_CreateObject();
        cJSON_AddStringToObject(w, "resource", "llm summary");
        cJSON_AddStringToObject(w, "error", r->llm_err);
        cJSON_AddStringToObject(w, "detail",
                                "Fell back to the locally generated summary");
        cJSON_AddItemToArray(arr, w);
    }
}

cJSON *job_result_json(const Job *j, const Config *cfg) {
    if (!j) return NULL;
    const Config *snap = cfg ? cfg : &j->export_cfg;

    if (j->kind == JOB_KIND_COMPARE) {
        cJSON *root = cJSON_CreateObject();
        const Report *a = &j->compare.period_a.report;
        const Report *b = &j->compare.period_b.report;
        cJSON_AddStringToObject(root, "kind", "compare");
        cJSON_AddStringToObject(root, "owner", a->owner);
        cJSON_AddStringToObject(root, "repo", a->repo);
        cJSON_AddNumberToObject(root, "period_days_a", j->compare.days_a);
        cJSON_AddNumberToObject(root, "period_days_b", j->compare.days_b);

        const struct { const char *key; double a, b; } pairs[] = {
            { "pr_total", a->pr_total, b->pr_total },
            { "pr_merged", a->pr_merged, b->pr_merged },
            { "commit_total", a->commit_total, b->commit_total },
            { "author_total", a->author_total, b->author_total },
            { "issue_total", a->issue_total, b->issue_total },
            { "release_total", a->release_total, b->release_total },
            { "contributor_total", a->contributor_total, b->contributor_total },
            { "pr_avg_merge_hours", a->pr_avg_merge_hours, b->pr_avg_merge_hours },
            { "issue_avg_close_hours", a->issue_avg_close_hours, b->issue_avg_close_hours },
            { "activity_count", a->activity_count, b->activity_count }
        };
        cJSON *ma = cJSON_AddObjectToObject(root, "period_a");
        cJSON *mb = cJSON_AddObjectToObject(root, "period_b");
        for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
            cJSON_AddNumberToObject(ma, pairs[i].key, pairs[i].a);
            cJSON_AddNumberToObject(mb, pairs[i].key, pairs[i].b);
        }
        cJSON_AddStringToObject(ma, "top_author", a->top_author);
        cJSON_AddStringToObject(ma, "top_contributor", a->top_contributor);
        cJSON_AddStringToObject(ma, "period", a->period);
        cJSON_AddNumberToObject(ma, "period_days", a->period_days);
        cJSON_AddStringToObject(mb, "top_author", b->top_author);
        cJSON_AddStringToObject(mb, "top_contributor", b->top_contributor);
        cJSON_AddStringToObject(mb, "period", b->period);
        cJSON_AddNumberToObject(mb, "period_days", b->period_days);
        cJSON *aa = cJSON_AddObjectToObject(ma, "activity");
        cJSON *ab = cJSON_AddObjectToObject(mb, "activity");
        for (int i = 0; i < a->activity_count; i++)
            cJSON_AddNumberToObject(aa, a->activity_labels[i], a->activity_values[i]);
        for (int i = 0; i < b->activity_count; i++)
            cJSON_AddNumberToObject(ab, b->activity_labels[i], b->activity_values[i]);

        if (j->compare.has_trend)
            cJSON_AddStringToObject(root, "trend_summary", j->compare.trend_summary);
        cJSON_AddStringToObject(root, "llm_provider", j->compare.llm_provider);
        cJSON_AddItemToObject(root, "report_a", analysis_to_cjson(&j->compare.period_a, snap));
        cJSON_AddItemToObject(root, "report_b", analysis_to_cjson(&j->compare.period_b, snap));
        return root;
    }

    if (j->report_count == 1) {
        cJSON *root = analysis_to_cjson(&j->reports[0], snap);
        add_warnings(root, &j->reports[0]);
        cJSON_AddNumberToObject(root, "status", j->report_status ? j->report_status[0] : 0);
        return root;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "kind", "batch");
    cJSON_AddNumberToObject(root, "total", j->report_count);
    int ok = 0;
    for (int i = 0; i < j->report_count; i++) if (j->report_status[i] == 0) ok++;
    cJSON_AddNumberToObject(root, "ok", ok);
    cJSON_AddNumberToObject(root, "failed", j->report_count - ok);
    cJSON *arr = cJSON_AddArrayToObject(root, "reports");
    for (int i = 0; i < j->report_count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "repo", j->report_repo[i]);
        cJSON_AddNumberToObject(entry, "status", j->report_status[i]);
        add_warnings(entry, &j->reports[i]);
        cJSON_AddItemToObject(entry, "report", analysis_to_cjson(&j->reports[i], snap));
        cJSON_AddItemToArray(arr, entry);
    }
    return root;
}

static cJSON *progress_array_locked(const Job *j) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < j->log_count; i++) {
        const ProgressEntry *e = &j->log[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "stage", e->stage);
        cJSON_AddNumberToObject(o, "current", e->current);
        cJSON_AddNumberToObject(o, "total", e->total);
        cJSON_AddNumberToObject(o, "state", e->state);
        cJSON_AddNumberToObject(o, "at", (double)e->at);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

cJSON *job_to_json(const Job *j) {
    if (!j) return NULL;
    pthread_mutex_lock(&g_jobs_lock);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", j->id);
    cJSON_AddStringToObject(root, "kind", job_kind_name(j->kind));
    cJSON_AddStringToObject(root, "state", job_state_name(j->state));
    cJSON_AddStringToObject(root, "title", j->title);
    cJSON_AddStringToObject(root, "owner", j->owner);
    cJSON_AddStringToObject(root, "repo", j->repo);
    cJSON_AddNumberToObject(root, "created_at", (double)j->created_at);
    cJSON_AddNumberToObject(root, "started_at", (double)j->started_at);
    cJSON_AddNumberToObject(root, "finished_at", (double)j->finished_at);
    cJSON_AddNumberToObject(root, "progress_seq", j->progress_seq);
    cJSON_AddNumberToObject(root, "report_count", j->report_count);
    if (j->error_text[0]) cJSON_AddStringToObject(root, "error", j->error_text);
    if (j->state == JOB_DONE || j->state == JOB_FAILED) {
        cJSON *prog = progress_array_locked(j);
        cJSON_AddItemToObject(root, "progress", prog);
    }
    cJSON_AddBoolToObject(root, "cancellable",
                          j->state == JOB_QUEUED || j->state == JOB_RUNNING);
    pthread_mutex_unlock(&g_jobs_lock);
    return root;
}

cJSON *job_progress_since(Job *j, int *since, JobState *state_out) {
    cJSON *arr = cJSON_CreateArray();
    if (!j || !since) return arr;
    pthread_mutex_lock(&g_jobs_lock);
    if (state_out) *state_out = j->state;
    for (int i = *since; i < j->log_count; i++) {
        const ProgressEntry *e = &j->log[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "stage", e->stage);
        cJSON_AddNumberToObject(o, "current", e->current);
        cJSON_AddNumberToObject(o, "total", e->total);
        cJSON_AddNumberToObject(o, "state", e->state);
        cJSON_AddNumberToObject(o, "at", (double)e->at);
        cJSON_AddItemToArray(arr, o);
    }
    *since = j->log_count;
    pthread_mutex_unlock(&g_jobs_lock);
    return arr;
}

cJSON *job_terminal_json(Job *j, JobState *state_out) {
    cJSON *root = cJSON_CreateObject();
    if (!j || !root) return NULL;

    pthread_mutex_lock(&g_jobs_lock);
    JobState st = j->state;
    char error[512] = "";
    snprintf(error, sizeof(error), "%s", j->error_text);
    pthread_mutex_unlock(&g_jobs_lock);

    if (state_out) *state_out = st;
    if (st == JOB_QUEUED || st == JOB_RUNNING) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(root, "id", j->id);
    cJSON_AddStringToObject(root, "state",
                            st == JOB_DONE ? "done" :
                            st == JOB_CANCELLED ? "cancelled" : "failed");
    if (error[0]) cJSON_AddStringToObject(root, "error", error);
    return root;
}

cJSON *job_list_json(int limit) {
    pthread_mutex_lock(&g_jobs_lock);
    cJSON *arr = cJSON_CreateArray();
    int n = 0;
    for (Job *j = g_jobs.head; j && (limit <= 0 || n < limit); j = j->next, n++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", j->id);
        cJSON_AddStringToObject(o, "kind", job_kind_name(j->kind));
        cJSON_AddStringToObject(o, "state", job_state_name(j->state));
        cJSON_AddStringToObject(o, "title", j->title);
        cJSON_AddNumberToObject(o, "created_at", (double)j->created_at);
        cJSON_AddNumberToObject(o, "finished_at", (double)j->finished_at);
        cJSON_AddNumberToObject(o, "progress_seq", j->progress_seq);
        cJSON_AddItemToArray(arr, o);
    }
    pthread_mutex_unlock(&g_jobs_lock);
    return arr;
}

/* ---- work ---- */

/* Runs one repo on the calling thread with progress attributed to (job, repo)
 * and cancellation bound to the job's own flag. */
typedef struct {
    Job   *job;
    Config cfg;
    int    index;
} RepoRun;

static void *repo_worker(void *arg) {
    RepoRun *run = (RepoRun *)arg;
    Job *j = run->job;

    tls_job = j;
    tls_repo = j->repo_list[run->index];
    gs_cancel_bind(&j->cancel_requested);
    gs_cancel_reset();

    BatchTask t;
    memset(&t, 0, sizeof(t));
    t.opts = j->opts_list ? j->opts_list[run->index] : j->opts;
    t.cfg = &run->cfg;
    t.repo = j->repo_list[run->index];
    t.label = t.repo;
    t.batch = j->repo_count > 1;
    t.result_out = &j->reports[run->index];
    t.result = batch_run_one(&t);

    j->report_status[run->index] = t.result;

    gs_cancel_bind(NULL);
    tls_job = NULL;
    tls_repo = NULL;
    return NULL;
}

/* Replaces the generic pool so progress stays attributed per repository while
 * repos still run concurrently. Mirrors run_batch_tasks' bounded thread model:
 * serial fetch inside a repo, parallel across repos. */
static int run_job_repos(Job *j, const Config *cfg) {
    int n = j->repo_count;
    j->reports = calloc((size_t)n, sizeof(AnalysisResult));
    j->report_repo = calloc((size_t)n, sizeof(char) * 256);
    j->report_status = calloc((size_t)n, sizeof(int));
    if (!j->reports || !j->report_repo || !j->report_status) return 1;
    j->report_count = n;
    for (int i = 0; i < n; i++) {
        analysis_result_init(&j->reports[i]);
        snprintf(j->report_repo[i], 256, "%s", j->repo_list[i]);
    }

    Options o = j->opts;
    o.serial_fetch = 1;
    int par = batch_parallelism(&o, cfg);
    if (par > GS_JOBS_MAX_WORKERS) par = GS_JOBS_MAX_WORKERS;
    if (par > n) par = n;

    if (par <= 1) {
        for (int i = 0; i < n; i++) {
            RepoRun run = { j, *cfg, i };
            snprintf(j->report_repo[i], 256, "%s", j->repo_list[i]);
            repo_worker(&run);
        }
        return 0;
    }

    pthread_t th[GS_JOBS_MAX_WORKERS];
    RepoRun runs[GS_JOBS_MAX_WORKERS];
    int started = 0;
    for (int i = 0; i < n; i++) {
        if (started >= par) break;
        snprintf(j->report_repo[i], 256, "%s", j->repo_list[i]);
        runs[started].job = j;
        runs[started].cfg = *cfg;
        runs[started].index = i;
        if (pthread_create(&th[started], NULL, repo_worker, &runs[started]) == 0)
            started++;
        else {
            repo_worker(&runs[started]);
            break;
        }
    }
    for (int i = 0; i < started; i++) pthread_join(th[i], NULL);
    /* Repos beyond the pool window still have to run. */
    for (int i = started + 1; i < n; i++) {
        RepoRun run = { j, *cfg, i };
        if (!j->report_repo[i][0]) snprintf(j->report_repo[i], 256, "%s", j->repo_list[i]);
        repo_worker(&run);
    }
    return 0;
}

static void job_finish(Job *j, JobState state, ErrorCode err, const char *detail) {
    pthread_mutex_lock(&g_jobs_lock);
    j->state = state;
    j->error = err;
    if (detail) snprintf(j->error_text, sizeof(j->error_text), "%s", detail);
    j->finished_at = (long)time(NULL);
    pthread_mutex_unlock(&g_jobs_lock);
    /* Watchers learn the outcome from the SSE handler, which polls this state.
     * Broadcasting here as well would emit the terminal frame twice for a
     * client that is already attached, and not at all for one that attaches
     * later -- so the stream owns exactly one terminal event. */
}

static void *job_execute(void *arg) {
    Job *j = (Job *)arg;

    pthread_mutex_lock(&g_jobs_lock);
    if (j->state == JOB_CANCELLED) {
        pthread_mutex_unlock(&g_jobs_lock);
        return NULL;
    }
    j->state = JOB_RUNNING;
    j->started_at = (long)time(NULL);
    pthread_mutex_unlock(&g_jobs_lock);

    /* Reading the config under the state lock keeps a settings edit from
     * landing halfway through this job's export snapshot. */
    Config cfg;
    server_load_config(&cfg);
    if (j->opts.lang[0]) snprintf(cfg.lang, sizeof(cfg.lang), "%s", j->opts.lang);
    gs_set_lang_str(cfg.lang);
    if (j->opts.platform[0])
        snprintf(cfg.platform, sizeof(cfg.platform), "%s", j->opts.platform);
    if (j->opts.api_base[0])
        snprintf(cfg.api_base, sizeof(cfg.api_base), "%s", j->opts.api_base);
    if (j->opts.proxy[0])
        snprintf(cfg.proxy, sizeof(cfg.proxy), "%s", j->opts.proxy);
    if (j->opts.token[0]) config_merge(&cfg, 0, 0, NULL, NULL, NULL, NULL,
                                       COLOR_AUTO, j->opts.token, NULL, NULL, NULL);
    if (j->opts.since_days > 0) cfg.default_days = j->opts.since_days;
    if (j->opts.max_pages > 0) cfg.max_pages = j->opts.max_pages;
    if (j->opts.branch[0])
        snprintf(cfg.branch, sizeof(cfg.branch), "%s", j->opts.branch);
    if (j->opts.export_extra[0])
        snprintf(cfg.export_extra, sizeof(cfg.export_extra), "%s", j->opts.export_extra);
    /* The overrides arrive after the config was loaded, so they have to be
     * applied here: the exporter reads the fields they change when the report
     * is rendered from the snapshot below. */
    config_apply_export_overrides(&cfg);

    /* The stored snapshot drives rendering for this job only; the live config
     * above drives the fetch. */
    j->export_cfg = cfg;

    anim_set_listener(on_anim_progress, NULL);
    tls_job = j;
    tls_repo = j->repo_count == 1 ? j->repo_list[0] : NULL;

    int rc = 0;
    ErrorCode err = GS_OK;

    if (j->kind == JOB_KIND_COMPARE) {
        j->has_compare = 1;
        memset(&j->compare, 0, sizeof(j->compare));
        rc = compare_analyze(&j->opts, cfg, j->repo_list[0],
                            j->compare_days_a, j->compare_days_b, &j->compare);
        if (rc == 2) err = ERR_CANCELLED;
        else if (rc != 0) err = ERR_NETWORK;
        j->report_count = j->repo_count;
    } else {
        rc = run_job_repos(j, &cfg);
        if (rc != 0) err = ERR_MEMORY;
    }

    int cancelled = atomic_load(&j->cancel_requested);

    tls_job = NULL;
    tls_repo = NULL;
    anim_set_listener(NULL, NULL);

    if (cancelled) {
        job_finish(j, JOB_CANCELLED, ERR_CANCELLED, "Cancelled by request");
    } else if (err != GS_OK) {
        job_finish(j, JOB_FAILED, err, gs_error_string(err));
    } else {
        job_finish(j, JOB_DONE, GS_OK, NULL);
    }

    /* The analyses stay owned by the job: the download endpoint renders from
     * them, so they live until the job is evicted. */
    return NULL;
}

int jobs_start(int workers) {
    if (workers < 1) workers = 1;
    if (workers > GS_JOBS_MAX_WORKERS) workers = GS_JOBS_MAX_WORKERS;
    pthread_mutex_lock(&g_jobs_lock);
    g_shutting_down = 0;
    pthread_mutex_unlock(&g_jobs_lock);
    return workers;
}

void jobs_shutdown(void) {
    pthread_mutex_lock(&g_jobs_lock);
    g_shutting_down = 1;
    pthread_cond_broadcast(&g_jobs_ready);
    pthread_mutex_unlock(&g_jobs_lock);
}

int jobs_submit(Job *j) {
    if (!j) return -1;
    pthread_t th;
    if (pthread_create(&th, NULL, job_execute, j) != 0) {
        pthread_mutex_lock(&g_jobs_lock);
        j->state = JOB_FAILED;
        j->error = ERR_THREAD;
        snprintf(j->error_text, sizeof(j->error_text), "Could not start a worker");
        j->finished_at = (long)time(NULL);
        pthread_mutex_unlock(&g_jobs_lock);
        return -1;
    }
    pthread_detach(th);

    pthread_mutex_lock(&g_jobs_lock);
    job_evict_locked();
    pthread_mutex_unlock(&g_jobs_lock);
    return 0;
}

int jobs_active_count(void) {
    pthread_mutex_lock(&g_jobs_lock);
    int n = 0;
    for (Job *j = g_jobs.head; j; j = j->next)
        if (j->state == JOB_RUNNING || j->state == JOB_QUEUED) n++;
    pthread_mutex_unlock(&g_jobs_lock);
    return n;
}

/* ---- SSE streams ---- */

int sse_begin(Request *req, Response *res, SseStream **out) {
    if (!req || !res || !out) return 0;

    int fd = dup(req->fd);
    if (fd < 0) return 0;

    static char head[512];
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache, no-transform\r\n"
        "Connection: close\r\n"
        "X-Accel-Buffering: no\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n");
    if (write(fd, head, (size_t)n) != n) {
        close(fd);
        return 0;
    }

    SseStream *s = calloc(1, sizeof(SseStream));
    if (!s) {
        close(fd);
        return 0;
    }
    s->fd = fd;
    res_stream(res, "text/event-stream");
    *out = s;
    return 1;
}

void sse_event(SseStream *s, const char *event, const char *json_data) {
    if (!s || s->closed || !event) return;
    char *frame = malloc(strlen(event) + strlen(json_data ? json_data : "") + 32);
    if (!frame) return;
    int n = snprintf(frame, strlen(event) + strlen(json_data ? json_data : "") + 32,
                     "event: %s\ndata: %s\n\n", event, json_data ? json_data : "{}");
    size_t left = (size_t)n;
    char *p = frame;
    while (left > 0) {
        ssize_t w = write(s->fd, p, left);
        if (w > 0) { p += w; left -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        s->closed = 1;
        break;
    }
    free(frame);
}

/* A comment line keeps proxies and browsers from timing the connection out
 * while nothing is happening. */
void sse_comment(SseStream *s, const char *text) {
    if (!s || s->closed) return;
    char frame[256];
    int n = snprintf(frame, sizeof(frame), ": %s\n\n", text ? text : "ping");
    if (n > 0 && write(s->fd, frame, (size_t)n) != n) s->closed = 1;
}

void sse_end(SseStream *s) {
    if (!s) return;
    if (!s->closed) {
        static const char tail[] = "event: close\ndata: {}\n\n";
        ssize_t ignored = write(s->fd, tail, sizeof(tail) - 1);
        (void)ignored;
    }
    close(s->fd);
    free(s);
}

/* Attaches a stream to a job so it receives that job's progress events. */
int sse_attach(SseStream *s, const char *job_id) {
    Job *j = job_find(job_id);
    if (!j || !s) return -1;
    s->job_held = 1;
    pthread_mutex_lock(&g_jobs_lock);
    s->next = (SseStream *)j->subs;
    j->subs = s;
    pthread_mutex_unlock(&g_jobs_lock);
    return 0;
}

void sse_detach(SseStream *s, const char *job_id) {
    Job *j = job_find(job_id);
    if (!j || !s) return;
    pthread_mutex_lock(&g_jobs_lock);
    SseStream **pp = (SseStream **)&j->subs;
    while (*pp) {
        if (*pp == s) {
            *pp = s->next;
            s->next = NULL;
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_jobs_lock);
    s->job_held = 0;
}