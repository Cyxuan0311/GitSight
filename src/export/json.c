#include "gitsight/export/exporter.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>

static void add_pr(cJSON *arr, const PR *pr) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "number", pr->number);
    cJSON_AddStringToObject(o, "title", pr->title);
    cJSON_AddStringToObject(o, "state", pr->state);
    cJSON_AddStringToObject(o, "created_at", pr->created_at);
    cJSON_AddStringToObject(o, "merged_at", pr->merged_at);
    cJSON_AddStringToObject(o, "author", pr->author);
    cJSON_AddStringToObject(o, "labels", pr->labels);
    cJSON_AddStringToObject(o, "html_url", pr->html_url);
    cJSON_AddItemToArray(arr, o);
}

static void add_commit(cJSON *arr, const Commit *c) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "sha", c->sha);
    cJSON_AddStringToObject(o, "message", c->message);
    cJSON_AddStringToObject(o, "author", c->author);
    cJSON_AddStringToObject(o, "date", c->date);
    cJSON_AddStringToObject(o, "html_url", c->html_url);
    cJSON_AddItemToArray(arr, o);
}

static void add_issue(cJSON *arr, const Issue *iss) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "number", iss->number);
    cJSON_AddStringToObject(o, "title", iss->title);
    cJSON_AddStringToObject(o, "state", iss->state);
    cJSON_AddStringToObject(o, "created_at", iss->created_at);
    cJSON_AddStringToObject(o, "closed_at", iss->closed_at);
    cJSON_AddStringToObject(o, "author", iss->author);
    cJSON_AddStringToObject(o, "labels", iss->labels);
    cJSON_AddStringToObject(o, "html_url", iss->html_url);
    cJSON_AddItemToArray(arr, o);
}

static void add_release(cJSON *arr, const Release *rel) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "tag_name", rel->tag_name);
    cJSON_AddStringToObject(o, "name", rel->name);
    cJSON_AddStringToObject(o, "author", rel->author);
    cJSON_AddStringToObject(o, "published_at", rel->published_at);
    cJSON_AddStringToObject(o, "html_url", rel->html_url);
    cJSON_AddStringToObject(o, "body", rel->body);
    cJSON_AddItemToArray(arr, o);
}

