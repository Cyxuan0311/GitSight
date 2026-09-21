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

int analyze_repo(const Options *opts, Config cfg, const char *repo_arg, int batch, Report *report_out);
int run_compare(const Options *opts, Config cfg, const char *repo_arg, int d1, int d2);

/* One repo of a batch run: per-repo options, shared config, result. */
#define GS_BATCH_MAX_THREADS 8

typedef struct {
    Options        opts;
    const Config  *cfg;
    const char    *repo;
    const char    *label;   /* progress line, e.g. "Analyzing owner/repo" */
    int            batch;
    int            result;
} BatchTask;

/* Concurrency for a batch: -j value, or auto (4 with a token, 1 without). */
int batch_parallelism(const Options *opts, const Config *cfg);
/* Runs all tasks (par<=1 = serial) and returns the OR of their results. */
int run_batch_tasks(BatchTask *tasks, int n, int par, int quiet);

int cmd_analyze(Options *opts);
int cmd_config(const Options *opts);
int cmd_watch(Options *opts);
int cmd_usage(const Options *opts);

#endif /* GS_CMD_H */
