/* HTML export — self-contained single-file report: inline CSS, colored
 * state badges, clickable links, anchored TOC, activity bars, and a
 * markdown-subset renderer for the summary section. */
#include "gitsight/export/exporter.h"
#include "gitsight/config.h"
#include "gitsight/i18n.h"
#include <stdlib.h>
#include <string.h>

static void html_escape(FILE *fp, const char *s) {
    for (; s && *s; s++) {
        switch (*s) {
            case '&':  fputs("&amp;", fp);  break;
            case '<':  fputs("&lt;", fp);   break;
            case '>':  fputs("&gt;", fp);   break;
            case '"':  fputs("&quot;", fp); break;
            default:   fputc(*s, fp);
        }
    }
}

static int safe_url(const char *u) {
    return strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0;
}

/* Inline markdown subset: `code`, **bold**, *em*, [text](url).
 * Unknown markup is emitted escaped. */
static void md_inline(FILE *fp, const char *s) {
    while (*s) {
        if (*s == '`') {
            const char *e = strchr(s + 1, '`');
            if (e && e > s + 1) {
                fputs("<code>", fp);
                for (const char *p = s + 1; p < e; p++) fputc(*p, fp);
                fputs("</code>", fp);
                s = e + 1;
                continue;
            }
        }
        if (s[0] == '*' && s[1] == '*') {
            const char *e = strstr(s + 2, "**");
            if (e && e > s + 2) {
                size_t n = (size_t)(e - (s + 2));
                char *inner = n < 4096 ? malloc(n + 1) : NULL;
                if (inner) {
                    memcpy(inner, s + 2, n);
                    inner[n] = '\0';
                    fputs("<strong>", fp);
                    md_inline(fp, inner);
                    fputs("</strong>", fp);
                    free(inner);
                    s = e + 2;
                    continue;
                }
            }
        }
        if (s[0] == '*' && s[1] && s[1] != '*') {
            const char *e = strchr(s + 1, '*');
            if (e && e > s + 1) {
                size_t n = (size_t)(e - (s + 1));
                char *inner = n < 4096 ? malloc(n + 1) : NULL;
                if (inner) {
                    memcpy(inner, s + 1, n);
                    inner[n] = '\0';
                    fputs("<em>", fp);
                    md_inline(fp, inner);
                    fputs("</em>", fp);
                    free(inner);
                    s = e + 1;
                    continue;
                }
            }
        }
        if (s[0] == '[') {
            const char *close = strchr(s, ']');
            if (close && close[1] == '(') {
                const char *paren = strchr(close + 2, ')');
                if (paren) {
                    size_t tl = (size_t)(close - s - 1);
                    size_t ul = (size_t)(paren - close - 2);
                    char *txt = tl < 4096 ? malloc(tl + 1) : NULL;
                    char *url = ul < 1024 ? malloc(ul + 1) : NULL;
                    if (txt && url) {
                        memcpy(txt, s + 1, tl); txt[tl] = '\0';
                        memcpy(url, close + 2, ul); url[ul] = '\0';
                        if (safe_url(url)) {
                            fputs("<a href=\"", fp);
                            html_escape(fp, url);
                            fputs("\">", fp);
                            html_escape(fp, txt);
                            fputs("</a>", fp);
                        } else {
                            html_escape(fp, txt);
                        }
                        s = paren + 1;
                    }
                    free(txt);
                    free(url);
                    continue;
                }
            }
        }
        html_escape(fp, (char[2]){ *s, 0 });
        s++;
    }
}

