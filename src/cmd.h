#ifndef GS_CMD_H
#define GS_CMD_H

#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/models.h"
#include "gitsight/export/exporter.h"
#include <stddef.h>

void get_config_path(char *buf, size_t len, const Options *opts);
int  build_fopts(FormatOptions *fopts, const Config *cfg, int close_fp, const char *format);
void free_fopts(FormatOptions *fopts);
void print_batch_summary(const char *const *repos, const int *statuses, int n);

/* Everything one repository's analysis produced: the aggregate Report plus the
 * raw item vectors the exporters need. analyze_repo() owns the vectors on
 * success, so the caller must release them with analysis_result_free().
 *
 * fetch_err / fetch_err_text expose the per-resource fetch failures that the
 * CLI prints as "-> Fetching X failed: Y" lines. Structured fields let a
 * caller report them without scraping colored terminal output.
 * Index order matches FetchJob: 0=PRs 1=commits 2=issues 3=releases 4=meta
 * 5=contributors 6=languages. A missing repository is not an error. */
#define ANALYSIS_FETCH_KINDS 7
extern const char *const analysis_fetch_labels[ANALYSIS_FETCH_KINDS];

typedef struct {
    Report report;
    Vector prs, commits, issues, releases, contributors, langs;
    ErrorCode fetch_err[ANALYSIS_FETCH_KINDS];
    char    fetch_err_text[ANALYSIS_FETCH_KINDS][256];
    char    llm_err[256];
    int     used_llm;
} AnalysisResult;

int  analyze_repo(const Options *opts, Config cfg, const char *repo_arg,
                  int batch, AnalysisResult *out);
void analysis_result_init(AnalysisResult *r);
void analysis_result_free(AnalysisResult *r);

/* analyze_repo() returns 0 on success, 1 on failure, and 2 when a cancel was
 * requested (see cancel.h) before the report could be produced. */

/* Reduces a repository argument -- "owner/repo" or a forge URL, with an optional
 * ".git" suffix or userinfo -- to owner and repo. A URL additionally yields the
 * platform its host implies (empty when the host says nothing, so an explicit
 * setting still wins). Returns 0 on success, -1 when the argument cannot name a
 * repository. Shared by the CLI and the daemon so both reject the same input
 * before a run starts rather than reporting an empty report as a success. */
int  repo_spec_resolve(const char *arg,
                       char *owner, size_t owner_len,
                       char *repo, size_t repo_len,
                       char *platform, size_t platform_len);

/* Render one analysis into memory with the given exporter. Works for all
 * seven formats, binary ones included: the exporter writes to a FILE* that
 * backs a heap buffer, so callers never touch the filesystem. `config` holds
 * the section/color/layout settings to apply. On success *buf is a malloc'd
 * block of *len bytes that the caller frees, and rc is 0. */
ErrorCode analysis_render(const AnalysisResult *r, const Config *cfg,
                          const char *format, char **buf, size_t *len);

/* Compare two periods. Both analyses are owned by `out` on success, which the
 * caller releases with analysis_result_free twice (period_a, period_b).
 * trend_summary is copied from period_b's LLM summary when available. */
typedef struct {
    AnalysisResult period_a;
    AnalysisResult period_b;
    int    days_a, days_b;
    char   trend_summary[MAX_SUMMARY_LEN];
    int    has_trend;
} CompareResult;

int  run_compare(const Options *opts, Config cfg, const char *repo_arg, int d1, int d2);
int  compare_analyze(const Options *opts, Config cfg, const char *repo_arg,
                     int d1, int d2, CompareResult *out);
void compare_result_free(CompareResult *cr);
ErrorCode compare_render(const CompareResult *cr, const char *format,
                         char **buf, size_t *len);

/* One repo of a batch run: per-repo options, shared config, result. */
#define GS_BATCH_MAX_THREADS 8

typedef struct {
    Options        opts;
    const Config  *cfg;
    const char    *repo;
    const char    *label;   /* progress line, e.g. "Analyzing owner/repo" */
    int            batch;
    int            result;
    /* Optional: when set, the repo's AnalysisResult is handed back here and
     * owned by the caller. Used by the server, which keeps each repo's data
     * so any export format can be produced later without refetching. */
    AnalysisResult *result_out;
} BatchTask;

/* Concurrency for a batch: -j value, or auto (4 with a token, 1 without). */
int batch_parallelism(const Options *opts, const Config *cfg);
/* Runs all tasks (par<=1 = serial) and returns the OR of their results. */
int run_batch_tasks(BatchTask *tasks, int n, int par, int quiet);

/* Runs a single task, storing the analysis in task->result_out when that is
 * set. Exposed so a caller that needs per-repository progress attribution --
 * the server -- can drive its own bounded thread pool. */
int batch_run_one(BatchTask *t);

int cmd_analyze(Options *opts);
int cmd_config(const Options *opts);
int cmd_watch(Options *opts);
int cmd_usage(const Options *opts);

/* Starts the web console (`gs serve`). argv/argc cover the arguments *after*
 * the "serve" verb, because the server parses its own flag set. */
int cmd_serve(int argc, char **argv);

#endif /* GS_CMD_H */
