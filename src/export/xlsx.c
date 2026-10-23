#include "miniz.h"
#include "gitsight/export/xlsx.h"
#include "gitsight/export/exporter.h"
#include "gitsight/config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define XML_BUF 16384

/* File-scope XLSX config (set by export_xlsx) */
static int g_xlsx_font_size = 11;
static char g_xlsx_font_name[64] = "Calibri";
static float g_xlsx_col_a_width = 20.0f;
static float g_xlsx_col_b_width = 60.0f;
static int g_xlsx_border = 1;
static char g_xlsx_sheet_name[64] = "Report";
static int g_xlsx_flags = ~0;

static size_t escape_xml(const char *src, char *dst, size_t cap) {
    size_t pos = 0;
    for (; *src && pos < cap - 6; src++) {
        switch (*src) {
            case '&': pos += snprintf(dst + pos, cap - pos, "&amp;"); break;
            case '<': pos += snprintf(dst + pos, cap - pos, "&lt;"); break;
            case '>': pos += snprintf(dst + pos, cap - pos, "&gt;"); break;
            case '"': pos += snprintf(dst + pos, cap - pos, "&quot;"); break;
            case '\'': pos += snprintf(dst + pos, cap - pos, "&apos;"); break;
            case '\n': pos += snprintf(dst + pos, cap - pos, "&#10;"); break;
            default:  dst[pos++] = *src; break;
        }
    }
    dst[pos] = '\0';
    return pos;
}

static size_t build_content_types(char *b, size_t c) {
    return snprintf(b, c,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
        "</Types>");
}