/* Block-level markdown subset used by the Summary section. */
static void md_blocks(FILE *fp, const char *text) {
    char *copy = strdup(text);
    if (!copy) return;
    enum { NONE, PARA, UL, OL } mode = NONE;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t') line++;
        int heading = 0;
        if (line[0] == '#') {
            const char *p = line;
            while (*p == '#') { heading++; p++; }
            if (heading <= 6 && (*p == ' ' || *p == '\0')) {
                if (mode == PARA) fputs("</p>\n", fp);
                mode = NONE;
                if (heading > 4) heading = 4;
                fprintf(fp, "<h%d>", heading + 2);
                md_inline(fp, line + heading + 1);
                fprintf(fp, "</h%d>\n", heading + 2);
                continue;
            }
            heading = 0;
        }
        if (line[0] == '\0') {
            if (mode == PARA) fputs("</p>\n", fp);
            if (mode == UL)   fputs("</ul>\n", fp);
            if (mode == OL)   fputs("</ol>\n", fp);
            mode = NONE;
            continue;
        }
        if (strcmp(line, "---") == 0 || strcmp(line, "***") == 0) {
            fputs("<hr>\n", fp);
            continue;
        }
        int ul_item = (line[0] == '-' || line[0] == '*') && line[1] == ' ';
        int ol_item = 0;
        if (!ul_item) {
            const char *p = line;
            int digits = 0;
            while (*p >= '0' && *p <= '9') { p++; digits++; }
            ol_item = digits > 0 && digits <= 2 && p[0] == '.' && p[1] == ' ';
        }
        if (ul_item || ol_item) {
            int want = ul_item ? UL : OL;
            if (mode == PARA) fputs("</p>\n", fp);
            if (mode != UL && mode != OL)
                fprintf(fp, "<%s>\n", ul_item ? "ul" : "ol");
            else if (mode != want)
                fputs(mode == UL ? "</ul>\n<ol>\n" : "</ol>\n<ul>\n", fp);
            mode = want;
            const char *rest = ul_item ? line + 2 : line;
            while (*rest >= '0' && *rest <= '9') rest++;
            if (*rest == '.') rest++;
            while (*rest == ' ') rest++;
            fputs("<li>", fp);
            md_inline(fp, rest);
            fputs("</li>\n", fp);
            continue;
        }
        if (mode == UL)   { fputs("</ul>\n", fp); mode = NONE; }
        if (mode == OL)   { fputs("</ol>\n", fp); mode = NONE; }
        if (mode != PARA) { fputs("<p>", fp); mode = PARA; }
        else              fputs("<br>", fp);
        md_inline(fp, line);
    }
    if (mode == PARA) fputs("</p>\n", fp);
    if (mode == UL)   fputs("</ul>\n", fp);
    if (mode == OL)   fputs("</ol>\n", fp);
    free(copy);
}

static void badge(FILE *fp, const char *state) {
    const char *cls = "b-none";
    if (!strcmp(state, "open"))        cls = "b-open";
    else if (!strcmp(state, "closed")) cls = "b-closed";
    else if (!strcmp(state, "merged")) cls = "b-merged";
    fputs("<span class=\"badge ", fp);
    fputs(cls, fp);
    fputs("\">", fp);
    html_escape(fp, state);
    fputs("</span>", fp);
}

static void rgb_hex(char out[8], const unsigned char c[3]) {
    snprintf(out, 8, "#%02x%02x%02x", c[0], c[1], c[2]);
}

static void css(FILE *fp, int uc, const HtmlExportConfig *hoc) {
    char tb[8], hb[8], lb[8], kb[8], ob[8], cb[8], mb[8];
    int colored = uc && hoc;
    if (colored) {
        rgb_hex(tb, hoc->title_color);
        rgb_hex(hb, hoc->header_color);
        rgb_hex(lb, hoc->label_color);
        rgb_hex(kb, hoc->link_color);
        rgb_hex(ob, hoc->open_color);
        rgb_hex(cb, hoc->closed_color);
        rgb_hex(mb, hoc->merged_color);
    } else {
        strcpy(tb, "#000"); strcpy(hb, "#000"); strcpy(lb, "#000"); strcpy(kb, "#000");
        strcpy(ob, "inherit"); strcpy(cb, "inherit"); strcpy(mb, "inherit");
    }
    fprintf(fp,
        "<style>\n"
        "body{font-family:system-ui,'Segoe UI',Arial,'Noto Sans CJK SC',sans-serif;"
        "max-width:860px;margin:2em auto;padding:0 1em;color:#111;line-height:1.5}\n"
        "h1{color:%s}h2{color:%s;border-bottom:2px solid #ddd;padding-bottom:.2em}\n"
        "a{color:%s}\n"
        ".muted{color:%s}.period{color:%s}\n"
        "table{border-collapse:collapse;margin:.6em 0}\n"
        "th,td{border:1px solid #ccc;padding:4px 10px;text-align:left}\n"
        "th{background:%s;color:#fff}\n"
        "nav a{margin-right:1em}\n"
        ".badge{border-radius:10px;padding:1px 8px;font-size:85%%;border:1px solid #999}\n"
        ".b-open{color:%s}.b-closed{color:%s}.b-merged{color:%s}.b-none{color:inherit}\n"
        ".chart{margin:.6em 0}.row{display:flex;align-items:center;margin:2px 0}\n"
        ".row .lbl{width:8em;font-size:90%%;color:%s}.row .val{margin-left:6px;font-size:90%%}\n"
        ".bar{flex:1;background:#eee;height:12px;border-radius:3px}\n"
        ".bar i{display:block;height:100%%;background:%s;border-radius:3px}\n"
        ".bar i.chr-add{background:%s}.bar i.chr-del{background:%s;margin-left:2px}\n"
        "code{background:#f4f4f4;padding:1px 4px;border-radius:3px}\n"
        "input.tfilter{padding:4px 8px;border:1px solid #ccc;border-radius:4px;"
        "margin:.2em 0 0;width:16em;font-size:90%%}\n"
        ".foot{margin-top:2em;color:%s;font-size:85%%}\n"
        "</style>\n",
        tb, hb, kb, lb, lb,
        colored ? hb : "#666",
        ob, cb,
        ob, cb, mb,
        lb, colored ? hb : "#888", lb);
}

