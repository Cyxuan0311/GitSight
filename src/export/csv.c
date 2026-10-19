/* CSV export — block-partitioned tidy data: one `#section` marker per table,
 * each with its own header row. All fetched rows are exported (no
 * item_limit truncation) so the output is suitable for pandas/Excel. */
#include "gitsight/export/exporter.h"
#include <string.h>

static void csv_field(FILE *fp, const char *s) {
    int needs = 0;
    for (const char *p = s; *p; p++)
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { needs = 1; break; }
    if (!needs) {
        fputs(s, fp);
        return;
    }
    fputc('"', fp);
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\n' || *p == '\r') fputc('"', fp);
        if (*p == '\n' || *p == '\r') fputc(' ', fp);
        else fputc(*p, fp);
    }
    fputc('"', fp);
}

static void csv_row(FILE *fp, const char *a, const char *b, const char *c,
                    const char *d, const char *e, const char *f,
                    const char *g, const char *h) {
    const char *cols[8] = { a, b, c, d, e, f, g, h };
    for (int i = 0; i < 8 && cols[i]; i++) {
        if (i) fputc(',', fp);
        csv_field(fp, cols[i]);
    }
    fputc('\n', fp);
}

static ErrorCode csv_write_report(FILE *fp, const Report *report,
                                  const Vector *prs, const Vector *commits,
                                  const Vector *issues, const Vector *releases,
                                  const FormatOptions *opts) {
    int flags = opts ? opts->flags : ~0;
    char num[32];

    fputs("# metrics\nkey,value\n", fp);
    csv_row(fp, "repository", report->owner, NULL, NULL, NULL, NULL, NULL, NULL);
    csv_row(fp, "name", report->repo, NULL, NULL, NULL, NULL, NULL, NULL);
    csv_row(fp, "period", report->period, NULL, NULL, NULL, NULL, NULL, NULL);
    if (report->branch[0]) csv_row(fp, "branch", report->branch, NULL, NULL, NULL, NULL, NULL, NULL);
    if (report->meta.has_meta) {
        snprintf(num, sizeof(num), "%ld", report->meta.stars);
        csv_row(fp, "stars", num, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(num, sizeof(num), "%ld", report->meta.forks);
        csv_row(fp, "forks", num, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->meta.language[0]) csv_row(fp, "language", report->meta.language, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->meta.license[0])  csv_row(fp, "license", report->meta.license, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->meta.default_branch[0])
            csv_row(fp, "default_branch", report->meta.default_branch, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    if (flags & FMT_SHOW_PRS) {
        snprintf(num, sizeof(num), "%d", report->pr_total);   csv_row(fp, "pr_total", num, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(num, sizeof(num), "%d", report->pr_merged);  csv_row(fp, "pr_merged", num, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(num, sizeof(num), "%d", report->pr_open);    csv_row(fp, "pr_open", num, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(num, sizeof(num), "%d", report->pr_closed - report->pr_merged);
        csv_row(fp, "pr_closed", num, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->pr_merged_count > 0) {
            snprintf(num, sizeof(num), "%.1f", report->pr_avg_merge_hours);
            csv_row(fp, "pr_avg_merge_hours", num, NULL, NULL, NULL, NULL, NULL, NULL);
        }
    }
    if (flags & FMT_SHOW_COMMITS) {
        snprintf(num, sizeof(num), "%d", report->commit_total); csv_row(fp, "commit_total", num, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(num, sizeof(num), "%d", report->author_total); csv_row(fp, "author_total", num, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->top_author[0]) csv_row(fp, "top_author", report->top_author, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    if (flags & FMT_SHOW_ISSUES) {
        snprintf(num, sizeof(num), "%d", report->issue_total);  csv_row(fp, "issue_total", num, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(num, sizeof(num), "%d", report->issue_open);   csv_row(fp, "issue_open", num, NULL, NULL, NULL, NULL, NULL, NULL);
        snprintf(num, sizeof(num), "%d", report->issue_closed); csv_row(fp, "issue_closed", num, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->issue_closed_count > 0) {
            snprintf(num, sizeof(num), "%.1f", report->issue_avg_close_hours);
            csv_row(fp, "issue_avg_close_hours", num, NULL, NULL, NULL, NULL, NULL, NULL);
        }
    }
    if (flags & FMT_SHOW_RELEASES) {
        snprintf(num, sizeof(num), "%d", report->release_total); csv_row(fp, "release_total", num, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->latest_release[0])
            csv_row(fp, "latest_release", report->latest_release, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    if (flags & FMT_SHOW_CONTRIBUTORS) {
        snprintf(num, sizeof(num), "%d", report->contributor_total);
        csv_row(fp, "contributor_total", num, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->top_contributor[0])
            csv_row(fp, "top_contributor", report->top_contributor, NULL, NULL, NULL, NULL, NULL, NULL);
        if (report->top_contributor_list[0])
            csv_row(fp, "top_contributor_list", report->top_contributor_list, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    if (flags & FMT_SHOW_SUMMARY) {
        csv_row(fp, "summary", report->summary, NULL, NULL, NULL, NULL, NULL, NULL);
    }

    if ((flags & FMT_SHOW_PRS) && prs && vector_len(prs) > 0) {
        fputs("\n# pull_requests\nnumber,title,state,author,created_at,merged_at,labels,url\n", fp);
        size_t n = vector_len(prs);
        for (size_t i = 0; i < n; i++) {
            PR *pr = (PR *)vector_get(prs, i);
            snprintf(num, sizeof(num), "%d", pr->number);
            csv_row(fp, num, pr->title, pr->state, pr->author, pr->created_at,
                    pr->merged_at, pr->labels, pr->html_url);
        }
    }

    if ((flags & FMT_SHOW_COMMITS) && commits && vector_len(commits) > 0) {
        fputs("\n# commits\nsha,author,date,message,url\n", fp);
        size_t n = vector_len(commits);
        for (size_t i = 0; i < n; i++) {
            Commit *c = (Commit *)vector_get(commits, i);
            csv_row(fp, c->sha, c->author, c->date, c->message, c->html_url, NULL, NULL, NULL);
        }
    }

    if ((flags & FMT_SHOW_ISSUES) && issues && vector_len(issues) > 0) {
        fputs("\n# issues\nnumber,title,state,author,created_at,closed_at,labels,url\n", fp);
        size_t n = vector_len(issues);
        for (size_t i = 0; i < n; i++) {
            Issue *iss = (Issue *)vector_get(issues, i);
            snprintf(num, sizeof(num), "%d", iss->number);
            csv_row(fp, num, iss->title, iss->state, iss->author, iss->created_at,
                    iss->closed_at, iss->labels, iss->html_url);
        }
    }

    if ((flags & FMT_SHOW_RELEASES) && releases && vector_len(releases) > 0) {
        fputs("\n# releases\ntag,name,author,published_at,url\n", fp);
        size_t n = vector_len(releases);
        for (size_t i = 0; i < n; i++) {
            Release *rel = (Release *)vector_get(releases, i);
            csv_row(fp, rel->tag_name, rel->name[0] ? rel->name : rel->tag_name,
                    rel->author, rel->published_at, rel->html_url, NULL, NULL, NULL);
        }
    }

    if ((flags & FMT_SHOW_LANGS) && report->lang_count > 0) {
        long long total = 0;
        for (int i = 0; i < report->lang_count; i++) total += report->lang_bytes[i];
        fputs("\n# languages\nname,bytes,percent\n", fp);
        for (int i = 0; i < report->lang_count; i++) {
            char b[32], p[32];
            snprintf(b, sizeof(b), "%lld", report->lang_bytes[i]);
            snprintf(p, sizeof(p), "%.1f", total > 0 ? 100.0 * report->lang_bytes[i] / total : 0.0);
            csv_row(fp, report->lang_names[i], b, p, NULL, NULL, NULL, NULL, NULL);
        }
    }

    return GS_OK;
}

const Exporter CSV_EXPORTER = {
    .write_report = csv_write_report
};
