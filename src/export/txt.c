#include "gitsight/export/exporter.h"
#include "gitsight/color.h"
#include "gitsight/i18n.h"
#include <stdlib.h>
#include <string.h>

static void print_fill(FILE *fp, char fill, int width) {
    for (int i = 0; i < width; i++) fputc(fill, fp);
}

static void print_wrapped(FILE *fp, const char *s, int W, const char *indent) {
    int max_line = W - 6;
    const char *p = s;
    while (*p) {
        size_t len = strlen(p);   /* remaining bytes, recomputed per row */
        int i, line_len = 0, last_space = -1;
        for (i = 0; p[i] && line_len < max_line; i++, line_len++) {
            if (p[i] == ' ') last_space = i;
        }
        int break_at;
        if ((size_t)i >= len || p[i] == '\0') {
            break_at = (int)len;
        } else if (last_space > 0 && line_len >= max_line) {
            break_at = last_space;
        } else {
            break_at = i;
            while (break_at > 0 && p[break_at] != ' ') break_at--;
            if (break_at == 0) break_at = i;
        }
        fprintf(fp, "| %s%.*s", indent, break_at, p);
        int written = break_at;
        if (written > max_line) written = max_line;
        int pad = W - (int)strlen(indent) - 5 - written;
        if (pad < 0) pad = 0;
        fprintf(fp, "%*s|\n", pad, "");
        p += break_at;
        while (*p == ' ') p++;
    }
}

/* Wrap a multi-line string: each '\n'-delimited segment is wrapped
 * separately; empty segments render as blank box rows. */
