#ifndef GITSIGHT_MODELS_H
#define GITSIGHT_MODELS_H

#include "container.h"

#define MAX_TITLE_LEN   512
#define MAX_MSG_LEN     2048
#define MAX_URL_LEN     512
#define MAX_AUTHOR_LEN   256
#define MAX_LABELS_LEN   512
#define MAX_SHA_LEN      64
#define MAX_DATE_LEN     32
#define MAX_STATE_LEN    16
#define MAX_SUMMARY_LEN  4096
#define MAX_TAG_LEN      128

#define TOP_AUTHOR_LEN  (MAX_AUTHOR_LEN + 64)

typedef struct {
    int    number;
    char   title[MAX_TITLE_LEN];
    char   state[MAX_STATE_LEN];
    char   created_at[MAX_DATE_LEN];
    char   merged_at[MAX_DATE_LEN];
    char   author[MAX_AUTHOR_LEN];
    char   labels[MAX_LABELS_LEN];
    char   html_url[MAX_URL_LEN];
} PR;

typedef struct {
    char   sha[MAX_SHA_LEN];
    char   message[MAX_MSG_LEN];
    char   author[MAX_AUTHOR_LEN];
    char   date[MAX_DATE_LEN];
    char   html_url[MAX_URL_LEN];
} Commit;

typedef struct {
    int    number;
    char   title[MAX_TITLE_LEN];
    char   state[MAX_STATE_LEN];
    char   created_at[MAX_DATE_LEN];
    char   closed_at[MAX_DATE_LEN];
    char   author[MAX_AUTHOR_LEN];
    char   labels[MAX_LABELS_LEN];
    char   html_url[MAX_URL_LEN];
} Issue;

typedef struct {
    char   tag_name[MAX_TAG_LEN];
    char   name[MAX_TITLE_LEN];
    char   author[MAX_AUTHOR_LEN];
    char   published_at[MAX_DATE_LEN];
    char   html_url[MAX_URL_LEN];
    char   body[MAX_MSG_LEN];
} Release;

typedef struct {
    char   login[MAX_AUTHOR_LEN];
    int    contributions;
} Contributor;

typedef struct {
    char   description[MAX_MSG_LEN];
    char   language[64];
    char   license[64];
    char   default_branch[64];
    char   created_at[MAX_DATE_LEN];
    char   pushed_at[MAX_DATE_LEN];
    long   stars;
    long   forks;
    long   watchers;
    long   open_issues;
    int    has_meta;
} RepoMeta;

typedef struct {
    char   owner[128];
    char   repo[128];
    char   period[32];
    RepoMeta meta;
    int    pr_total;
    int    pr_open;
    int    pr_closed;
    int    pr_merged;
    char   top_labels[MAX_LABELS_LEN];
    int    commit_total;
    int    author_total;
    char   top_author[TOP_AUTHOR_LEN];
    int    issue_total;
    int    issue_open;
    int    issue_closed;
    int    release_total;
    char   latest_release[MAX_TAG_LEN];
    int    contributor_total;
    char   top_contributor[TOP_AUTHOR_LEN];
    char   top_contributor_list[1024];
    double pr_avg_merge_hours;
    int    pr_merged_count;
    double issue_avg_close_hours;
    int    issue_closed_count;
    int    activity_count;
    char   activity_labels[60][16];
    int    activity_values[60];
    int    activity_max;
    int    weekday_values[7];
    int    busiest_weekday;
    char   summary[MAX_SUMMARY_LEN];
} Report;

PR       *pr_create(void);
void      pr_free(void *p);
Commit   *commit_create(void);
void      commit_free(void *c);
Issue    *issue_create(void);
void      issue_free(void *i);
Release  *release_create(void);
void      release_free(void *r);
Contributor *contributor_create(void);
void      contributor_free(void *c);

#endif
