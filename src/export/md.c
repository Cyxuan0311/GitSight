#include "gitsight/export/exporter.h"
#include "gitsight/color.h"
#include "gitsight/i18n.h"
#include <stdlib.h>
#include <string.h>

static int md_prev_punct(const char *s, size_t i) {
    while (i && s[i - 1] == ' ') i--;
    if (!i) return 0;
    char c = s[i - 1];
    return c == '.' || c == '!' || c == '?' || c == ')';
}

/* True when buf[0..n) (up to the current line start) is just "N." / "N)" —
 * used so "**Title**" after a list marker stays on the marker's line. */
static int md_is_marker_line(const char *buf, size_t n) {
    size_t i = 0;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;
    if (i >= n || buf[i] < '1' || buf[i] > '9') return 0;
    while (i < n && buf[i] >= '0' && buf[i] <= '9') i++;
    if (i < n && (buf[i] == '.' || buf[i] == ')')) i++;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;
    return i == n;
}

size_t md_prettify_text(const char *in, char *out, size_t outcap) {
    if (!outcap) return 0;
    size_t len = strlen(in);
    size_t b = 0, ls = 0;   /* ls = start of current output line in out */
    for (size_t i = 0; i < len && b + 1 < outcap; i++) {
        char c = in[i];
        if (c == '\r') continue;
        if (c == '\n') ls = b + 1;
        int brk = 0;
        if (c == '*' && i + 1 < len && in[i + 1] == '*') {
            brk = md_prev_punct(in, i);
        } else if (c >= '1' && c <= '9') {
            size_t j = i + 1;
            while (j < len && in[j] >= '0' && in[j] <= '9') j++;
            if (j + 1 < len && in[j] == '.' && in[j + 1] == ' ')
                brk = md_prev_punct(in, i);
        }
        if (brk && md_is_marker_line(out + ls, b - ls)) brk = 0;
        if (brk && b && out[b - 1] != '\n') {
            out[b++] = '\n';
            ls = b;
            if (b + 1 >= outcap) break;
        }
        out[b++] = c;
    }
    out[b] = '\0';
    return b;
}

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

    fprintf(fp, "# %s\n\n", gs_t("GitSight Report"));
    fprintf(fp, "**%s**: %s%s%s/%s%s%s\n", gs_t("Repository"), B, report->owner, R, B, report->repo, R);
    fprintf(fp, "**%s**:     %s\n\n", gs_t("Period"), report->period);
    fprintf(fp, "---\n\n");

    if (report->meta.has_meta) {
        fprintf(fp, "## %s\n\n", gs_t("Repository"));
        fprintf(fp, "| %-15s | %-5s |\n", gs_t("Metric"), gs_t("Value"));
        fprintf(fp, "|----------------|-------|\n");
        fprintf(fp, "| %-15s | %s%ld%s |\n", gs_t("Stars"), C, report->meta.stars, R);
        fprintf(fp, "| %-15s | %s%ld%s |\n", gs_t("Forks"), C, report->meta.forks, R);
        if (report->meta.language[0])
            fprintf(fp, "| %-15s | %s |\n", gs_t("Language"), report->meta.language);
        if (report->meta.license[0])
            fprintf(fp, "| %-15s | %s |\n", gs_t("License"), report->meta.license);
        if (report->meta.default_branch[0])
            fprintf(fp, "| %-15s | %s |\n", gs_t("Default branch"), report->meta.default_branch);
        if (report->branch[0])
            fprintf(fp, "| %-15s | %s |\n", gs_t("Branch"), report->branch);
        if (report->meta.description[0])
            fprintf(fp, "| %-15s | %s |\n", gs_t("Description"), report->meta.description);
        fprintf(fp, "\n");
    }

    if (flags & FMT_SHOW_PRS) {
    fprintf(fp, "## %s\n\n", gs_t("[PR] Pull Requests"));
    fprintf(fp, "| %-6s | %-5s |\n", gs_t("Metric"), gs_t("Value"));
    fprintf(fp, "|--------|-------|\n");
    fprintf(fp, "| %-6s | %s%d%s |\n", gs_t("Total"), C, report->pr_total, R);
    fprintf(fp, "| %-6s | %s%d%s |\n", gs_t("Merged"), G, report->pr_merged, R);
    fprintf(fp, "| %-6s | %s%d%s |\n", gs_t("Open"), Y, report->pr_open, R);
    fprintf(fp, "| %-6s | %d |\n", gs_t("Closed"), report->pr_closed - report->pr_merged);
    if (report->pr_merged_count > 0) {
        fprintf(fp, "| %s | %.1f %s |\n", gs_t("Avg merge time"), report->pr_avg_merge_hours, gs_t("hours"));
    }
    fprintf(fp, "\n");

    if (limit > 0 && prs) {
        size_t n = vector_len(prs);
        if (n > (size_t)limit) n = (size_t)limit;
        if (n > 0) {
            fprintf(fp, "**%s**\n\n", gs_t("Recent pull requests:"));
            fprintf(fp, "| # | %s | %s | %s |\n", gs_t("Title"), gs_t("State"), gs_t("Author"));
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
    fprintf(fp, "## %s\n\n", gs_t("[CM] Commits"));
    fprintf(fp, "| %-11s | %-5s |\n", gs_t("Metric"), gs_t("Value"));
    fprintf(fp, "|-------------|-------|\n");
    fprintf(fp, "| %-11s | %s%d%s |\n", gs_t("Total"), C, report->commit_total, R);
    fprintf(fp, "| %-11s | %d |\n", gs_t("Authors"), report->author_total);
    if (report->top_author[0]) {
        fprintf(fp, "| %-11s | %s |\n", gs_t("Top author"), report->top_author);
    }
    fprintf(fp, "\n");

    if (report->activity_count > 1 && report->activity_max > 0) {
        char abuf[128];
        snprintf(abuf, sizeof(abuf), gs_t("Commit activity (last %d buckets):"), report->activity_count);
        fprintf(fp, "**%s**\n\n", abuf);
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
            fprintf(fp, "**%s**\n\n", gs_t("Recent commits:"));
            fprintf(fp, "| Sha | %s | %s | %s |\n", gs_t("Message"), gs_t("Author"), gs_t("Date"));
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
    fprintf(fp, "## %s\n\n", gs_t("[IS] Issues"));
    fprintf(fp, "| %-6s | %-5s |\n", gs_t("Metric"), gs_t("Value"));
    fprintf(fp, "|--------|-------|\n");
    fprintf(fp, "| %-6s | %s%d%s |\n", gs_t("Total"), C, report->issue_total, R);
    fprintf(fp, "| %-6s | %s%d%s |\n", gs_t("Open"), Y, report->issue_open, R);
    fprintf(fp, "| %-6s | %s%d%s |\n", gs_t("Closed"), G, report->issue_closed, R);
    if (report->issue_closed_count > 0) {
        fprintf(fp, "| %s | %.1f %s |\n", gs_t("Avg close time"), report->issue_avg_close_hours, gs_t("hours"));
    }
    fprintf(fp, "\n");

    if (limit > 0 && issues) {
        size_t n = vector_len(issues);
        if (n > (size_t)limit) n = (size_t)limit;
        if (n > 0) {
            fprintf(fp, "**%s**\n\n", gs_t("Recent issues:"));
            fprintf(fp, "| # | %s | %s | %s |\n", gs_t("Title"), gs_t("State"), gs_t("Author"));
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
        fprintf(fp, "**%s**: %s\n\n", gs_t("Top labels"), report->top_labels);
    }

    if ((flags & FMT_SHOW_RELEASES) && report->release_total > 0) {
    fprintf(fp, "## %s\n\n", gs_t("[RL] Releases"));
    fprintf(fp, "| %-11s | %-5s |\n", gs_t("Metric"), gs_t("Value"));
    fprintf(fp, "|-------------|-------|\n");
    fprintf(fp, "| %-11s | %s%d%s |\n", gs_t("Total"), C, report->release_total, R);
    if (report->latest_release[0]) {
        fprintf(fp, "| %-11s | %s |\n", gs_t("Latest"), report->latest_release);
    }
    fprintf(fp, "\n");

    if (limit > 0 && releases) {
        size_t n = vector_len(releases);
        if (n > (size_t)limit) n = (size_t)limit;
        if (n > 0) {
            fprintf(fp, "**%s**\n\n", gs_t("Recent releases:"));
            fprintf(fp, "| %s | %s | %s | %s |\n", gs_t("Tag"), gs_t("Name"), gs_t("Author"), gs_t("Date"));
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
    fprintf(fp, "## %s\n\n", gs_t("[CT] Contributors"));
    fprintf(fp, "| %-11s | %-5s |\n", gs_t("Metric"), gs_t("Value"));
    fprintf(fp, "|-------------|-------|\n");
    fprintf(fp, "| %-11s | %s%d%s |\n", gs_t("Total"), C, report->contributor_total, R);
    if (report->top_contributor[0]) {
        fprintf(fp, "| %-11s | %s |\n", gs_t("Top"), report->top_contributor);
    }
    if (report->top_contributor_list[0]) {
        fprintf(fp, "| %-11s | %s |\n", gs_t("Top list"), report->top_contributor_list);
    }
    fprintf(fp, "\n");
    }

    if (flags & FMT_SHOW_SUMMARY) {
    fprintf(fp, "## %s\n\n", gs_t("[*] Summary"));
    size_t slen = strlen(report->summary);
    char *pretty = malloc(slen * 2 + 2);
    if (pretty) {
        md_prettify_text(report->summary, pretty, slen * 2 + 2);
        fprintf(fp, "%s\n", pretty);
        free(pretty);
    } else {
        fprintf(fp, "%s\n", report->summary);
    }
    }
    fprintf(fp, "\n---\n");
    fprintf(fp, "*%s*\n", gs_t("Generated by [GitSight](https://github.com/user/gitsight)"));

    return GS_OK;
}

const Exporter MD_EXPORTER = {
    .write_report = md_write_report
};
