#include "gitsight/export/pdf.h"
#include "gitsight/export/exporter.h"
#include "gitsight/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>

/* ---- Dynamic Buffer ---- */
typedef struct { char *data; size_t len, cap; } DynBuf;

static void db_init(DynBuf *b) { memset(b, 0, sizeof(*b)); }
static void db_free(DynBuf *b) { free(b->data); }

static int db_grow(DynBuf *b, size_t need) {
    if (b->len + need <= b->cap) return 1;
    size_t nc = b->cap ? b->cap : 4096;
    while (nc < b->len + need) nc *= 2;
    char *p = realloc(b->data, nc);
    if (!p) return 0;
    b->data = p;
    b->cap = nc;
    return 1;
}

static void db_puts(DynBuf *b, const char *s) {
    size_t n = strlen(s);
    if (db_grow(b, n + 1)) { memcpy(b->data + b->len, s, n); b->len += n; }
}

static void db_putc(DynBuf *b, char c) {
    if (db_grow(b, 1)) b->data[b->len++] = c;
}

__attribute__((format(printf,2,3)))
static void db_printf(DynBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (!db_grow(b, (size_t)n + 2)) return;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += n;
}

/* ---- PDF Color ---- */
typedef struct { unsigned char r, g, b; } PdfColor;

static const PdfColor PC_BLACK = {0, 0, 0};
static const PdfColor PC_BLUE  = {25, 55, 160};
static const PdfColor PC_GRAY  = {90, 90, 90};

/* ---- UTF-8 helpers ---- */
/* Decode one UTF-8 codepoint from s; advance s past it; return codepoint */
static uint32_t utf8_decode(const unsigned char **sp) {
    const unsigned char *s = *sp;
    uint32_t cp;
    if (*s < 0x80) {
        cp = *s++;
    } else if ((*s & 0xE0) == 0xC0) {
        cp = (*s++ & 0x1F) << 6;
        cp |= *s++ & 0x3F;
    } else if ((*s & 0xF0) == 0xE0) {
        cp = (*s++ & 0x0F) << 12;
        cp |= (*s++ & 0x3F) << 6;
        cp |= *s++ & 0x3F;
    } else if ((*s & 0xF8) == 0xF0) {
        cp = (*s++ & 0x07) << 18;
        cp |= (*s++ & 0x3F) << 12;
        cp |= (*s++ & 0x3F) << 6;
        cp |= *s++ & 0x3F;
    } else {
        s++;
        cp = 0xFFFD;
    }
    *sp = s;
    return cp;
}

/* approximate width of one CP in Helvetica at font size fs */
static float cp_width(uint32_t cp, float fs) {
    if (cp == ' ') return fs * 0.28f;
    if (cp <= 0x7F) {
        if (cp == 'I') return fs * 0.25f;
        if (cp == 'M' || cp == 'W') return fs * 0.72f;
        if (cp >= 'A' && cp <= 'Z') return fs * 0.60f;
        if (cp >= 'a' && cp <= 'z') return fs * 0.50f;
        if (cp >= '0' && cp <= '9') return fs * 0.55f;
        return fs * 0.50f;
    }
    if (cp <= 0xFFFF) return fs;
    return fs * 1.5f;
}

/* ---- Content Stream Builder ---- */
static void ct_printf(DynBuf *ct, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (!db_grow(ct, (size_t)n + 2)) return;
    va_start(ap, fmt);
    vsnprintf(ct->data + ct->len, ct->cap - ct->len, fmt, ap);
    va_end(ap);
    ct->len += n;
}

static void ct_color(DynBuf *ct, PdfColor c) {
    ct_printf(ct, "%.3f %.3f %.3f rg\n", c.r / 255.0, c.g / 255.0, c.b / 255.0);
}

static void ct_font(DynBuf *ct, const char *key, float sz) {
    ct_printf(ct, "/%s %f Tf\n", key, sz);
}

static int has_non_ascii(const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p > 126) return 1;
    return 0;
}

