/* PDF export — object/xref layer: serializes the laid-out Doc plus the
 * (optionally embedded) CJK font into final PDF file bytes.
 * Object numbers are reserved up front so bodies may forward-reference. */
#include "pdf_int.h"
#include <time.h>

ErrorCode pdf_write_document(PdfW *w, const Doc *d, const CjkFont *cf,
                             int embed, const Report *report,
                             float pw, float ph)
{
    int n = d->npages;
    int nl = d->nlinks;
    int no = d->noutline;

    int f1  = pw_reserve(w);
    int f2  = pw_reserve(w);
    int f4  = pw_reserve(w);
    int f5  = pw_reserve(w);
    int f3  = pw_reserve(w);
    int cid = pw_reserve(w);
    int cmap = pw_reserve(w);
    int fd  = pw_reserve(w);
    int cidmapobj = 0, ffileobj = 0;
    if (embed) {
        cidmapobj = pw_reserve(w);
        ffileobj = pw_reserve(w);
    }
    int *streams = malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
    int *annots  = malloc((size_t)(nl > 0 ? nl : 1) * sizeof(int));
    int *pages   = malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
    int *outs    = malloc((size_t)(no > 0 ? no : 1) * sizeof(int));
    int oroot, pagesroot, info, catalog;
    if (!streams || !annots || !pages || !outs) {
        free(streams); free(annots); free(pages); free(outs);
        return ERR_EXPORT;
    }
    for (int i = 0; i < n; i++) streams[i] = pw_reserve(w);
    for (int i = 0; i < nl; i++) annots[i] = pw_reserve(w);
    for (int i = 0; i < n; i++) pages[i] = pw_reserve(w);
    for (int i = 0; i < no; i++) outs[i] = pw_reserve(w);
    oroot = pw_reserve(w);
    pagesroot = pw_reserve(w);
    info = pw_reserve(w);
    catalog = pw_reserve(w);

    db_puts(&w->buf, "%PDF-1.4\n");

    /* F1: Helvetica (WinAnsiEncoding -> cp1252 punctuation like em-dash) */
    {
        pw_record(w, f1);
        pw_printf(w, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica"
                  " /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding >>\nendobj\n", f1);
    }
    /* F2: Helvetica-Bold (same encoding) */
    {
        pw_record(w, f2);
        pw_printf(w, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold"
                  " /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding >>\nendobj\n", f2);
    }
    /* F4: Helvetica-Oblique (markdown *italic*) */
    {
        pw_record(w, f4);
        pw_printf(w, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique"
                  " /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding >>\nendobj\n", f4);
    }
    /* F5: Courier (markdown `inline code`) */
    {
        pw_record(w, f5);
        pw_printf(w, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier"
                  " /FirstChar 32 /LastChar 255 /Encoding /WinAnsiEncoding >>\nendobj\n", f5);
    }
    /* F3: CJK Type0 — embedded TrueType subset, else STSong-Light fallback */
    {
        pw_record(w, f3);
        pw_printf(w, "%d 0 obj\n<< /Type /Font /Subtype /Type0"
                  " /BaseFont /%s /Encoding /Identity-H"
                  " /DescendantFonts [%d 0 R] /ToUnicode %d 0 R >>\nendobj\n",
                  f3, embed ? "AAAAAA+GitSightCJK" : "STSong-Light", cid, cmap);
    }
    {
        pw_record(w, cid);
        if (embed) {
            pw_printf(w, "%d 0 obj\n<< /Type /Font /Subtype /CIDFontType2"
                      " /BaseFont /AAAAAA+GitSightCJK"
                      " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>"
                      " /FontDescriptor %d 0 R /CIDToGIDMap %d 0 R"
                      " /DW 1000 /W [32 126 500 %s] >>\nendobj\n",
                      cid, fd, cidmapobj,
                      cf->widths.data ? cf->widths.data : "");
        } else {
            pw_printf(w, "%d 0 obj\n<< /Type /Font /Subtype /CIDFontType2"
                      " /BaseFont /STSong-Light"
                      " /CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 5 >>"
                      " /FontDescriptor %d 0 R"
                      " /DW 1000 /W [32 126 500] >>\nendobj\n",
                      cid, fd);
        }
    }
    {
        pw_record(w, cmap);
        const char *cmapdata =
            "/CIDInit /ProcSet findresource begin\n"
            "12 dict begin\n"
            "begincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 5 >> def\n"
            "/CMapName /Adobe-Identity-UCS def\n"
            "/CMapType 2 def\n"
            "1 begincodespacerange\n"
            "<0000> <FFFF>\n"
            "endcodespacerange\n"
            "1 beginbfrange\n"
            "<0000> <FFFF> <0000>\n"
            "endbfrange\n"
            "endcmap\n"
            "CMapName currentdict /CMap defineresource pop\n"
            "end\n"
            "end\n";
        size_t clen = strlen(cmapdata);
        pw_printf(w, "%d 0 obj\n<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
                  cmap, clen, cmapdata);
    }
    {
        pw_record(w, fd);
        if (embed) {
            pw_printf(w, "%d 0 obj\n<< /Type /FontDescriptor /FontName /AAAAAA+GitSightCJK"
                      " /Flags 4 /FontBBox [%d %d %d %d]"
                      " /ItalicAngle 0 /Ascent %d /Descent %d"
                      " /CapHeight %d /StemV 80 /FontFile2 %d 0 R >>\nendobj\n",
                      fd, cf->bbox[0], cf->bbox[1], cf->bbox[2], cf->bbox[3],
                      cf->asc, cf->desc, cf->asc, ffileobj);
        } else {
            pw_printf(w, "%d 0 obj\n<< /Type /FontDescriptor /FontName /STSong-Light"
                      " /Flags 4 /FontBBox [0 0 1000 1000]"
                      " /ItalicAngle 0 /Ascent 1000 /Descent 0"
                      " /CapHeight 1000 /StemV 0 >>\nendobj\n",
                      fd);
        }
    }
    if (embed) {
        pw_record(w, ffileobj);
        pw_printf(w, "%d 0 obj\n<< /Length %zu /Filter /FlateDecode >>\nstream\n",
                  ffileobj, cf->font_len);
        db_mem(&w->buf, cf->font, cf->font_len);
        db_puts(&w->buf, "\nendstream\nendobj\n");

        pw_record(w, cidmapobj);
        pw_printf(w, "%d 0 obj\n<< /Length %zu /Filter /FlateDecode >>\nstream\n",
                  cidmapobj, cf->cidmap_len);
        db_mem(&w->buf, cf->cidmap, cf->cidmap_len);
        db_puts(&w->buf, "\nendstream\nendobj\n");
    }

    /* ---- Stream objects ---- */
    for (int i = 0; i < n; i++) {
        pw_record(w, streams[i]);
        pw_printf(w, "%d 0 obj\n<< /Length %zu >>\nstream\n",
                  streams[i], d->pages[i].len);
        db_puts(&w->buf, d->pages[i].data);
        db_puts(&w->buf, "\nendstream\nendobj\n");
    }

    /* ---- Link annotations ---- */
    for (int i = 0; i < nl; i++) {
        pw_record(w, annots[i]);
        LinkRec *l = &d->links[i];
        char url[MAX_URL_LEN * 2 + 8];
        escape_pdf_string(l->url, url, sizeof(url));
        pw_printf(w, "%d 0 obj\n<< /Type /Annot /Subtype /Link"
                  " /Rect [%.2f %.2f %.2f %.2f] /Border [0 0 0]"
                  " /A << /S /URI /URI (%s) >> /P %d 0 R >>\nendobj\n",
                  annots[i], l->x1, l->y1, l->x2, l->y2, url, pages[l->page]);
    }

    /* ---- Page objects ---- */
    for (int i = 0; i < n; i++) {
        pw_record(w, pages[i]);
        db_printf(&w->buf, "%d 0 obj\n<< /Type /Page /Parent %d 0 R"
                  " /MediaBox [0 0 %.0f %.0f] /Contents %d 0 R",
                  pages[i], pagesroot, pw, ph, streams[i]);
        if (nl > 0) {
            db_puts(&w->buf, " /Annots [");
            for (int j = 0; j < nl; j++)
                if (d->links[j].page == i) db_printf(&w->buf, "%d 0 R ", annots[j]);
            db_puts(&w->buf, "]");
        }
        db_printf(&w->buf, " /Resources << /Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R"
                  " /F4 %d 0 R /F5 %d 0 R >> >>"
                  " >>\nendobj\n", f1, f2, f3, f4, f5);
    }

    /* ---- Outline entries ---- */
    for (int i = 0; i < no; i++) {
        pw_record(w, outs[i]);
        db_printf(&w->buf, "%d 0 obj\n<< /Title (", outs[i]);
        for (const char *t = d->otitles[i]; *t; t++) {
            if (*t == '(' || *t == ')' || *t == '\\') db_putc(&w->buf, '\\');
            db_putc(&w->buf, *t);
        }
        db_printf(&w->buf, ") /Parent %d 0 R", oroot);
        if (i > 0) db_printf(&w->buf, " /Prev %d 0 R", outs[i - 1]);
        if (i < no - 1) db_printf(&w->buf, " /Next %d 0 R", outs[i + 1]);
        db_printf(&w->buf, " /Dest [%d 0 R /XYZ 0 %.0f 0] >>\nendobj\n",
                  pages[d->opages[i]], ph);
    }

    /* ---- Outlines root ---- */
    {
        pw_record(w, oroot);
        db_printf(&w->buf, "%d 0 obj\n<< /Type /Outlines", oroot);
        if (no > 0)
            db_printf(&w->buf, " /First %d 0 R /Last %d 0 R /Count %d",
                      outs[0], outs[no - 1], no);
        db_puts(&w->buf, " >>\nendobj\n");
    }

    /* ---- Pages tree ---- */
    {
        pw_record(w, pagesroot);
        db_printf(&w->buf, "%d 0 obj\n<< /Type /Pages /Kids [", pagesroot);
        for (int i = 0; i < n; i++) db_printf(&w->buf, "%d 0 R ", pages[i]);
        db_printf(&w->buf, "] /Count %d >>\nendobj\n", n);
    }

    /* ---- Info ---- */
    {
        pw_record(w, info);
        time_t t = time(NULL);
        struct tm *tmv = gmtime(&t);
        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "D:%04d%02d%02d%02d%02d%02d",
                 tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                 tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
        pw_printf(w, "%d 0 obj\n<< /Title (GitSight Report - %s/%s)"
                  " /Author (GitSight) /Creator (GitSight) /Producer (GitSight)"
                  " /CreationDate (%s) >>\nendobj\n",
                  info, report->owner, report->repo, dbuf);
    }

    /* ---- Catalog ---- */
    {
        pw_record(w, catalog);
        pw_printf(w, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R /Outlines %d 0 R >>\nendobj\n",
                  catalog, pagesroot, oroot);
    }

    /* ---- xref ---- */
    pw_finish(w, catalog, info);

    free(streams);
    free(annots);
    free(pages);
    free(outs);
    return GS_OK;
}