static void sec_head(FILE *fp, const char *id, const char *title) {
    fprintf(fp, "<h2 id=\"%s\">", id);
    html_escape(fp, title);
    fputs("</h2>\n", fp);
}

/* Live keyword-filter input bound (by the inline script at the bottom of
 * the page) to the detail table with the given id. */
static void filter_input(FILE *fp, const char *tbl_id) {
    fputs("<p><input class=\"tfilter\" type=\"text\" data-target=\"", fp);
    fputs(tbl_id, fp);
    fputs("\" placeholder=\"", fp);
    html_escape(fp, gs_t("Filter rows"));
    fputs("\"></p>\n", fp);
}

static void item_table_open(FILE *fp, const char *tbl_id, const char *cols_html) {
    filter_input(fp, tbl_id);
    fprintf(fp, "<table id=\"%s\">\n<tr>%s</tr>\n", tbl_id, cols_html);
}

static void js_filter(FILE *fp) {
    (void)fp;
    fputs(
      "<script>\n"
      "document.querySelectorAll('input.tfilter').forEach(function(inp){\n"
      "  var tbl=document.getElementById(inp.getAttribute('data-target'));\n"
      "  if(!tbl)return;\n"
      "  inp.addEventListener('input',function(){\n"
      "    var q=inp.value.toLowerCase();\n"
      "    tbl.querySelectorAll('tr').forEach(function(tr){\n"
      "      if(!tr.querySelector('td'))return;\n"
      "      tr.style.display=tr.textContent.toLowerCase().indexOf(q)>=0?'':'none';\n"
      "    });\n"
      "  });\n"
      "});\n"
      "</script>\n", fp);
}

/* Translation keys carry a "[XX] " disambiguation prefix (md headers, PDF
 * sections); strip it for HTML headings and the TOC. */
static const char *sec_title(const char *id) {
    const char *t = gs_t(id);
    if (t[0] == '[') {
        const char *close = strstr(t, "] ");
        if (close && close - t <= 5) t = close + 2;
    }
    return t;
}