static void ct_text(DynBuf *ct, float x, float y, const char *text,
                    const char *font, float sz, const PdfColor *color)
{
    ct_printf(ct, "BT\n");
    if (color) ct_color(ct, *color); else ct_color(ct, PC_BLACK);

    if (has_non_ascii(text)) {
        ct_font(ct, "F3", sz);
        ct_printf(ct, "1 0 0 1 %.2f %.2f Tm\n", x, y);
        db_putc(ct, '<');
        const unsigned char *p = (const unsigned char *)text;
        while (*p) {
            uint32_t cp = utf8_decode(&p);
            if (cp <= 0xFFFF)
                ct_printf(ct, "%04X", (unsigned int)cp);
            else {
                uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
                uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
                ct_printf(ct, "%04X%04X", hi, lo);
            }
        }
        db_putc(ct, '>');
    } else {
        ct_font(ct, font, sz);
        ct_printf(ct, "1 0 0 1 %.2f %.2f Tm\n", x, y);
        db_putc(ct, '(');
        for (const unsigned char *p = (const void*)text; *p; p++) {
            if (*p == '(' || *p == ')' || *p == '\\')
                ct_printf(ct, "\\%c", *p);
            else if (*p < 32)
                ct_printf(ct, "\\%03o", *p);
            else
                db_putc(ct, *p);
        }
        db_putc(ct, ')');
    }
    ct_printf(ct, " Tj\nET\n");
}

static void ct_line(DynBuf *ct, float x1, float y1, float x2, float y2) {
    ct_printf(ct, "%.2f %.2f m %.2f %.2f l S\n", x1, y1, x2, y2);
}

/* approximate string width at given font size */
static float str_width(const char *s, float fs) {
    float w = 0;
    const unsigned char *p = (const void*)s;
    while (*p) {
        uint32_t cp = utf8_decode(&p);
        w += cp_width(cp, fs);
    }
    return w;
}

/* ---- PDF Writer ---- */
typedef struct {
    DynBuf  buf;
    size_t *offsets;
    int     nobjs;
    int     ocap;
} PdfW;

static void pw_init(PdfW *pw) {
    memset(pw, 0, sizeof(*pw));
}

static void pw_free(PdfW *pw) {
    db_free(&pw->buf);
    free(pw->offsets);
}

static int pw_alloc(PdfW *pw) {
    if (pw->nobjs >= pw->ocap) {
        int nc = pw->ocap ? pw->ocap * 2 : 32;
        size_t *p = realloc(pw->offsets, (size_t)nc * sizeof(size_t));
        if (!p) return -1;
        pw->offsets = p;
        pw->ocap = nc;
    }
    pw->offsets[pw->nobjs] = pw->buf.len;
    return pw->nobjs++;
}

static void pw_printf(PdfW *pw, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (!db_grow(&pw->buf, (size_t)n + 2)) return;
    va_start(ap, fmt);
    vsnprintf(pw->buf.data + pw->buf.len, pw->buf.cap - pw->buf.len, fmt, ap);
    va_end(ap);
    pw->buf.len += n;
}

/* Write final xref table and trailer */
static void pw_finish(PdfW *pw, int root_obj) {
    size_t xref_off = pw->buf.len;
    db_printf(&pw->buf, "xref\n%d %d\n", 0, pw->nobjs);
    /* entry 0 (free) */
    db_puts(&pw->buf, "0000000000 65535 f \n");
    for (int i = 1; i < pw->nobjs; i++) {
        db_printf(&pw->buf, "%010zu 00000 n \n", pw->offsets[i]);
    }

    db_printf(&pw->buf,
        "trailer\n<< /Size %d /Root %d 0 R >>\n"
        "startxref\n%zu\n%%%%EOF\n",
        pw->nobjs, root_obj, xref_off);
}

/* ---- Layout Helpers ---- */