static size_t build_rels(char *b, size_t c) {
    return snprintf(b, c,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>");
}

static size_t build_workbook(char *b, size_t c) {
    return snprintf(b, c,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"%s\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>",
        g_xlsx_sheet_name);
}

static size_t build_wb_rels(char *b, size_t c) {
    return snprintf(b, c,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
        "</Relationships>");
}

static size_t build_styles(char *b, size_t c) {
    int bcnt = g_xlsx_border ? 2 : 1;
    return snprintf(b, c,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"2\">"
        "<font><sz val=\"%d\"/><name val=\"%s\"/></font>"
        "<font><b/><sz val=\"%d\"/><name val=\"%s\"/></font>"
        "</fonts>"
        "<fills count=\"2\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "</fills>"
        "<borders count=\"%d\">"
        "<border><left/><right/><top/><bottom/><diagonal/></border>"
        "%s"
        "</borders>"
        "<cellStyleXfs count=\"1\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>"
        "</cellStyleXfs>"
        "<cellXfs count=\"3\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"%d\" xfId=\"0\" applyFont=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"%d\" xfId=\"0\" applyBorder=\"1\"/>"
        "</cellXfs>"
        "</styleSheet>",
        g_xlsx_font_size, g_xlsx_font_name,
        g_xlsx_font_size, g_xlsx_font_name,
        bcnt,
        g_xlsx_border
            ? "<border>"
              "<left style=\"thin\"><color auto=\"1\"/></left>"
              "<right style=\"thin\"><color auto=\"1\"/></right>"
              "<top style=\"thin\"><color auto=\"1\"/></top>"
              "<bottom style=\"thin\"><color auto=\"1\"/></bottom>"
              "<diagonal/>"
              "</border>"
            : "",
        g_xlsx_border ? 1 : 0,
        g_xlsx_border ? 1 : 0);
}

static size_t build_shared_strings(char *b, size_t c, const Report *r) {
    size_t p = 0;
    char own[320];

    snprintf(own, sizeof(own), "%s/%s", r->owner, r->repo);
    const char *strings[] = {
        "GitSight Report", "Repository", own, "Period", r->period,
        "[PR] Pull Requests", "Total", "Merged", "Open", "Closed",
        "[CM] Commits", "Authors", "Top Author", r->top_author[0] ? r->top_author : "-",
        "Additions", "Deletions",
        "[IS] Issues", "[*] Summary",
        "[RL] Releases", "Latest release", r->latest_release[0] ? r->latest_release : "-",
        "[CT] Contributors", "Top contributor", r->top_contributor[0] ? r->top_contributor : "-",
        "[META] Repository", "Stars", "Forks", "Language", "License", "Default branch", "Branch",
        r->meta.language[0] ? r->meta.language : "-",
        r->meta.license[0] ? r->meta.license : "-",
        r->meta.default_branch[0] ? r->meta.default_branch : "-",
        r->branch[0] ? r->branch : "-"
    };
    int n = sizeof(strings) / sizeof(strings[0]);

    p += snprintf(b + p, c - p,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"%d\" uniqueCount=\"%d\">",
        n + 1, n + 1);
    for (int i = 0; i < n; i++)
        p += snprintf(b + p, c - p, "<si><t>%s</t></si>", strings[i]);

    char sum_esc[4096];
    escape_xml(r->summary, sum_esc, sizeof(sum_esc));
    p += snprintf(b + p, c - p, "<si><t>%s</t></si>", sum_esc);
    p += snprintf(b + p, c - p, "</sst>");
    return p;
}

static void cell_s(char *b, size_t c, size_t *p, const char *col, int row, int style, int idx) {
    *p += snprintf(b + *p, c - *p,
        "<c r=\"%s%d\" t=\"s\" s=\"%d\"><v>%d</v></c>",
        col, row, style, idx);
}

static void cell_n(char *b, size_t c, size_t *p, const char *col, int row, int style, int val) {
    *p += snprintf(b + *p, c - *p,
        "<c r=\"%s%d\" s=\"%d\"><v>%d</v></c>",
        col, row, style, val);
}

/* Same numeric cell, but for counters that can pass 2^31 (churn totals are
   aggregated longs; a decade-old huge repository can get there). */
static void cell_n_l(char *b, size_t c, size_t *p, const char *col, int row, int style, long val) {
    *p += snprintf(b + *p, c - *p,
        "<c r=\"%s%d\" s=\"%d\"><v>%ld</v></c>",
        col, row, style, val);
}

static void empty_row(char *b, size_t c, size_t *p, int row) {
    *p += snprintf(b + *p, c - *p, "<row r=\"%d\"/>", row);
}

#define COL_A(cell) cell_s(b, c, &p, "A", row, 1, cell)
#define COL_B(cell) cell_s(b, c, &p, "B", row, 1, cell)
#define VAL_A(cell) cell_s(b, c, &p, "A", row, 2, cell)
#define VAL_B(cell) cell_s(b, c, &p, "B", row, 2, cell)
#define NUM_A(num) cell_n(b, c, &p, "A", row, 2, num)
#define NUM_B(num) cell_n(b, c, &p, "B", row, 2, num)

static int str_idx(const char *s, const char **arr, int n) {
    for (int i = 0; i < n; i++)
        if (strcmp(s, arr[i]) == 0) return i;
    return 0;
}

static size_t build_sheet(char *b, size_t c, const Report *r) {
    size_t p = 0;
    int row = 0;
    char own[320];

    snprintf(own, sizeof(own), "%s/%s", r->owner, r->repo);

    const char *strs[64];
    int n = 0;
    strs[n++] = "GitSight Report";
    strs[n++] = "Repository";
    strs[n++] = own;
    strs[n++] = "Period";
    strs[n++] = r->period;
    strs[n++] = "[PR] Pull Requests";
    strs[n++] = "Total";
    strs[n++] = "Merged";
    strs[n++] = "Open";
    strs[n++] = "Closed";
    strs[n++] = "[CM] Commits";
    strs[n++] = "Authors";
    strs[n++] = "Top Author";
    strs[n++] = r->top_author[0] ? r->top_author : "-";
    strs[n++] = "Additions";
    strs[n++] = "Deletions";
    strs[n++] = "[IS] Issues";
    strs[n++] = "[*] Summary";
    strs[n++] = "[RL] Releases";
    strs[n++] = "Latest release";
    strs[n++] = r->latest_release[0] ? r->latest_release : "-";
    strs[n++] = "[CT] Contributors";
    strs[n++] = "Top contributor";
    strs[n++] = r->top_contributor[0] ? r->top_contributor : "-";
    strs[n++] = "[META] Repository";
    strs[n++] = "Stars";
    strs[n++] = "Forks";
    strs[n++] = "Language";
    strs[n++] = "License";
    strs[n++] = "Default branch";
    strs[n++] = "Branch";
    strs[n++] = r->meta.language[0] ? r->meta.language : "-";
    strs[n++] = r->meta.license[0] ? r->meta.license : "-";
    strs[n++] = r->meta.default_branch[0] ? r->meta.default_branch : "-";
    strs[n++] = r->branch[0] ? r->branch : "-";
    int summary_idx = n;

#define SI(s) str_idx((s), strs, n)

    p += snprintf(b + p, c - p,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<cols>"
        "<col min=\"1\" max=\"1\" width=\"%.0f\" customWidth=\"1\"/>"
        "<col min=\"2\" max=\"2\" width=\"%.0f\" customWidth=\"1\"/>"
        "</cols>"
        "<sheetData>",
        (double)g_xlsx_col_a_width, (double)g_xlsx_col_b_width);

    row = 1;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 1, SI("GitSight Report"));
    p += snprintf(b + p, c - p, "</row>");

    row = 2;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Repository"));
    cell_s(b, c, &p, "B", row, 2, SI(own));
    p += snprintf(b + p, c - p, "</row>");

    row = 3;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Period"));
    cell_s(b, c, &p, "B", row, 2, SI(r->period));
    p += snprintf(b + p, c - p, "</row>");

    row = 4; empty_row(b, c, &p, row);

    row = 5;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 1, SI("[PR] Pull Requests"));
    p += snprintf(b + p, c - p, "</row>");

    row = 6;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Total"));
    cell_n(b, c, &p, "B", row, 2, r->pr_total);
    p += snprintf(b + p, c - p, "</row>");

    row = 7;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Merged"));
    cell_n(b, c, &p, "B", row, 2, r->pr_merged);
    p += snprintf(b + p, c - p, "</row>");

    row = 8;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Open"));
    cell_n(b, c, &p, "B", row, 2, r->pr_open);
    p += snprintf(b + p, c - p, "</row>");

    row = 9;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Closed"));
    cell_n(b, c, &p, "B", row, 2, r->pr_closed);
    p += snprintf(b + p, c - p, "</row>");

    row = 10; empty_row(b, c, &p, row);

    row = 11;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 1, SI("[CM] Commits"));
    p += snprintf(b + p, c - p, "</row>");

    row = 12;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Total"));
    cell_n(b, c, &p, "B", row, 2, r->commit_total);
    p += snprintf(b + p, c - p, "</row>");

    row = 13;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Authors"));
    cell_n(b, c, &p, "B", row, 2, r->author_total);
    p += snprintf(b + p, c - p, "</row>");

    row = 14;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Top Author"));
    cell_s(b, c, &p, "B", row, 2, SI(r->top_author[0] ? r->top_author : "-"));
    p += snprintf(b + p, c - p, "</row>");

    /* Code churn rows keep their fixed numbers; when the platform supplies
       no line counts the rows are simply absent, which reads as no data. */
    if (r->churn_available) {
        row = 15;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("Additions"));
        cell_n_l(b, c, &p, "B", row, 2, r->churn_additions);
        p += snprintf(b + p, c - p, "</row>");

        row = 16;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("Deletions"));
        cell_n_l(b, c, &p, "B", row, 2, r->churn_deletions);
        p += snprintf(b + p, c - p, "</row>");
    }

    row = 17; empty_row(b, c, &p, row);

    row = 18;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 1, SI("[IS] Issues"));
    p += snprintf(b + p, c - p, "</row>");

    row = 19;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Total"));
    cell_n(b, c, &p, "B", row, 2, r->issue_total);
    p += snprintf(b + p, c - p, "</row>");

    row = 20;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Open"));
    cell_n(b, c, &p, "B", row, 2, r->issue_open);
    p += snprintf(b + p, c - p, "</row>");

    row = 21;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Closed"));
    cell_n(b, c, &p, "B", row, 2, r->issue_closed);
    p += snprintf(b + p, c - p, "</row>");

    row = 22; empty_row(b, c, &p, row);

    row = 23;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 1, SI("[RL] Releases"));
    p += snprintf(b + p, c - p, "</row>");

    row = 24;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Total"));
    cell_n(b, c, &p, "B", row, 2, r->release_total);
    p += snprintf(b + p, c - p, "</row>");

    row = 25;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Latest release"));
    cell_s(b, c, &p, "B", row, 2, SI(r->latest_release[0] ? r->latest_release : "-"));
    p += snprintf(b + p, c - p, "</row>");

    row = 26; empty_row(b, c, &p, row);

    row = 27;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 1, SI("[CT] Contributors"));
    p += snprintf(b + p, c - p, "</row>");

    row = 28;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Total"));
    cell_n(b, c, &p, "B", row, 2, r->contributor_total);
    p += snprintf(b + p, c - p, "</row>");

    row = 29;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, SI("Top contributor"));
    cell_s(b, c, &p, "B", row, 2, SI(r->top_contributor[0] ? r->top_contributor : "-"));
    p += snprintf(b + p, c - p, "</row>");

    row = 30; empty_row(b, c, &p, row);

    row = 31;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 1, SI("[*] Summary"));
    p += snprintf(b + p, c - p, "</row>");

    row = 32;
    p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
    cell_s(b, c, &p, "A", row, 2, summary_idx);
    p += snprintf(b + p, c - p, "</row>");

    if (r->meta.has_meta) {
        row = 33;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 1, SI("[META] Repository"));
        p += snprintf(b + p, c - p, "</row>");

        row = 34;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("Stars"));
        cell_n(b, c, &p, "B", row, 2, (int)r->meta.stars);
        p += snprintf(b + p, c - p, "</row>");

        row = 35;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("Forks"));
        cell_n(b, c, &p, "B", row, 2, (int)r->meta.forks);
        p += snprintf(b + p, c - p, "</row>");

        row = 36;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("Language"));
        cell_s(b, c, &p, "B", row, 2, SI(r->meta.language[0] ? r->meta.language : "-"));
        p += snprintf(b + p, c - p, "</row>");

        row = 37;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("License"));
        cell_s(b, c, &p, "B", row, 2, SI(r->meta.license[0] ? r->meta.license : "-"));
        p += snprintf(b + p, c - p, "</row>");

        row = 38;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("Default branch"));
        cell_s(b, c, &p, "B", row, 2, SI(r->meta.default_branch[0] ? r->meta.default_branch : "-"));
        p += snprintf(b + p, c - p, "</row>");

        row = 39;
        p += snprintf(b + p, c - p, "<row r=\"%d\">", row);
        cell_s(b, c, &p, "A", row, 2, SI("Branch"));
        cell_s(b, c, &p, "B", row, 2, SI(r->branch[0] ? r->branch : "-"));
        p += snprintf(b + p, c - p, "</row>");
    }

    p += snprintf(b + p, c - p, "</sheetData></worksheet>");
    return p;
}

