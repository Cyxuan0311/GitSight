/* PDF export — assembles the report into a paginated layout document and
 * delegates serialization to the object/xref layer.
 * Shared internals: pdf_int.h; implementation split across
 * pdf_util.c (buffers/text metrics), pdf_font.c (CJK subsetting/embedding),
 * pdf_write.c (content-stream ops + file writer), pdf_doc.c (page layout),
 * pdf_objects.c (PDF object/xref serialization). */
#include "gitsight/export/pdf.h"
#include "gitsight/i18n.h"
#include "pdf_int.h"

/* ---- Export ---- */
ErrorCode export_pdf(FILE *fp, const Report *report,
                     const Vector *prs, const Vector *commits,
                     const Vector *issues, const Vector *releases,
                     const FormatOptions *opts) {
    cjk_reset();
    int uc = opts ? opts->color_active : 0;
    float pw = opts && opts->page_width  > 0 ? opts->page_width  : 595.0f;
    float ph = opts && opts->page_height > 0 ? opts->page_height : 842.0f;
    float fs = opts && opts->font_size   > 0 ? opts->font_size   : 11.0f;

    const PdfExportConfig *pec = (opts && opts->extra) ? (const PdfExportConfig *)opts->extra : NULL;
    float mg = pec ? pec->margin : 50.0f;
    if (mg < 10) mg = 10;
    float lh_factor = pec ? pec->line_height : 1.4f;
    float c2pad = pec ? pec->col2_padding : 12.0f;

    Doc d;
    memset(&d, 0, sizeof(d));
    d.pw = pw;
    d.ph = ph;
    d.mg = mg;
    d.fs = fs;
    d.lh = fs * lh_factor;
    d.bot = mg + d.lh * 1.8f;
    d.usable = pw - 2 * mg;
    d.col1 = mg;
    d.uc = uc;
    d.report = report;
    d.tcol = pec ? (PdfColor){pec->title_color[0], pec->title_color[1], pec->title_color[2]} : PC_BLUE;
    d.hcol = pec ? (PdfColor){pec->header_color[0], pec->header_color[1], pec->header_color[2]} : PC_BLUE;
    d.lcol = pec ? (PdfColor){pec->label_color[0], pec->label_color[1], pec->label_color[2]} : PC_GRAY;
    d.linkcol   = pec ? (PdfColor){pec->link_color[0], pec->link_color[1], pec->link_color[2]} : PC_LINK;
    d.opencol   = pec ? (PdfColor){pec->open_color[0], pec->open_color[1], pec->open_color[2]} : PC_OPEN;
    d.closedcol = pec ? (PdfColor){pec->closed_color[0], pec->closed_color[1], pec->closed_color[2]} : PC_CLOSED;
    d.mergedcol = pec ? (PdfColor){pec->merged_color[0], pec->merged_color[1], pec->merged_color[2]} : PC_MERGED;

    /* value column: left edge = widest label + padding */
    {
        const char *lbls[] = {
            gs_t("Total"), gs_t("Merged"), gs_t("Open"), gs_t("Closed"),
            gs_t("Authors"), gs_t("Top Author"), gs_t("Latest release"),
            gs_t("Top contributor"), gs_t("Default branch"), gs_t("Branch"),
            gs_t("Stars"), gs_t("Forks"), gs_t("Language"), gs_t("License"),
            gs_t("Avg merge"), gs_t("Avg close"),
            NULL
        };
        float mx = 0;
        for (int i = 0; lbls[i]; i++) {
            float w = str_width(lbls[i], fs);
            if (w > mx) mx = w;
        }
        d.col2 = d.col1 + mx + c2pad;
    }

    int sec_flags = opts ? opts->flags : ~0;
    int limit = opts && opts->item_limit > 0 ? opts->item_limit : 10;

    /* ---- Title ---- */
    doc_ensure(&d, d.lh * 2.2f);
    {
        char title[1024];
        snprintf(title, sizeof(title), "%s - %s/%s", gs_t("GitSight Report"), report->owner, report->repo);
        float tw = str_width(title, fs * 1.6f);
        char tbuf[1024];
        if (tw > d.usable) {
            ellipsize(title, d.usable, fs * 1.6f, tbuf, sizeof(tbuf));
            tw = d.usable;
        } else {
            snprintf(tbuf, sizeof(tbuf), "%s", title);
        }
        const PdfColor *tc = uc ? &d.tcol : NULL;
        doc_text(&d, mg + (d.usable - tw) / 2, d.y, tbuf, "F2", fs * 1.6f, tc);
        d.y -= d.lh * 1.8f;
    }

    /* ---- Period ---- */
    doc_ensure(&d, d.lh * 1.5f);
    {
        char pbuf[256];
        snprintf(pbuf, sizeof(pbuf), gs_t("Period: %s"), report->period);
        float tw = str_width(pbuf, fs);
        doc_text(&d, mg + (d.usable - tw) / 2, d.y, pbuf, "F1", fs, NULL);
        d.y -= d.lh * 1.2f;
    }

    /* ---- Repository metadata ---- */
    if (report->meta.has_meta) {
        doc_section(&d, gs_t("[META] Repository"), d.lh * 8);
        char buf[128];
        fmt_int(buf, sizeof(buf), report->meta.stars);
        doc_kv(&d, gs_t("Stars"), buf);
        fmt_int(buf, sizeof(buf), report->meta.forks);
        doc_kv(&d, gs_t("Forks"), buf);
        if (report->meta.language[0]) doc_kv(&d, gs_t("Language"), report->meta.language);
        if (report->meta.license[0])  doc_kv(&d, gs_t("License"), report->meta.license);
        if (report->meta.default_branch[0]) doc_kv(&d, gs_t("Default branch"), report->meta.default_branch);
        if (report->branch[0]) doc_kv(&d, gs_t("Branch"), report->branch);
        d.y -= d.lh;
    }

    /* ---- Separator ---- */
    ct_line(doc_cur(&d), mg, d.y, pw - mg, d.y);
    d.y -= d.lh;

    /* ---- Section: Pull Requests ---- */
    if (sec_flags & FMT_SHOW_PRS) {
        doc_section(&d, gs_t("[PR] Pull Requests"), d.lh * 6);
        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->pr_total);
        doc_kv(&d, gs_t("Total"), buf);
        snprintf(buf, sizeof(buf), "%d", report->pr_merged);
        doc_kv_c(&d, gs_t("Merged"), buf, &d.mergedcol);
        snprintf(buf, sizeof(buf), "%d", report->pr_open);
        doc_kv_c(&d, gs_t("Open"), buf, &d.opencol);
        snprintf(buf, sizeof(buf), "%d", report->pr_closed);
        doc_kv_c(&d, gs_t("Closed"), buf, &d.closedcol);
        if (report->pr_merged_count > 0) {
            char tbuf[64];
            snprintf(tbuf, sizeof(tbuf), gs_t("%.1f h"), report->pr_avg_merge_hours);
            doc_kv(&d, gs_t("Avg merge"), tbuf);
        }
        d.y -= d.lh;
        if (limit > 0 && prs) {
            size_t n = vector_len(prs);
            if (n > (size_t)limit) n = (size_t)limit;
            for (size_t i = 0; i < n; i++) {
                PR *pr = (PR *)vector_get(prs, i);
                char row[1024];
                snprintf(row, sizeof(row), "#%d %s", pr->number, pr->title);
                doc_item_row(&d, row, pr->state, pr->html_url);
            }
        }
        d.y -= d.lh * 0.4f;
    }

    /* ---- Section: Commits ---- */
    if (sec_flags & FMT_SHOW_COMMITS) {
        doc_section(&d, gs_t("[CM] Commits"), d.lh * 6);
        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->commit_total);
        doc_kv(&d, gs_t("Total"), buf);
        snprintf(buf, sizeof(buf), "%d", report->author_total);
        doc_kv(&d, gs_t("Authors"), buf);
        if (report->top_author[0]) doc_kv(&d, gs_t("Top Author"), report->top_author);
        d.y -= d.lh;

        if (report->activity_count > 1 && report->activity_max > 0) {
            doc_ensure(&d, d.lh * 2);
            doc_text(&d, d.col1, d.y, gs_t("Activity (commits/day):"), "F1", fs, &d.lcol);
            d.y -= d.lh;
            const PdfColor *bar = uc ? &d.hcol : &PC_BLACK;
            for (int i = report->activity_count - 1; i >= 0; i--) {
                doc_hbar(&d, report->activity_labels[i], report->activity_values[i],
                         report->activity_max, bar);
            }
            d.y -= d.lh * 0.4f;
        }

        if (report->churn_available &&
            report->churn_additions + report->churn_deletions > 0) {
            snprintf(buf, sizeof(buf), gs_t("+%ld / -%ld lines"),
                     report->churn_additions, report->churn_deletions);
            doc_kv(&d, gs_t("Code churn"), buf);
            d.y -= d.lh * 0.2f;
        }

        int mx = 0;
        for (int i = 0; i < 7; i++)
            if (report->weekday_values[i] > mx) mx = report->weekday_values[i];
        if (mx > 0) {
            float chart_h = d.lh * 4.5f;
            doc_ensure(&d, d.lh + chart_h + d.fs * 1.6f);
            doc_text(&d, d.col1, d.y, gs_t("Commits by weekday:"), "F1", fs, &d.lcol);
            d.y -= d.lh;
            const char *wd[7];
            for (int i = 0; i < 7; i++) wd[i] = gs_weekday(i);
            const PdfColor *bar = uc ? &d.hcol : &PC_BLACK;
            doc_vbar(&d, wd, report->weekday_values, 7, mx, d.usable, chart_h, bar);
            d.y -= d.lh * 0.6f;
        }

        if (limit > 0 && commits) {
            size_t n = vector_len(commits);
            if (n > (size_t)limit) n = (size_t)limit;
            for (size_t i = 0; i < n; i++) {
                Commit *c = (Commit *)vector_get(commits, i);
                char row[2500];
                snprintf(row, sizeof(row), "%s %s (%s)", c->sha, c->message, c->author);
                doc_item_row(&d, row, NULL, c->html_url);
            }
        }
        d.y -= d.lh * 0.4f;
    }

    /* ---- Section: Issues ---- */
    if (sec_flags & FMT_SHOW_ISSUES) {
        doc_section(&d, gs_t("[IS] Issues"), d.lh * 6);
        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->issue_total);
        doc_kv(&d, gs_t("Total"), buf);
        snprintf(buf, sizeof(buf), "%d", report->issue_open);
        doc_kv_c(&d, gs_t("Open"), buf, &d.opencol);
        snprintf(buf, sizeof(buf), "%d", report->issue_closed);
        doc_kv_c(&d, gs_t("Closed"), buf, &d.closedcol);
        if (report->issue_closed_count > 0) {
            char tbuf[64];
            snprintf(tbuf, sizeof(tbuf), gs_t("%.1f h"), report->issue_avg_close_hours);
            doc_kv(&d, gs_t("Avg close"), tbuf);
        }
        d.y -= d.lh;
        if (limit > 0 && issues) {
            size_t n = vector_len(issues);
            if (n > (size_t)limit) n = (size_t)limit;
            for (size_t i = 0; i < n; i++) {
                Issue *iss = (Issue *)vector_get(issues, i);
                char row[1024];
                snprintf(row, sizeof(row), "#%d %s", iss->number, iss->title);
                doc_item_row(&d, row, iss->state, iss->html_url);
            }
        }
        d.y -= d.lh * 0.4f;
    }

    /* ---- Section: Releases ---- */
    if ((sec_flags & FMT_SHOW_RELEASES) && report->release_total > 0) {
        doc_section(&d, gs_t("[RL] Releases"), d.lh * 6);
        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->release_total);
        doc_kv(&d, gs_t("Total"), buf);
        if (report->latest_release[0]) doc_kv(&d, gs_t("Latest release"), report->latest_release);
        d.y -= d.lh;
        if (limit > 0 && releases) {
            size_t n = vector_len(releases);
            if (n > (size_t)limit) n = (size_t)limit;
            for (size_t i = 0; i < n; i++) {
                Release *rel = (Release *)vector_get(releases, i);
                char name[MAX_TITLE_LEN];
                snprintf(name, sizeof(name), "%s", rel->name[0] ? rel->name : rel->tag_name);
                char row[1024];
                snprintf(row, sizeof(row), "%s  %s  (%s)", rel->tag_name, name, rel->author);
                doc_item_row(&d, row, NULL, rel->html_url);
            }
        }
        d.y -= d.lh * 0.4f;
    }

    /* ---- Section: Contributors ---- */
    if ((sec_flags & FMT_SHOW_CONTRIBUTORS) && report->contributor_total > 0) {
        doc_section(&d, gs_t("[CT] Contributors"), d.lh * 6);
        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->contributor_total);
        doc_kv(&d, gs_t("Total"), buf);
        if (report->top_contributor[0]) doc_kv(&d, gs_t("Top contributor"), report->top_contributor);
        d.y -= d.lh;
        if (report->top_contributor_list[0])
            doc_wrapped(&d, report->top_contributor_list, "F1", fs, NULL);
        d.y -= d.lh * 0.4f;
    }

    /* ---- Section: Languages ---- */
    if ((sec_flags & FMT_SHOW_LANGS) && report->lang_count > 0) {
        long long total = 0;
        for (int i = 0; i < report->lang_count; i++) total += report->lang_bytes[i];
        doc_section(&d, gs_t("[LG] Languages"), d.lh * (2 + report->lang_count));
        const PdfColor *bar = uc ? &d.hcol : &PC_BLACK;
        for (int i = 0; i < report->lang_count; i++) {
            double pct = total > 0 ? 100.0 * report->lang_bytes[i] / total : 0.0;
            char vbuf[32];
            snprintf(vbuf, sizeof(vbuf), "%.1f%%", pct);
            doc_hbar_txt(&d, report->lang_names[i], vbuf, (float)(pct / 100.0), bar);
        }
        d.y -= d.lh * 0.4f;
    }

    /* ---- Top labels ---- */
    if ((sec_flags & FMT_SHOW_LABELS) && report->top_labels[0]) {
        char labels[1032];
        snprintf(labels, sizeof(labels), gs_t("Labels: %s"), report->top_labels);
        if (str_width(labels, fs) > d.usable) {
            doc_wrapped(&d, labels, "F1", fs, NULL);
            d.y -= d.lh * 0.3f;
        } else {
            doc_ensure(&d, d.lh);
            doc_text(&d, d.col1, d.y, labels, "F1", fs, NULL);
            d.y -= d.lh * 1.2f;
        }
    }

    /* ---- Section: Summary ---- */
    if (sec_flags & FMT_SHOW_SUMMARY) {
        doc_section(&d, gs_t("[*] Summary"), d.lh * 2);
        doc_md(&d, report->summary, fs);
    }

    /* ---- Footer on every page ---- */
    {
        float ly = mg + fs * 1.8f;
        float ty = mg + fs * 1.0f;
        char lbuf[320];
        snprintf(lbuf, sizeof(lbuf), "%s - %s/%s", gs_t("GitSight Report"), report->owner, report->repo);
        for (int i = 0; i < d.npages; i++) {
            DynBuf *ct = &d.pages[i];
            ct_line(ct, mg, ly, pw - mg, ly);
            ct_text(ct, mg, ty, lbuf, "F1", fs * 0.8f, &d.lcol);
            char pbuf[64];
            snprintf(pbuf, sizeof(pbuf), gs_t("Page %d of %d"), i + 1, d.npages);
            float lw = str_width(pbuf, fs * 0.8f);
            ct_text(ct, pw - mg - lw, ty, pbuf, "F1", fs * 0.8f, &d.lcol);
        }
    }

    /* ---- CJK font embedding decision ---- */
    CjkFont cf;
    memset(&cf, 0, sizeof(cf));
    int embed = 0;
    if (cjk_any_used()) {
        embed = cjk_build(&cf);
        if (!embed)
            fprintf(stderr, "warning: no embeddable CJK TrueType font found; "
                    "set GITSIGHT_PDF_FONT=/path/to/font.ttf\n");
    }

    /* ---- Write PDF objects ---- */
    PdfW w;
    pw_init(&w);
    ErrorCode werr = pdf_write_document(&w, &d, &cf, embed, report, pw, ph);

    int ok = werr == GS_OK;
    if (ok && fwrite(w.buf.data, 1, w.buf.len, fp) != w.buf.len) ok = 0;

    pw_free(&w);
    cjkfont_free(&cf);
    for (int i = 0; i < d.npages; i++) db_free(&d.pages[i]);
    free(d.pages);
    free(d.links);
    free(d.otitles);
    free(d.opages);

    return ok ? GS_OK : ERR_EXPORT;
}