/* add word-wrapped text (ASCII) or character-wrapped (CJK) text to content stream; returns new y */
static float add_wrapped(DynBuf *ct, float x, float y, const char *text,
                         const char *font, float sz, float lh, const PdfColor *color,
                         float max_w, float margin_b)
{
    const char *p = text;
    const char *line_start = p;
    const char *last_space = NULL;
    float line_w = 0;

    while (*p) {
        const unsigned char *up = (const void*)p;
        uint32_t cp = utf8_decode(&up);
        size_t clen = (size_t)((const char*)up - p);
        float cw = cp_width(cp, sz);

        if (cp == ' ') {
            last_space = p;
        }

        if (line_w + cw > max_w && line_w > 0) {
            const char *break_at;
            if (last_space && last_space >= line_start) {
                break_at = last_space;
            } else {
                break_at = p;
            }
            size_t len = (size_t)(break_at - line_start);
            char saved = line_start[len];
            *((char*)line_start + len) = '\0';
            ct_text(ct, x, y, line_start, font, sz, color);
            *((char*)line_start + len) = saved;

            y -= lh;
            if (y < margin_b + lh) return y;

            if (last_space && last_space >= line_start)
                p = break_at + 1;
            else
                p = break_at;
            while (*p == ' ') p++;
            line_start = p;
            line_w = 0;
            last_space = NULL;
        } else {
            line_w += cw;
            p += clen;
        }
    }

    if (p > line_start) {
        ct_text(ct, x, y, line_start, font, sz, color);
    }

    return y;
}

/* ---- Export ---- */
ErrorCode export_pdf(FILE *fp, const Report *report,
                     const Vector *prs, const Vector *commits,
                     const Vector *issues, const Vector *releases,
                     const FormatOptions *opts) {
    (void)prs; (void)commits; (void)issues; (void)releases;
    int uc = opts ? opts->color_active : 0;
    float pw = opts && opts->page_width  > 0 ? opts->page_width  : 595.0f;
    float ph = opts && opts->page_height > 0 ? opts->page_height : 842.0f;
    float fs = opts && opts->font_size   > 0 ? opts->font_size   : 11.0f;

    const PdfExportConfig *pec = (opts && opts->extra) ? (const PdfExportConfig *)opts->extra : NULL;
    float mg = pec ? pec->margin : 50.0f;
    float lh_factor = pec ? pec->line_height : 1.4f;
    float c2pad = pec ? pec->col2_padding : 12.0f;
    PdfColor tcol = pec ? (PdfColor){pec->title_color[0], pec->title_color[1], pec->title_color[2]} : PC_BLUE;
    PdfColor hcol = pec ? (PdfColor){pec->header_color[0], pec->header_color[1], pec->header_color[2]} : PC_BLUE;
    PdfColor lcol = pec ? (PdfColor){pec->label_color[0], pec->label_color[1], pec->label_color[2]} : PC_GRAY;

    float usable = pw - 2 * mg;
    float col1 = mg;
    float col2;
    {
        const char *lbls[] = {"Total", "Merged", "Open", "Closed", "Authors", "Top Author", "Latest release", "Top contributor", "Default branch"};
        float mx = 0;
        for (int i = 0; i < 9; i++) { float w = str_width(lbls[i], fs); if (w > mx) mx = w; }
        col2 = col1 + mx + c2pad;
    }
    float lh = fs * lh_factor;
    float y = ph - mg;
    int sec_flags = opts ? opts->flags : ~0;

    /* Build content streams across pages */
    DynBuf pages[8];
    int npages = 0;
    memset(pages, 0, sizeof(pages));

    /* ---- helper: start new page stream ---- */
#define NEW_PAGE() do { \
    if (npages >= 8) goto done; \
    db_init(&pages[npages]); \
    y = ph - mg; \
    npages++; \
} while(0)

    /* helper: check if we need a new page before adding content */