static ErrorCode html_write_report(FILE *fp, const Report *report,
                                   const Vector *prs, const Vector *commits,
                                   const Vector *issues, const Vector *releases,
                                   const FormatOptions *opts) {
    int uc = opts ? opts->color_active : 0;
    int flags = opts ? opts->flags : ~0;
    int limit = opts && opts->item_limit > 0 ? opts->item_limit : 10;

    fputs("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n", fp);
    fprintf(fp, "<title>");
    html_escape(fp, gs_t("GitSight Report"));
    fprintf(fp, " - %s/%s</title>\n", report->owner, report->repo);
    css(fp, uc, opts && opts->extra ? (const HtmlExportConfig *)opts->extra : NULL);
    fputs("</head>\n<body>\n", fp);

    fprintf(fp, "<h1>%s - ", gs_t("GitSight Report"));
    html_escape(fp, report->owner);
    fputs("/", fp);
    html_escape(fp, report->repo);
    fputs("</h1>\n", fp);
    fprintf(fp, "<p class=\"period\">%s: ", gs_t("Period"));
    html_escape(fp, report->period);
    fputs("</p>\n", fp);

    /* TOC */
    fputs("<nav class=\"muted\">", fp);
    if (report->meta.has_meta)
        fprintf(fp, "<a href=\"#meta\">%s</a>", gs_t("Repository"));
    if (flags & FMT_SHOW_PRS)
        fprintf(fp, "<a href=\"#prs\">%s</a>", sec_title("[PR] Pull Requests"));
    if (flags & FMT_SHOW_COMMITS)
        fprintf(fp, "<a href=\"#commits\">%s</a>", sec_title("[CM] Commits"));
    if (flags & FMT_SHOW_ISSUES)
        fprintf(fp, "<a href=\"#issues\">%s</a>", sec_title("[IS] Issues"));
    if ((flags & FMT_SHOW_RELEASES) && report->release_total > 0)
        fprintf(fp, "<a href=\"#releases\">%s</a>", sec_title("[RL] Releases"));
    if ((flags & FMT_SHOW_LANGS) && report->lang_count > 0)
        fprintf(fp, "<a href=\"#languages\">%s</a>", sec_title("[LG] Languages"));
    if (flags & FMT_SHOW_SUMMARY)
        fprintf(fp, "<a href=\"#summary\">%s</a>", sec_title("[*] Summary"));
    fputs("</nav>\n", fp);

    if (report->meta.has_meta) {
        sec_head(fp, "meta", gs_t("Repository"));
        fputs("<table>\n", fp);
        fputs("<tr><th>Stars</th><th>Forks</th><th>Language</th><th>License</th><th>Default branch</th></tr>\n", fp);
        fprintf(fp, "<tr><td>%ld</td><td>%ld</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
                report->meta.stars, report->meta.forks,
                report->meta.language[0] ? report->meta.language : "-",
                report->meta.license[0] ? report->meta.license : "-",
                report->meta.default_branch[0] ? report->meta.default_branch : "-");
        fputs("</table>\n", fp);
        if (report->meta.description[0]) {
            fputs("<p class=\"muted\">", fp);
            html_escape(fp, report->meta.description);
            fputs("</p>\n", fp);
        }
        if (report->branch[0])
            fprintf(fp, "<p class=\"muted\">%s: %s</p>\n", gs_t("Branch"), report->branch);
    }

    if (flags & FMT_SHOW_PRS) {
        sec_head(fp, "prs", sec_title("[PR] Pull Requests"));
        fprintf(fp,
            "<ul class=\"muted\"><li>%s: %d</li><li>%s: %d</li><li>%s: %d</li><li>%s: %d</li>",
            gs_t("Total"), report->pr_total,
            gs_t("Merged"), report->pr_merged,
            gs_t("Open"), report->pr_open,
            gs_t("Closed"), report->pr_closed - report->pr_merged);
        if (report->pr_merged_count > 0) {
            fprintf(fp, "<li>%s: %.1f %s</li>", gs_t("Avg merge time"),
                    report->pr_avg_merge_hours, gs_t("hours"));
        }
        fputs("</ul>\n", fp);
        if (limit > 0 && prs) {
            size_t n = vector_len(prs);
            if (n > (size_t)limit) n = (size_t)limit;
            if (n > 0) {
                fprintf(fp, "<p><strong>%s</strong></p>\n", gs_t("Recent pull requests:"));
                item_table_open(fp, "tbl-prs", "<th>#</th><th>Title</th><th>State</th><th>Author</th>");
                for (size_t i = 0; i < n; i++) {
                    PR *pr = (PR *)vector_get(prs, i);
                    fprintf(fp, "<tr><td>");
                    if (pr->html_url[0] && safe_url(pr->html_url))
                        fprintf(fp, "<a href=\"%s\">#%d</a>", pr->html_url, pr->number);
                    else
                        fprintf(fp, "#%d", pr->number);
                    fputs("</td><td>", fp);
                    html_escape(fp, pr->title);
                    fputs("</td><td>", fp);
                    badge(fp, pr->state);
                    fputs("</td><td>", fp);
                    html_escape(fp, pr->author);
                    fputs("</td></tr>\n", fp);
                }
                fputs("</table>\n", fp);
            }
        }
    }

    if (flags & FMT_SHOW_COMMITS) {
        sec_head(fp, "commits", sec_title("[CM] Commits"));
        fprintf(fp, "<ul class=\"muted\"><li>%s: %d</li><li>%s: %d</li>",
                gs_t("Total"), report->commit_total,
                gs_t("Authors"), report->author_total);
        if (report->top_author[0])
            fprintf(fp, "<li>%s: %s</li>", gs_t("Top author"), report->top_author);
        fputs("</ul>\n", fp);

        if (report->activity_count > 1 && report->activity_max > 0) {
            fprintf(fp, "<p><strong>%s</strong></p>\n<div class=\"chart\">\n",
                    gs_t("Activity (commits/day):"));
            for (int i = report->activity_count - 1; i >= 0; i--) {
                int pct = (report->activity_values[i] * 100) / report->activity_max;
                if (pct < 2 && report->activity_values[i] > 0) pct = 2;
                if (pct > 100) pct = 100;
                fputs("<div class=\"row\"><span class=\"lbl\">", fp);
                html_escape(fp, report->activity_labels[i]);
                fprintf(fp, "</span><span class=\"bar\"><i style=\"width:%d%%\"></i></span>"
                            "<span class=\"val\">%d</span></div>\n",
                        pct, report->activity_values[i]);
            }
            fputs("</div>\n", fp);
        }

        if (report->churn_available &&
            report->churn_additions + report->churn_deletions > 0) {
            long churn_max = 0;
            for (int i = 0; i < report->activity_count; i++) {
                if (report->churn_add_values[i] > churn_max) churn_max = report->churn_add_values[i];
                if (report->churn_del_values[i] > churn_max) churn_max = report->churn_del_values[i];
            }
            fprintf(fp, "<p><strong>%s: %ld / %ld</strong></p>\n<div class=\"chart\">\n",
                    gs_t("Code churn"),
                    report->churn_additions, report->churn_deletions);
            for (int i = report->activity_count - 1; i >= 0; i--) {
                int added = (int)((report->churn_add_values[i] * 100) / churn_max);
                int removed = (int)((report->churn_del_values[i] * 100) / churn_max);
                if (added < 2 && report->churn_add_values[i] > 0) added = 2;
                if (removed < 2 && report->churn_del_values[i] > 0) removed = 2;
                if (added > 100) added = 100;
                if (removed > 100) removed = 100;
                fputs("<div class=\"row\"><span class=\"lbl\">", fp);
                html_escape(fp, report->activity_labels[i]);
                fprintf(fp, "</span><span class=\"bar\"><i class=\"chr-add\" style=\"width:%d%%\"></i>"
                            "<i class=\"chr-del\" style=\"width:%d%%\"></i></span>"
                            "<span class=\"val\">+%ld / -%ld</span></div>\n",
                        added, removed,
                        report->churn_add_values[i], report->churn_del_values[i]);
            }
            fputs("</div>\n", fp);
        }

        if (limit > 0 && commits) {
            size_t n = vector_len(commits);
            if (n > (size_t)limit) n = (size_t)limit;
            if (n > 0) {
                fprintf(fp, "<p><strong>%s</strong></p>\n", gs_t("Recent commits:"));
                item_table_open(fp, "tbl-commits", "<th>Sha</th><th>Message</th><th>Author</th><th>Date</th>");
                for (size_t i = 0; i < n; i++) {
                    Commit *c = (Commit *)vector_get(commits, i);
                    fputs("<tr><td><code>", fp);
                    html_escape(fp, c->sha);
                    fputs("</code></td><td>", fp);
                    if (c->html_url[0] && safe_url(c->html_url))
                        fprintf(fp, "<a href=\"%s\">", c->html_url);
                    html_escape(fp, c->message);
                    if (c->html_url[0] && safe_url(c->html_url))
                        fputs("</a>", fp);
                    fprintf(fp, "</td><td>%s</td><td>%s</td></tr>\n",
                            c->author, c->date);
                }
                fputs("</table>\n", fp);
            }
        }
    }

    if (flags & FMT_SHOW_ISSUES) {
        sec_head(fp, "issues", sec_title("[IS] Issues"));
        fprintf(fp, "<ul class=\"muted\"><li>%s: %d</li><li>%s: %d</li><li>%s: %d</li>",
                gs_t("Total"), report->issue_total,
                gs_t("Open"), report->issue_open,
                gs_t("Closed"), report->issue_closed);
        if (report->issue_closed_count > 0) {
            fprintf(fp, "<li>%s: %.1f %s</li>", gs_t("Avg close time"),
                    report->issue_avg_close_hours, gs_t("hours"));
        }
        fputs("</ul>\n", fp);
        if (limit > 0 && issues) {
            size_t n = vector_len(issues);
            if (n > (size_t)limit) n = (size_t)limit;
            if (n > 0) {
                fprintf(fp, "<p><strong>%s</strong></p>\n", gs_t("Recent issues:"));
                item_table_open(fp, "tbl-issues", "<th>#</th><th>Title</th><th>State</th><th>Author</th>");
                for (size_t i = 0; i < n; i++) {
                    Issue *iss = (Issue *)vector_get(issues, i);
                    fputs("<tr><td>", fp);
                    if (iss->html_url[0] && safe_url(iss->html_url))
                        fprintf(fp, "<a href=\"%s\">#%d</a>", iss->html_url, iss->number);
                    else
                        fprintf(fp, "#%d", iss->number);
                    fputs("</td><td>", fp);
                    html_escape(fp, iss->title);
                    fputs("</td><td>", fp);
                    badge(fp, iss->state);
                    fprintf(fp, "</td><td>%s</td></tr>\n", iss->author);
                }
                fputs("</table>\n", fp);
            }
        }
        if ((flags & FMT_SHOW_LABELS) && report->top_labels[0]) {
            fprintf(fp, "<p class=\"muted\"><strong>%s</strong>: ", gs_t("Top labels"));
            html_escape(fp, report->top_labels);
            fputs("</p>\n", fp);
        }
    }

    if ((flags & FMT_SHOW_RELEASES) && report->release_total > 0) {
        sec_head(fp, "releases", sec_title("[RL] Releases"));
        fprintf(fp, "<ul class=\"muted\"><li>%s: %d</li>", gs_t("Total"), report->release_total);
        if (report->latest_release[0])
            fprintf(fp, "<li>%s: %s</li>", gs_t("Latest"), report->latest_release);
        fputs("</ul>\n", fp);
        if (limit > 0 && releases) {
            size_t n = vector_len(releases);
            if (n > (size_t)limit) n = (size_t)limit;
            if (n > 0) {
                fprintf(fp, "<p><strong>%s</strong></p>\n", gs_t("Recent releases:"));
                item_table_open(fp, "tbl-releases", "<th>Tag</th><th>Name</th><th>Author</th><th>Date</th>");
                for (size_t i = 0; i < n; i++) {
                    Release *rel = (Release *)vector_get(releases, i);
                    fputs("<tr><td>", fp);
                    if (rel->html_url[0] && safe_url(rel->html_url))
                        fprintf(fp, "<a href=\"%s\">", rel->html_url);
                    html_escape(fp, rel->tag_name);
                    if (rel->html_url[0] && safe_url(rel->html_url))
                        fputs("</a>", fp);
                    fprintf(fp, "</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
                            rel->name[0] ? rel->name : rel->tag_name,
                            rel->author, rel->published_at);
                }
                fputs("</table>\n", fp);
            }
        }
    }

    if ((flags & FMT_SHOW_CONTRIBUTORS) && report->contributor_total > 0) {
        fprintf(fp, "<p class=\"muted\"><strong>%s</strong>: %d", sec_title("[CT] Contributors"),
                report->contributor_total);
        if (report->top_contributor[0])
            fprintf(fp, " · %s", report->top_contributor);
        fputs("</p>\n", fp);
    }

    if ((flags & FMT_SHOW_LANGS) && report->lang_count > 0) {
        long long total = 0;
        for (int i = 0; i < report->lang_count; i++) total += report->lang_bytes[i];
        sec_head(fp, "languages", sec_title("[LG] Languages"));
        fputs("<div class=\"chart\">\n", fp);
        for (int i = 0; i < report->lang_count; i++) {
            double pct = total > 0 ? 100.0 * report->lang_bytes[i] / total : 0.0;
            int w = (int)(pct + 0.5);
            if (w < 1 && pct > 0) w = 1;
            if (w > 100) w = 100;
            fputs("<div class=\"row\"><span class=\"lbl\">", fp);
            html_escape(fp, report->lang_names[i]);
            fprintf(fp, "</span><span class=\"bar\"><i style=\"width:%d%%\"></i></span>"
                        "<span class=\"val\">%.1f%%</span></div>\n", w, pct);
        }
        fputs("</div>\n", fp);
    }

    if (flags & FMT_SHOW_SUMMARY) {
        sec_head(fp, "summary", sec_title("[*] Summary"));
        size_t slen = strlen(report->summary);
        char *pretty = malloc(slen * 2 + 2);
        if (pretty) {
            md_prettify_text(report->summary, pretty, slen * 2 + 2);
            md_blocks(fp, pretty);
            free(pretty);
        } else {
            fputs("<p>", fp);
            html_escape(fp, report->summary);
            fputs("</p>\n", fp);
        }
    }

    fprintf(fp, "<p class=\"foot\">");
    md_inline(fp, gs_t("Generated by [GitSight](https://github.com/user/gitsight)"));
    fputs("</p>\n", fp);
    js_filter(fp);
    fputs("</body>\n</html>\n", fp);
    return GS_OK;
}

const Exporter HTML_EXPORTER = {
    .write_report = html_write_report
};