ErrorCode export_xlsx(FILE *fp, const Report *report,
                      const Vector *prs, const Vector *commits,
                      const Vector *issues, const Vector *releases,
                      const FormatOptions *opts) {
    (void)prs; (void)commits; (void)issues; (void)releases;
    const XlsxExportConfig *xec = (opts && opts->extra) ? (const XlsxExportConfig *)opts->extra : NULL;
    g_xlsx_font_size = xec ? xec->font_size : 11;
    snprintf(g_xlsx_font_name, sizeof(g_xlsx_font_name), "%s", xec ? xec->font_name : "Calibri");
    g_xlsx_col_a_width = xec ? xec->col_a_width : 20.0f;
    g_xlsx_col_b_width = xec ? xec->col_b_width : 60.0f;
    g_xlsx_border = xec ? xec->border : 1;
    snprintf(g_xlsx_sheet_name, sizeof(g_xlsx_sheet_name), "%s", xec ? xec->sheet_name : "Report");
    g_xlsx_flags = opts ? opts->flags : ~0;
    char xml[XML_BUF];
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_writer_init_heap(&zip, 0, 65536))
        return ERR_EXPORT;

    typedef size_t (*build_no_report)(char*,size_t);
    typedef size_t (*build_with_report)(char*,size_t,const Report*);
    typedef struct {
        const char *name;
        int need_report;
        union { build_no_report nr; build_with_report wr; } fn;
    } Entry;
    static const Entry entries[] = {
        {"[Content_Types].xml",       0, {.nr = build_content_types}},
        {"_rels/.rels",               0, {.nr = build_rels}},
        {"xl/workbook.xml",           0, {.nr = build_workbook}},
        {"xl/_rels/workbook.xml.rels",0, {.nr = build_wb_rels}},
        {"xl/styles.xml",             0, {.nr = build_styles}},
        {"xl/sharedStrings.xml",      1, {.wr = build_shared_strings}},
        {"xl/worksheets/sheet1.xml",  1, {.wr = build_sheet}},
    };
    int n_entries = sizeof(entries) / sizeof(entries[0]);

    for (int i = 0; i < n_entries; i++) {
        size_t len;
        if (entries[i].need_report) {
            len = entries[i].fn.wr(xml, sizeof(xml), report);
        } else {
            len = entries[i].fn.nr(xml, sizeof(xml));
        }
        if (len == 0 || len >= sizeof(xml))
            goto err;
        if (!mz_zip_writer_add_mem(&zip, entries[i].name, xml, len, MZ_DEFAULT_COMPRESSION))
            goto err;
    }

    void *buf = NULL;
    size_t buf_size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buf, &buf_size))
        goto err;

    mz_zip_writer_end(&zip);

    if (fwrite(buf, 1, buf_size, fp) != buf_size) {
        free(buf);
        return ERR_EXPORT;
    }
    free(buf);
    return GS_OK;

err:
    mz_zip_writer_end(&zip);
    return ERR_EXPORT;
}