#define CHECK_PAGE(needed) do { \
    if (npages == 0) NEW_PAGE(); \
    if (y - (needed) < mg) { NEW_PAGE(); } \
} while(0)

    /* Page 1 content */
    NEW_PAGE();
    DynBuf *ct = &pages[npages - 1];

    /* Title */
    {
        char title[1024];
        snprintf(title, sizeof(title), "GitSight Report - %s/%s", report->owner, report->repo);
        float tw = str_width(title, fs * 1.6f);
        float tx = mg + (usable - tw) / 2;
        const PdfColor *tc = uc ? &tcol : NULL;
        ct_text(ct, tx, y, title, "F2", fs * 1.6f, tc);
        y -= lh * 1.8f;
    }

    /* Period */
    {
        char pbuf[256];
        snprintf(pbuf, sizeof(pbuf), "Period: %s", report->period);
        float tw = str_width(pbuf, fs);
        float tx = mg + (usable - tw) / 2;
        ct_text(ct, tx, y, pbuf, "F1", fs, NULL);
        y -= lh * 1.2f;
    }

    /* ---- Repository metadata ---- */
    if (report->meta.has_meta) {
        CHECK_PAGE(lh * 8 + 10);
        ct = &pages[npages - 1];
        const PdfColor *hc2 = uc ? &hcol : NULL;
        ct_text(ct, col1, y, "[META] Repository", "F2", fs * 1.15f, hc2);
        y -= lh * 1.4f;

        char buf[128];
        snprintf(buf, sizeof(buf), "%ld", report->meta.stars);
        ct_text(ct, col1, y, "Stars", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        snprintf(buf, sizeof(buf), "%ld", report->meta.forks);
        ct_text(ct, col1, y, "Forks", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        if (report->meta.language[0]) {
            ct_text(ct, col1, y, "Language", "F1", fs, &lcol);
            ct_text(ct, col2, y, report->meta.language, "F1", fs, NULL);
            y -= lh;
        }
        if (report->meta.license[0]) {
            ct_text(ct, col1, y, "License", "F1", fs, &lcol);
            ct_text(ct, col2, y, report->meta.license, "F1", fs, NULL);
            y -= lh;
        }
        if (report->meta.default_branch[0]) {
            ct_text(ct, col1, y, "Default branch", "F1", fs, &lcol);
            ct_text(ct, col2, y, report->meta.default_branch, "F1", fs, NULL);
            y -= lh;
        }
        y -= lh;
    }

    /* Separator line */
    ct_line(ct, mg, y, pw - mg, y);
    y -= lh;

    /* ---- Section: Pull Requests ---- */
    if (sec_flags & FMT_SHOW_PRS) {
    CHECK_PAGE(lh * 5 + 10);
    ct = &pages[npages - 1];
    {
        const PdfColor *hc2 = uc ? &hcol : NULL;
        ct_text(ct, col1, y, "[PR] Pull Requests", "F2", fs * 1.15f, hc2);
        y -= lh * 1.4f;

        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->pr_total);
        ct_text(ct, col1, y, "Total", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        snprintf(buf, sizeof(buf), "%d", report->pr_merged);
        ct_text(ct, col1, y, "Merged", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        snprintf(buf, sizeof(buf), "%d", report->pr_open);
        ct_text(ct, col1, y, "Open", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        snprintf(buf, sizeof(buf), "%d", report->pr_closed);
        ct_text(ct, col1, y, "Closed", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;
        y -= lh;
    }
    }

    /* ---- Section: Commits ---- */
    if (sec_flags & FMT_SHOW_COMMITS) {
    CHECK_PAGE(lh * 5 + 10);
    ct = &pages[npages - 1];
    {
        const PdfColor *hc2 = uc ? &hcol : NULL;
        ct_text(ct, col1, y, "[CM] Commits", "F2", fs * 1.15f, hc2);
        y -= lh * 1.4f;

        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->commit_total);
        ct_text(ct, col1, y, "Total", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        snprintf(buf, sizeof(buf), "%d", report->author_total);
        ct_text(ct, col1, y, "Authors", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        if (report->top_author[0]) {
            ct_text(ct, col1, y, "Top Author", "F1", fs, &lcol);
            ct_text(ct, col2, y, report->top_author, "F1", fs, NULL);
            y -= lh;
        }
        y -= lh;
        y -= lh;
    }
    }

    /* ---- Section: Issues ---- */
    if (sec_flags & FMT_SHOW_ISSUES) {
    CHECK_PAGE(lh * 5 + 10);
    ct = &pages[npages - 1];
    {
        const PdfColor *hc2 = uc ? &hcol : NULL;
        ct_text(ct, col1, y, "[IS] Issues", "F2", fs * 1.15f, hc2);
        y -= lh * 1.4f;

        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->issue_total);
        ct_text(ct, col1, y, "Total", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        snprintf(buf, sizeof(buf), "%d", report->issue_open);
        ct_text(ct, col1, y, "Open", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        snprintf(buf, sizeof(buf), "%d", report->issue_closed);
        ct_text(ct, col1, y, "Closed", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;
        y -= lh;
    }
    }

    /* ---- Section: Releases ---- */
    if ((sec_flags & FMT_SHOW_RELEASES) && report->release_total > 0) {
    CHECK_PAGE(lh * 5 + 10);
    ct = &pages[npages - 1];
    {
        const PdfColor *hc2 = uc ? &hcol : NULL;
        ct_text(ct, col1, y, "[RL] Releases", "F2", fs * 1.15f, hc2);
        y -= lh * 1.4f;

        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->release_total);
        ct_text(ct, col1, y, "Total", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        if (report->latest_release[0]) {
            ct_text(ct, col1, y, "Latest release", "F1", fs, &lcol);
            ct_text(ct, col2, y, report->latest_release, "F1", fs, NULL);
            y -= lh;
        }
        y -= lh;
    }
    }

    /* ---- Section: Contributors ---- */
    if ((sec_flags & FMT_SHOW_CONTRIBUTORS) && report->contributor_total > 0) {
    CHECK_PAGE(lh * 5 + 10);
    ct = &pages[npages - 1];
    {
        const PdfColor *hc2 = uc ? &hcol : NULL;
        ct_text(ct, col1, y, "[CT] Contributors", "F2", fs * 1.15f, hc2);
        y -= lh * 1.4f;

        char buf[128];
        snprintf(buf, sizeof(buf), "%d", report->contributor_total);
        ct_text(ct, col1, y, "Total", "F1", fs, &lcol);
        ct_text(ct, col2, y, buf, "F1", fs, NULL);
        y -= lh;

        if (report->top_contributor[0]) {
            ct_text(ct, col1, y, "Top contributor", "F1", fs, &lcol);
            ct_text(ct, col2, y, report->top_contributor, "F1", fs, NULL);
            y -= lh;
        }
        y -= lh;
    }
    }

    /* Top labels */
    if ((sec_flags & FMT_SHOW_LABELS) && report->top_labels[0]) {
        CHECK_PAGE(lh + mg);
        ct = &pages[npages - 1];

        char labels[1032];
        snprintf(labels, sizeof(labels), "Labels: %s", report->top_labels);
        float lw = str_width(labels, fs);
        if (lw > usable) {
            y = add_wrapped(ct, col1, y, labels, "F1", fs, lh, NULL, usable, mg);
            y -= lh * 0.3f;
        } else {
            ct_text(ct, col1, y, labels, "F1", fs, NULL);
            y -= lh * 1.2f;
        }
    }

    /* ---- Section: Summary ---- */
    if (sec_flags & FMT_SHOW_SUMMARY) {
    CHECK_PAGE(lh + mg);
    ct = &pages[npages - 1];
    {
        const PdfColor *hc2 = uc ? &hcol : NULL;
        ct_text(ct, col1, y, "[*] Summary", "F2", fs * 1.15f, hc2);
        y -= lh * 1.4f;

        y = add_wrapped(ct, col1, y, report->summary, "F1", fs, lh, NULL, usable, mg);
    }
    }

done:
    /* ---- Write PDF objects ---- */
    ;
    PdfW w;
    pw_init(&w);

    /* Assigned object numbers:
       obj 1 = Catalog
       obj 2 = Pages
       obj 3..3+npages-1 = Page objects
       obj 3+npages..3+2*npages-1 = Stream objects  (content streams)
       obj 3+2*npages = Helvetica
       obj 3+2*npages+1 = Helvetica-Bold
       obj 3+2*npages+2 = CJK Type0
       obj 3+2*npages+3 = CIDFontType2
       obj 3+2*npages+4 = ToUnicode CMap
       obj 3+2*npages+5 = FontDescriptor
    */
    int stream_start = 3 + npages;
    int font_start = stream_start + npages;

    /* Header */
    db_puts(&w.buf, "%PDF-1.4\n");

    /* ---- Font objects ---- */
    /* F1: Helvetica */
    int f_helv = font_start;
    pw_printf(&w, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n",
              pw_alloc(&w));

    /* F2: Helvetica-Bold */
    int f_bold = font_start + 1;
    pw_printf(&w, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n",
              pw_alloc(&w));

    /* F3: CJK Type0 (STSong-Light) */
    int f_cjk = font_start + 2;
    int cid_font = font_start + 3;
    int cmap_obj = font_start + 4;
    int fd_obj   = font_start + 5;

    pw_printf(&w, "%d 0 obj\n<< /Type /Font /Subtype /Type0"
              " /BaseFont /STSong-Light /Encoding /Identity-H"
              " /DescendantFonts [%d 0 R] /ToUnicode %d 0 R >>\nendobj\n",
              pw_alloc(&w), cid_font, cmap_obj);

    /* CIDFontType2 descendant */
    pw_printf(&w, "%d 0 obj\n<< /Type /Font /Subtype /CIDFontType2"
              " /BaseFont /STSong-Light"
              " /CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 5 >>"
              " /FontDescriptor %d 0 R"
              " /W [0 [1000]] >>\nendobj\n",
              pw_alloc(&w), fd_obj);

    /* ToUnicode CMap (Identity) */
    {
        int ob = pw_alloc(&w);
        const char *cmap =
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
            "end\n";
        size_t clen = strlen(cmap);
        pw_printf(&w, "%d 0 obj\n<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
                  ob, clen, cmap);
    }

    /* FontDescriptor for CJK */
    pw_printf(&w, "%d 0 obj\n<< /Type /FontDescriptor /FontName /STSong-Light"
              " /Flags 4 /FontBBox [0 0 1000 1000]"
              " /ItalicAngle 0 /Ascent 1000 /Descent 0"
              " /CapHeight 1000 /StemV 0 >>\nendobj\n",
              pw_alloc(&w));

    /* ---- Stream objects ---- */
    int stream_objs[8];
    for (int i = 0; i < npages; i++) {
        stream_objs[i] = pw_alloc(&w);
        pw_printf(&w, "%d 0 obj\n<< /Length %zu >>\nstream\n",
                  stream_objs[i], pages[i].len);
        db_puts(&w.buf, pages[i].data);
        db_puts(&w.buf, "\nendstream\nendobj\n");
    }

    /* ---- Page objects ---- */
    int page_objs[8];
    for (int i = 0; i < npages; i++) {
        page_objs[i] = pw_alloc(&w);
        pw_printf(&w, "%d 0 obj\n<< /Type /Page /Parent 2 0 R"
                  " /MediaBox [0 0 %.0f %.0f]"
                  " /Contents %d 0 R"
                  " /Resources << /Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R >> >>"
                  " >>\nendobj\n",
                  page_objs[i], pw, ph,
                  stream_objs[i], f_helv, f_bold, f_cjk);
    }

    /* ---- Pages tree ---- */
    int pages_obj = pw_alloc(&w);
    db_printf(&w.buf, "%d 0 obj\n<< /Type /Pages /Kids [", pages_obj);
    for (int i = 0; i < npages; i++)
        db_printf(&w.buf, "%d 0 R ", page_objs[i]);
    db_printf(&w.buf, "] /Count %d >>\nendobj\n", npages);

    /* ---- Catalog ---- */
    int catalog = pw_alloc(&w);
    db_printf(&w.buf, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n",
              catalog, pages_obj);

    /* ---- xref ---- */
    pw_finish(&w, catalog);

    /* ---- Write to file ---- */
    if (fwrite(w.buf.data, 1, w.buf.len, fp) != w.buf.len) {
        pw_free(&w);
        for (int i = 0; i < npages; i++) db_free(&pages[i]);
        return ERR_EXPORT;
    }

    pw_free(&w);
    for (int i = 0; i < npages; i++) db_free(&pages[i]);
    return GS_OK;
}
