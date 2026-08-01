#ifndef GITSIGHT_USAGE_H
#define GITSIGHT_USAGE_H

#include "error.h"
#include <stddef.h>

/* Local LLM usage log, decoupled from the rest of GitSight. Records are
 * persisted as JSON next to the config file (or $GS_USAGE_FILE), aggregated
 * by LLM provider or by day, and rendered by the chart module. */

#define USAGE_FILE          "usage.json"
#define USAGE_MAX_RECORDS   5000
#define USAGE_MAX_GROUPS    32

typedef struct {
    long long timestamp;        /* unix seconds */
    char llm[64];               /* provider name */
    char model[128];
    long long prompt_tokens;
    long long completion_tokens;
    long long total_tokens;
} UsageRecord;

typedef struct {
    char name[64];
    long long calls;
    long long prompt_tokens;
    long long completion_tokens;
    long long total_tokens;
} UsageGroup;

typedef struct {
    long long day_start;        /* unix seconds at 00:00 UTC */
    long long calls;
    long long prompt_tokens;
    long long completion_tokens;
    long long total_tokens;
} UsageDay;

/* Point the module at the active usage log. main.c calls this once with the
 * path derived from the config file so LLM calls and `gs usage` agree on the
 * file. Passing NULL/"" restores automatic resolution. */
void usage_set_file(const char *path);

/* Resolve the usage file path given a config file path: the file sits in the
 * same directory as the config (dir(config) + "/usage.json"). If config_path
 * is empty, falls back to the default config directory. */
void usage_path_from_config(const char *config_path, char *buf, size_t len);

/* Resolve the effective usage file path (set file, then $GS_USAGE_FILE, then
 * the default config directory). */
void usage_file_path(char *buf, size_t len);

/* Append one record. `hint` is a usage file path override (usually NULL,
 * meaning "the active file"). */
ErrorCode usage_record(const char *hint, const UsageRecord *rec);

/* Load all records. Returns the count, or -1 on error. Caller owns *out and
 * must free it with usage_free. A missing/empty file yields 0. */
long usage_load(const char *hint, UsageRecord **out);

void usage_free(UsageRecord *recs);

/* Remove all records (writes an empty log). */
ErrorCode usage_clear(const char *hint);

/* Sum across all records; any output pointer may be NULL. */
void usage_totals(const UsageRecord *recs, long n,
                  long long *calls, long long *prompt_tokens,
                  long long *completion_tokens, long long *total_tokens);

/* Group records by LLM provider into `out` (max_out entries). Returns the
 * number of groups written. */
int usage_by_llm(const UsageRecord *recs, long n, UsageGroup *out, int max_out);

/* Bucket records into per-day totals for the last `days` days (UTC, oldest
 * first). `out` must hold `days` entries; empty days are zeroed. */
void usage_by_day(const UsageRecord *recs, long n, int days, UsageDay *out);

#endif
