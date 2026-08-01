#include "gitsight/export/exporter.h"
#include "gitsight/color.h"
#include <string.h>

static ErrorCode md_write_report(FILE *fp, const Report *report,
                                 const Vector *prs, const Vector *commits,
                                 const Vector *issues, const Vector *releases,
                                 const FormatOptions *opts) {
    int uc = opts ? opts->color_active : 0;
    int flags = opts ? opts->flags : ~0;
    int limit = opts && opts->item_limit > 0 ? opts->item_limit : 10;
    const char *B = uc ? CLR_BOLD : "";
    const char *C = uc ? CLR_CYAN  : "";
    const char *G = uc ? CLR_GREEN : "";
    const char *Y = uc ? CLR_YELLOW: "";
    const char *R = uc ? CLR_RESET : "";

    fprintf(fp, "# GitSight Report\n\n");
    fprintf(fp, "**Repository**: %s%s%s/%s%s%s\n", B, report->owner, R, B, report->repo, R);
    fprintf(fp, "**Period**:     %s\n\n", report->period);
    fprintf(fp, "---\n\n");

    if (report->meta.has_meta) {
        fprintf(fp, "## Repository\n\n");
        fprintf(fp, "| Metric         | Value |\n");
        fprintf(fp, "|----------------|-------|\n");
        fprintf(fp, "| Stars          | %s%ld%s |\n", C, report->meta.stars, R);
        fprintf(fp, "| Forks          | %s%ld%s |\n", C, report->meta.forks, R);
        if (report->meta.language[0])
            fprintf(fp, "| Language       | %s |\n", report->meta.language);
        if (report->meta.license[0])
            fprintf(fp, "| License        | %s |\n", report->meta.license);
        if (report->meta.default_branch[0])
            fprintf(fp, "| Default branch | %s |\n", report->meta.default_branch);
        if (report->meta.description[0])
            fprintf(fp, "| Description    | %s |\n", report->meta.description);
        fprintf(fp, "\n");
    }

    if (flags & FMT_SHOW_PRS) {
    fprintf(fp, "## [PR] Pull Requests\n\n");
    fprintf(fp, "| Metric | Value |\n");
    fprintf(fp, "|--------|-------|\n");
    fprintf(fp, "| Total  | %s%d%s |\n", C, report->pr_total, R);
    fprintf(fp, "| Merged | %s%d%s |\n", G, report->pr_merged, R);
    fprintf(fp, "| Open   | %s%d%s |\n", Y, report->pr_open, R);
    fprintf(fp, "| Closed | %d |\n", report->pr_closed - report->pr_merged);
    if (report->pr_merged_count > 0) {
        fprintf(fp, "| Avg merge time | %.1f hours |\n", report->pr_avg_merge_hours);
    }
    fprintf(fp, "\n");

    if (limit > 0 && prs) {
        size_t n = vector_len(prs);
        if (n > (size_t)limit) n = (size_t)limit;
        if (n > 0) {
            fprintf(fp, "**Recent pull requests:**\n\n");
            fprintf(fp, "| # | Title | State | Author |\n");
            fprintf(fp, "|---|-------|-------|--------|\n");
            for (size_t i = 0; i < n; i++) {
                PR *pr = (PR *)vector_get(prs, i);
                fprintf(fp, "| [%d](%s) | %s | %s | %s |\n",
                        pr->number, pr->html_url[0] ? pr->html_url : "#",
                        pr->title, pr->state, pr->author);
            }
            fprintf(fp, "\n");
        }
    }
    }

    if (flags & FMT_SHOW_COMMITS) {
    fprintf(fp, "## [CM] Commits\n\n");
    fprintf(fp, "| Metric      | Value |\n");
    fprintf(fp, "|-------------|-------|\n");
    fprintf(fp, "| Total       | %s%d%s |\n", C, report->commit_total, R);
    fprintf(fp, "| Authors     | %d |\n", report->author_total);
    if (report->top_author[0]) {
        fprintf(fp, "| Top author  | %s |\n", report->top_author);
    }
    fprintf(fp, "\n");

    if (report->activity_count > 1 && report->activity_max > 0) {
        fprintf(fp, "**Commit activity (last %d buckets):**\n\n", report->activity_count);
        fprintf(fp, "```\n");
        for (int i = report->activity_count - 1; i >= 0; i--) {
            int bar = (report->activity_values[i] * 40) / report->activity_max;
            if (bar < 1 && report->activity_values[i] > 0) bar = 1;
            fprintf(fp, "%s |", report->activity_labels[i]);
            for (int b = 0; b < bar; b++) fputc('#', fp);
            fprintf(fp, " %d\n", report->activity_values[i]);
        }
        fprintf(fp, "```\n\n");
    }

    if (limit > 0 && commits) {
        size_t n = vector_len(commits);
        if (n > (size_t)limit) n = (size_t)limit;
        if (n > 0) {
            fprintf(fp, "**Recent commits:**\n\n");
            fprintf(fp, "| Sha | Message | Author | Date |\n");
            fprintf(fp, "|-----|---------|--------|------|\n");
            for (size_t i = 0; i < n; i++) {
                Commit *c = (Commit *)vector_get(commits, i);
                fprintf(fp, "| [`%s`](%s) | %s | %s | %s |\n",
                        c->sha, c->html_url[0] ? c->html_url : "#",
                        c->message, c->author, c->date);
            }
            fprintf(fp, "\n");
        }
    }
    }

    if (flags & FMT_SHOW_ISSUES) {
    fprintf(fp, "## [IS] Issues\n\n");
    fprintf(fp, "| Metric | Value |\n");
    fprintf(fp, "|--------|-------|\n");
    fprintf(fp, "| Total  | %s%d%s |\n", C, report->issue_total, R);
    fprintf(fp, "| Open   | %s%d%s |\n", Y, report->issue_open, R);
    fprintf(fp, "| Closed | %s%d%s |\n", G, report->issue_closed, R);
    if (report->issue_closed_count > 0) {
        fprintf(fp, "| Avg close time | %.1f hours |\n", report->issue_avg_close_hours);
    }
    fprintf(fp, "\n");

    if (limit > 0 && issues) {
        size_t n = vector_len(issues);
        if (n > (size_t)limit) n = (size_t)limit;
        if (n > 0) {
            fprintf(fp, "**Recent issues:**\n\n");
            fprintf(fp, "| # | Title | State | Author |\n");
            fprintf(fp, "|---|-------|-------|--------|\n");
            for (size_t i = 0; i < n; i++) {
                Issue *iss = (Issue *)vector_get(issues, i);
                fprintf(fp, "| [%d](%s) | %s | %s | %s |\n",
                        iss->number, iss->html_url[0] ? iss->html_url : "#",
                        iss->title, iss->state, iss->author);
            }
            fprintf(fp, "\n");
        }
    }
    }

    if ((flags & FMT_SHOW_LABELS) && report->top_labels[0]) {
        fprintf(fp, "**Top labels**: %s\n\n", report->top_labels);
    }

    if ((flags & FMT_SHOW_RELEASES) && report->release_total > 0) {
    fprintf(fp, "## [RL] Releases\n\n");
    fprintf(fp, "| Metric      | Value |\n");
    fprintf(fp, "|-------------|-------|\n");
    fprintf(fp, "| Total       | %s%d%s |\n", C, report->release_total, R);
    if (report->latest_release[0]) {
        fprintf(fp, "| Latest      | %s |\n", report->latest_release);
    }
    fprintf(fp, "\n");

    if (limit > 0 && releases) {
        size_t n = vector_len(releases);
        if (n > (size_t)limit) n = (size_t)limit;
        if (n > 0) {
            fprintf(fp, "**Recent releases:**\n\n");
            fprintf(fp, "| Tag | Name | Author | Date |\n");
            fprintf(fp, "|-----|------|--------|------|\n");
            for (size_t i = 0; i < n; i++) {
                Release *rel = (Release *)vector_get(releases, i);
                fprintf(fp, "| [%s](%s) | %s | %s | %s |\n",
                        rel->tag_name, rel->html_url[0] ? rel->html_url : "#",
                        rel->name[0] ? rel->name : rel->tag_name,
                        rel->author, rel->published_at);
            }
            fprintf(fp, "\n");
        }
    }
    }

    if ((flags & FMT_SHOW_CONTRIBUTORS) && report->contributor_total > 0) {
    fprintf(fp, "## [CT] Contributors\n\n");
    fprintf(fp, "| Metric      | Value |\n");
    fprintf(fp, "|-------------|-------|\n");
    fprintf(fp, "| Total       | %s%d%s |\n", C, report->contributor_total, R);
    if (report->top_contributor[0]) {
        fprintf(fp, "| Top         | %s |\n", report->top_contributor);
    }
    if (report->top_contributor_list[0]) {
        fprintf(fp, "| Top list    | %s |\n", report->top_contributor_list);
    }
    fprintf(fp, "\n");
    }

    if (flags & FMT_SHOW_SUMMARY) {
    fprintf(fp, "## [*] Summary\n\n");
    fprintf(fp, "%s\n", report->summary);
    }
    fprintf(fp, "\n---\n");
    fprintf(fp, "*Generated by [GitSight](https://github.com/user/gitsight)*\n");

    return GS_OK;
}

const Exporter MD_EXPORTER = {
    .write_report = md_write_report
};