static ErrorCode json_write_report(FILE *fp, const Report *report,
                                   const Vector *prs, const Vector *commits,
                                   const Vector *issues, const Vector *releases,
                                   const FormatOptions *opts) {
    (void)opts;

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "owner", report->owner);
    cJSON_AddStringToObject(root, "repo", report->repo);
    cJSON_AddStringToObject(root, "period", report->period);
    /* Both are numeric so a client can relabel the window and the weekdays in
     * the reader's own language; the strings above stay for the exporters. */
    cJSON_AddNumberToObject(root, "period_days", report->period_days);

    if (report->meta.has_meta) {
        cJSON *meta = cJSON_CreateObject();
        cJSON_AddNumberToObject(meta, "stars", report->meta.stars);
        cJSON_AddNumberToObject(meta, "forks", report->meta.forks);
        cJSON_AddNumberToObject(meta, "watchers", report->meta.watchers);
        cJSON_AddNumberToObject(meta, "open_issues", report->meta.open_issues);
        cJSON_AddStringToObject(meta, "language", report->meta.language);
        cJSON_AddStringToObject(meta, "license", report->meta.license);
        cJSON_AddStringToObject(meta, "default_branch", report->meta.default_branch);
        cJSON_AddStringToObject(meta, "branch", report->branch);
        cJSON_AddStringToObject(meta, "description", report->meta.description);
        cJSON_AddStringToObject(meta, "created_at", report->meta.created_at);
        cJSON_AddStringToObject(meta, "pushed_at", report->meta.pushed_at);
        cJSON_AddItemToObject(root, "repository", meta);
    }

    cJSON *prs_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(prs_obj, "total", report->pr_total);
    cJSON_AddNumberToObject(prs_obj, "open", report->pr_open);
    cJSON_AddNumberToObject(prs_obj, "closed", report->pr_closed);
    cJSON_AddNumberToObject(prs_obj, "merged", report->pr_merged);
    if (report->pr_merged_count > 0) {
        cJSON_AddNumberToObject(prs_obj, "avg_merge_hours", report->pr_avg_merge_hours);
        cJSON_AddNumberToObject(prs_obj, "merged_count", report->pr_merged_count);
    }
    if (prs) {
        cJSON *items = cJSON_AddArrayToObject(prs_obj, "items");
        for (size_t i = 0; i < vector_len(prs); i++)
            add_pr(items, (PR *)vector_get(prs, i));
    }
    cJSON_AddItemToObject(root, "pull_requests", prs_obj);

    cJSON *cm = cJSON_CreateObject();
    cJSON_AddNumberToObject(cm, "total", report->commit_total);
    cJSON_AddNumberToObject(cm, "authors", report->author_total);
    cJSON_AddStringToObject(cm, "top_author", report->top_author);
    if (report->activity_count > 0) {
        cJSON *act = cJSON_AddArrayToObject(cm, "activity");
        for (int i = 0; i < report->activity_count; i++) {
            cJSON *b = cJSON_CreateObject();
            cJSON_AddStringToObject(b, "label", report->activity_labels[i]);
            cJSON_AddNumberToObject(b, "commits", report->activity_values[i]);
            cJSON_AddItemToArray(act, b);
        }
        cJSON *wd = cJSON_AddArrayToObject(cm, "weekday");
        const char *wd_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
for (int i = 0; i < 7; i++) {
            cJSON *b = cJSON_CreateObject();
            cJSON_AddStringToObject(b, "day", wd_names[i]);
            cJSON_AddNumberToObject(b, "index", i);
            cJSON_AddNumberToObject(b, "commits", report->weekday_values[i]);
            cJSON_AddItemToArray(wd, b);
        }
    }
    if (commits) {
        cJSON *items = cJSON_AddArrayToObject(cm, "items");
        for (size_t i = 0; i < vector_len(commits); i++)
            add_commit(items, (Commit *)vector_get(commits, i));
    }
    cJSON_AddItemToObject(root, "commits", cm);

    cJSON *is = cJSON_CreateObject();
    cJSON_AddNumberToObject(is, "total", report->issue_total);
    cJSON_AddNumberToObject(is, "open", report->issue_open);
    cJSON_AddNumberToObject(is, "closed", report->issue_closed);
    if (report->issue_closed_count > 0) {
        cJSON_AddNumberToObject(is, "avg_close_hours", report->issue_avg_close_hours);
        cJSON_AddNumberToObject(is, "closed_count", report->issue_closed_count);
    }
    if (issues) {
        cJSON *items = cJSON_AddArrayToObject(is, "items");
        for (size_t i = 0; i < vector_len(issues); i++)
            add_issue(items, (Issue *)vector_get(issues, i));
    }
    cJSON_AddItemToObject(root, "issues", is);

    cJSON *rl = cJSON_CreateObject();
    cJSON_AddNumberToObject(rl, "total", report->release_total);
    if (report->latest_release[0])
        cJSON_AddStringToObject(rl, "latest", report->latest_release);
    if (releases) {
        cJSON *items = cJSON_AddArrayToObject(rl, "items");
        for (size_t i = 0; i < vector_len(releases); i++)
            add_release(items, (Release *)vector_get(releases, i));
    }
    cJSON_AddItemToObject(root, "releases", rl);

    cJSON *ct = cJSON_CreateObject();
    cJSON_AddNumberToObject(ct, "total", report->contributor_total);
    if (report->top_contributor[0])
        cJSON_AddStringToObject(ct, "top", report->top_contributor);
    if (report->top_contributor_list[0])
        cJSON_AddStringToObject(ct, "top_list", report->top_contributor_list);
    cJSON_AddItemToObject(root, "contributors", ct);

    if (report->lang_count > 0) {
        long long total = 0;
        for (int i = 0; i < report->lang_count; i++) total += report->lang_bytes[i];
        cJSON *lg = cJSON_AddArrayToObject(root, "languages");
        for (int i = 0; i < report->lang_count; i++) {
            cJSON *b = cJSON_CreateObject();
            cJSON_AddStringToObject(b, "name", report->lang_names[i]);
            cJSON_AddNumberToObject(b, "bytes", (double)report->lang_bytes[i]);
            cJSON_AddNumberToObject(b, "percent",
                total > 0 ? 100.0 * report->lang_bytes[i] / total : 0.0);
            cJSON_AddItemToArray(lg, b);
        }
    }

    cJSON_AddStringToObject(root, "top_labels", report->top_labels);
    cJSON_AddStringToObject(root, "summary", report->summary);

    char *str = cJSON_Print(root);
    if (!str) { cJSON_Delete(root); return ERR_MEMORY; }

    fprintf(fp, "%s\n", str);
    free(str);
    cJSON_Delete(root);
    return GS_OK;
}

const Exporter JSON_EXPORTER = {
    .write_report = json_write_report
};