static void print_wrapped_block(FILE *fp, const char *s, int W, const char *indent) {
    const char *p = s;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t seg = nl ? (size_t)(nl - p) : strlen(p);
        char *line = malloc(seg + 1);
        if (line) {
            memcpy(line, p, seg);
            line[seg] = '\0';
            if (line[0]) print_wrapped(fp, line, W, indent);
            else fprintf(fp, "|\n");
            free(line);
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static void item_row(FILE *fp, const char *text, int W) {
    char buf[1024];
    size_t len = strlen(text);
    size_t maxl = (size_t)(W - 6);
    if (len > maxl) {
        int keep = (int)maxl > 3 ? (int)maxl - 3 : 0;
        snprintf(buf, sizeof(buf), "%.*s...", keep, text);
        text = buf;
        len = strlen(buf);
    }
    fprintf(fp, "|   %s", text);
    int pad = W - 6 - (int)len;
    if (pad < 0) pad = 0;
    fprintf(fp, "%*s|\n", pad, "");
}

static ErrorCode txt_write_report(FILE *fp, const Report *report,
                                  const Vector *prs, const Vector *commits,
                                  const Vector *issues, const Vector *releases,
                                  const FormatOptions *opts) {
    int uc = opts ? opts->color_active : 0;
    int flags = opts ? opts->flags : ~0;
    int limit = opts && opts->item_limit > 0 ? opts->item_limit : 10;
    const char *B = uc ? CLR_BOLD  : "";
    const char *C = uc ? CLR_CYAN  : "";
    const char *G = uc ? CLR_GREEN : "";
    const char *Y = uc ? CLR_YELLOW: "";
    const char *R = uc ? CLR_RESET : "";

    int W = opts && opts->table_width > 0 ? opts->table_width : 56;
    char lbl[64];

    if (uc) fprintf(fp, "%s", B);
    fprintf(fp, "+");
    print_fill(fp, '-', W - 2);
    fprintf(fp, "|\n");

    fprintf(fp, "|%s%s%*s%s", B, C, (W - 4) / 2 + 10, gs_t("GitSight Report"), R);
    if (uc) fprintf(fp, "%s", B);
    fprintf(fp, "%*s|\n", (W - 4) / 2 - 10, "");
    if (uc) fprintf(fp, "%s", R);

    fprintf(fp, "|");
    for (int i = 0; i < W - 2; i++) fputc('-', fp);
    fprintf(fp, "|\n");

    snprintf(lbl, sizeof lbl, "%s:", gs_t("Repository"));
    fprintf(fp, "| %-13s%s/%s", lbl, report->owner, report->repo);
    fprintf(fp, "%*s|\n", W - (int)strlen(report->owner) - (int)strlen(report->repo) - 17, "");

    snprintf(lbl, sizeof lbl, "%s:", gs_t("Period"));
    fprintf(fp, "| %-13s%s", lbl, report->period);
    fprintf(fp, "%*s|\n", W - (int)strlen(report->period) - 16, "");

    if (report->branch[0]) {
        snprintf(lbl, sizeof lbl, "%s:", gs_t("Branch"));
        fprintf(fp, "| %-13s%s", lbl, report->branch);
        fprintf(fp, "%*s|\n", W - (int)strlen(report->branch) - 16, "");
    }

    if (report->meta.has_meta) {
        char mbuf[512];
        snprintf(mbuf, sizeof(mbuf), gs_t("%ld stars | %ld forks"), report->meta.stars, report->meta.forks);
        fprintf(fp, "|%s", mbuf);
        int mlen = (int)strlen(mbuf);
        int pad = W - mlen - 3;
        if (pad < 0) pad = 0;
        fprintf(fp, "%*s|\n", pad, "");
        if (report->meta.language[0] || report->meta.license[0]) {
            snprintf(mbuf, sizeof(mbuf), gs_t("Language: %s  License: %s"),
                     report->meta.language[0] ? report->meta.language : "-",
                     report->meta.license[0] ? report->meta.license : "-");
            fprintf(fp, "|%s", mbuf);
            mlen = (int)strlen(mbuf);
            pad = W - mlen - 3;
            if (pad < 0) pad = 0;
            fprintf(fp, "%*s|\n", pad, "");
        }
    }

    fprintf(fp, "|");
    for (int i = 0; i < W - 2; i++) fputc('-', fp);
    fprintf(fp, "|\n");
    fprintf(fp, "|\n");

    if (flags & FMT_SHOW_PRS) {
    fprintf(fp, "| %s\n", gs_t("[PR] Pull Requests"));
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Total"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, C, report->pr_total, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Merged"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, G, report->pr_merged, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Open"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, Y, report->pr_open, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    if (report->pr_merged_count > 0) {
        char tbuf[128];
        snprintf(tbuf, sizeof(tbuf), gs_t("Avg merge: %.1f h"), report->pr_avg_merge_hours);
        fprintf(fp, "|   %s", tbuf);
        int pad = W - 7 - (int)strlen(tbuf);
        if (pad < 0) pad = 0;
        fprintf(fp, "%*s|\n", pad, "");
    }
    if (limit > 0 && prs) {
        size_t n = vector_len(prs);
        if (n > (size_t)limit) n = (size_t)limit;
        for (size_t i = 0; i < n; i++) {
            PR *pr = (PR *)vector_get(prs, i);
            char title[MAX_TITLE_LEN];
            snprintf(title, sizeof(title), "%s", pr->title);
            char row[1024];
            snprintf(row, sizeof(row), "#%d %s [%s]", pr->number, title, pr->state);
            item_row(fp, row, W);
        }
        if (n == 0) fprintf(fp, "|   %s\n", gs_t("(none in period)"));
    }
    fprintf(fp, "|\n");
    }

    if (flags & FMT_SHOW_COMMITS) {
    fprintf(fp, "| %s\n", gs_t("[CM] Commits"));
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Total"));
    fprintf(fp, "|   %-9s%s%5d%s", lbl, C, report->commit_total, R);
    fprintf(fp, "%*s|\n", W - 21, "");
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Authors"));
    fprintf(fp, "|   %-9s%5d", lbl, report->author_total);
    fprintf(fp, "%*s|\n", W - 21, "");
    if (report->top_author[0]) {
        snprintf(lbl, sizeof lbl, "%s:", gs_t("Top"));
        fprintf(fp, "|   %-9s%s", lbl, report->top_author);
        int pad = W - 21 - (int)strlen(report->top_author);
        if (pad < 0) pad = 0;
        fprintf(fp, "%*s|\n", pad, "");
    }
    if (report->activity_count > 1 && report->activity_max > 0) {
        fprintf(fp, "|   %s\n", gs_t("Activity (commits/day):"));
        for (int i = report->activity_count - 1; i >= 0; i--) {
            int bar = (report->activity_values[i] * 30) / report->activity_max;
            if (bar < 1 && report->activity_values[i] > 0) bar = 1;
            char abuf[128];
            int pos = snprintf(abuf, sizeof(abuf), "%s |", report->activity_labels[i]);
            for (int b = 0; b < bar; b++) {
                if (pos < (int)sizeof(abuf) - 1) abuf[pos++] = '#';
            }
            pos += snprintf(abuf + pos, sizeof(abuf) - pos, " %d", report->activity_values[i]);
            fprintf(fp, "|   %s", abuf);
            int pad = W - 6 - (int)strlen(abuf);
            if (pad < 0) pad = 0;
            fprintf(fp, "%*s|\n", pad, "");
        }
    }
    if (limit > 0 && commits) {
        size_t n = vector_len(commits);
        if (n > (size_t)limit) n = (size_t)limit;
        for (size_t i = 0; i < n; i++) {
            Commit *c = (Commit *)vector_get(commits, i);
            char msg[MAX_MSG_LEN];
            snprintf(msg, sizeof(msg), "%s", c->message);
            char row[2400];
            snprintf(row, sizeof(row), "%s %s (%s)", c->sha, msg, c->author);
            item_row(fp, row, W);
        }
        if (n == 0) fprintf(fp, "|   %s\n", gs_t("(none in period)"));
    }
    fprintf(fp, "|\n");
    }

    if (flags & FMT_SHOW_ISSUES) {
    fprintf(fp, "| %s\n", gs_t("[IS] Issues"));
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Total"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, C, report->issue_total, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Open"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, Y, report->issue_open, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Closed"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, G, report->issue_closed, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    if (report->issue_closed_count > 0) {
        char tbuf[128];
        snprintf(tbuf, sizeof(tbuf), gs_t("Avg close: %.1f h"), report->issue_avg_close_hours);
        fprintf(fp, "|   %s", tbuf);
        int pad = W - 7 - (int)strlen(tbuf);
        if (pad < 0) pad = 0;
        fprintf(fp, "%*s|\n", pad, "");
    }
    if (limit > 0 && issues) {
        size_t n = vector_len(issues);
        if (n > (size_t)limit) n = (size_t)limit;
        for (size_t i = 0; i < n; i++) {
            Issue *iss = (Issue *)vector_get(issues, i);
            char title[MAX_TITLE_LEN];
            snprintf(title, sizeof(title), "%s", iss->title);
            char row[1024];
            snprintf(row, sizeof(row), "#%d %s [%s]", iss->number, title, iss->state);
            item_row(fp, row, W);
        }
        if (n == 0) fprintf(fp, "|   %s\n", gs_t("(none in period)"));
    }
    fprintf(fp, "|\n");
    }

    if ((flags & FMT_SHOW_LABELS) && report->top_labels[0]) {
        snprintf(lbl, sizeof lbl, "%s:", gs_t("Top labels"));
        fprintf(fp, "| %s %s", lbl, report->top_labels);
        size_t len = strlen(report->top_labels);
        int pad = W - 17 - (int)len;
        if (pad < 0) { pad = 0; }
        fprintf(fp, "%*s|\n", pad, "");
        fprintf(fp, "|\n");
    }

    if ((flags & FMT_SHOW_RELEASES) && report->release_total > 0) {
    fprintf(fp, "| %s\n", gs_t("[RL] Releases"));
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Total"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, C, report->release_total, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    if (report->latest_release[0]) {
        snprintf(lbl, sizeof lbl, "%s:", gs_t("Latest"));
        fprintf(fp, "|   %-8s%s", lbl, report->latest_release);
        int pad = W - 20 - (int)strlen(report->latest_release);
        if (pad < 0) pad = 0;
        fprintf(fp, "%*s|\n", pad, "");
    }
    if (limit > 0 && releases) {
        size_t n = vector_len(releases);
        if (n > (size_t)limit) n = (size_t)limit;
        for (size_t i = 0; i < n; i++) {
            Release *rel = (Release *)vector_get(releases, i);
            char name[MAX_TITLE_LEN];
            snprintf(name, sizeof(name), "%s", rel->name[0] ? rel->name : rel->tag_name);
            char row[1024];
            snprintf(row, sizeof(row), "%s %s (%s)", rel->tag_name, name, rel->author);
            item_row(fp, row, W);
        }
    }
    fprintf(fp, "|\n");
    }

    if ((flags & FMT_SHOW_CONTRIBUTORS) && report->contributor_total > 0) {
    fprintf(fp, "| %s\n", gs_t("[CT] Contributors"));
    snprintf(lbl, sizeof lbl, "%s:", gs_t("Total"));
    fprintf(fp, "|   %-8s%s%3d%s", lbl, C, report->contributor_total, R);
    fprintf(fp, "%*s|\n", W - 18, "");
    if (report->top_contributor[0]) {
        snprintf(lbl, sizeof lbl, "%s:", gs_t("Top"));
        fprintf(fp, "|   %-8s%s", lbl, report->top_contributor);
        int pad = W - 20 - (int)strlen(report->top_contributor);
        if (pad < 0) pad = 0;
        fprintf(fp, "%*s|\n", pad, "");
    }
    if (report->top_contributor_list[0]) {
        print_wrapped(fp, report->top_contributor_list, W, gs_t("Top5: "));
    }
    fprintf(fp, "|\n");
    }

    if (flags & FMT_SHOW_SUMMARY) {
    fprintf(fp, "| %s\n", gs_t("[*] Summary"));
    size_t slen = strlen(report->summary);
    char *pretty = malloc(slen * 2 + 2);
    if (pretty) {
        md_prettify_text(report->summary, pretty, slen * 2 + 2);
        print_wrapped_block(fp, pretty, W, "  ");
        free(pretty);
    } else {
        print_wrapped(fp, report->summary, W, "  ");
    }
    fprintf(fp, "|\n");
    }

    if (uc) fprintf(fp, "%s", B);
    fprintf(fp, "+");
    print_fill(fp, '-', W - 2);
    fprintf(fp, "+\n");
    if (uc) fprintf(fp, "%s", R);

    return GS_OK;
}

const Exporter TXT_EXPORTER = {
    .write_report = txt_write_report
};
