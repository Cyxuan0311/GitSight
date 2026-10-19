#ifndef GS_SERVER_JOBS_H
#define GS_SERVER_JOBS_H

#include "server.h"

#include <cJSON.h>
#include <stdatomic.h>

/* Long-running analyses run as jobs so a browser can hold an SSE stream open
 * and render progress instead of waiting on a blocked request.
 *
 * A finished job owns its AnalysisResult, which means every export format can
 * be rendered later straight from memory -- a download never refetches. The
 * exporter settings are snapshotted when the job runs, so a settings change
 * cannot retroactively alter the artifact a user downloaded for that job. */

#define GS_JOBS_MAX          128   /* retained jobs, oldest evicted first */
#define GS_JOBS_MAX_WORKERS  8
#define GS_PROG_MAX          256   /* progress log entries per job */

typedef enum {
    JOB_QUEUED = 0,
    JOB_RUNNING,
    JOB_DONE,
    JOB_FAILED,
    JOB_CANCELLED
} JobState;

typedef enum {
    JOB_KIND_ANALYZE = 0,     /* one or more repos, cross-repo parallel */
    JOB_KIND_COMPARE,
    JOB_KIND_SUBSCRIPTIONS
} JobKind;

typedef struct {
    char stage[160];
    int  current;
    int  total;
    int  state;              /* ANIM_PROG_* */
    long at;
} ProgressEntry;

typedef struct SseStream SseStream;

typedef struct Job {
    char id[40];
    JobKind kind;
    JobState state;
    char title[256];
    char owner[128];
    char repo[128];

    long created_at, started_at, finished_at;
    int  progress_seq;

    ProgressEntry log[GS_PROG_MAX];
    int log_count;
    int log_dropped;

    /* Inputs captured at submit time. */
    Options opts;              /* shared defaults */
    Options *opts_list;        /* per-repository overrides, repo_count entries,
                               * or NULL when every repo shares `opts` */
    Config  export_cfg;
    char (*repo_list)[256];
    int    repo_count;
    int    compare_days_a, compare_days_b;

    /* Outputs, owned once the job reaches a terminal state. */
    AnalysisResult *reports;
    int    report_count;
    int   *report_status;
    char (*report_repo)[256];
    CompareResult compare;
    int    has_compare;

    ErrorCode error;
    char error_text[512];

    atomic_int cancel_requested;

    SseStream *subs;
    struct Job *next;
    struct Job *prev;
} Job;

/* ---- lifecycle ---- */
int  jobs_start(int workers);
void jobs_shutdown(void);
/* Runs the job on its own thread and returns immediately. */
int  jobs_submit(Job *j);
int  jobs_active_count(void);

Job *job_create(JobKind kind, const char *title);
Job *job_find(const char *id);
int  job_cancel(const char *id);

void job_set_options(Job *j, const Options *opts);
/* Adds a repository together with its own options, for cases like
 * subscriptions where each entry can override days/platform/format/branch.
 * Passing NULL for `opts` reuses the job's shared options. */
int  job_add_repo(Job *j, const char *repo, const Options *opts);
void job_set_compare(Job *j, int days_a, int days_b);

/* Serialization. job_to_json never includes the (potentially large) result. */
cJSON *job_to_json(const Job *j);
cJSON *job_list_json(int limit);
/* Serializes the finished result payload from the job's stored analyses. */
cJSON *job_result_json(const Job *j, const Config *cfg);

/* Copies progress entries with index >= *since into a fresh array while holding
 * the registry lock, then advances the cursor and reports the job's current
 * state. Lets an SSE client resume without exposing the lock. */
cJSON *job_progress_since(Job *j, int *since, JobState *state_out);

/* Builds the terminal payload for a finished job and reports its state through
 * *state_out. Returns NULL while the job is still queued or running. The
 * SSE handler uses this so the terminal frame is emitted exactly once,
 * whether the client attached before or after the work finished. */
cJSON *job_terminal_json(Job *j, JobState *state_out);

/* ---- SSE ---- */
/* Writes the response head and hands back the raw socket. sse_end closes it.
 * sse_attach/sse_detach register the stream as a listener on a job so the
 * animation progress stream reaches the browser. */
int  sse_begin(Request *req, Response *res, SseStream **out);
void sse_event(SseStream *s, const char *event, const char *json_data);
void sse_comment(SseStream *s, const char *text);
void sse_end(SseStream *s);
int  sse_attach(SseStream *s, const char *job_id);
void sse_detach(SseStream *s, const char *job_id);

#endif /* GS_SERVER_JOBS_H */