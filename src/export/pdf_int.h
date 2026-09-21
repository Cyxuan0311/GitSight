#ifndef GITSIGHT_PDF_INT_H
#define GITSIGHT_PDF_INT_H
/* Internal declarations shared by the PDF export modules
 * (pdf_util.c, pdf_font.c, pdf_write.c, pdf_doc.c, pdf.c).
 * Not part of the public API. */

#include "gitsight/export/exporter.h"
#include "gitsight/config.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_FULL ((size_t)-1)

/* ---- Dynamic buffer ----
 * Invariant: b->data[b->len] is always '\0' so the buffer doubles as a C
 * string; growth helpers reserve room for it. */
typedef struct { char *data; size_t len, cap; } DynBuf;

void db_init(DynBuf *b);
void db_free(DynBuf *b);
int  db_grow(DynBuf *b, size_t need);
void db_puts(DynBuf *b, const char *s);
void db_putc(DynBuf *b, char c);
void db_mem(DynBuf *b, const void *data, size_t n);
__attribute__((format(printf,2,3)))
void db_printf(DynBuf *b, const char *fmt, ...);

/* ---- Colors ---- */
typedef struct { unsigned char r, g, b; } PdfColor;

extern const PdfColor PC_BLACK;
extern const PdfColor PC_BLUE;
extern const PdfColor PC_GRAY;
extern const PdfColor PC_LINK;    /* item rows with a URL        */
extern const PdfColor PC_OPEN;    /* [open] status tag           */
extern const PdfColor PC_CLOSED;  /* [closed] status tag         */
extern const PdfColor PC_MERGED;  /* [merged] status tag         */

/* ---- UTF-8 / text measurement ---- */
uint32_t utf8_decode(const unsigned char **sp);
int   cpansi_byte(uint32_t cp);
float cp_width(uint32_t cp, float fs);
float cp_width_f(uint32_t cp, float fs, int bold);
float str_width(const char *s, float fs);
size_t ellipsize(const char *s, float max_w, float fs, char *out, size_t cap);
void  fmt_int(char *buf, size_t cap, long v);

/* ---- CJK usage tracking (pdf_font.c) ----
 * ct_textn marks every codepoint it routes to the Type0 font; export_pdf
 * checks the bitmap afterwards to decide whether to embed a font subset. */
void cjk_mark(uint32_t cp);
int  cjk_any_used(void);
void cjk_reset(void);

/* ---- Content stream + file writer (pdf_write.c) ---- */
void ct_printf(DynBuf *ct, const char *fmt, ...)
    __attribute__((format(printf,2,3)));
void ct_color(DynBuf *ct, PdfColor c);
void ct_font(DynBuf *ct, const char *key, float sz);
void ct_textn(DynBuf *ct, float x, float y, const char *text, size_t maxlen,
              const char *font, float sz, const PdfColor *color);
void ct_text(DynBuf *ct, float x, float y, const char *text,
             const char *font, float sz, const PdfColor *color);
void ct_line(DynBuf *ct, float x1, float y1, float x2, float y2);
void ct_line_c(DynBuf *ct, float x1, float y1, float x2, float y2, PdfColor c);
void ct_rect(DynBuf *ct, float x, float y, float w, float h, PdfColor c);

typedef struct {
    DynBuf  buf;
    size_t *offsets;
    int     nobjs;
    int     ocap;
} PdfW;

void pw_init(PdfW *pw);
void pw_free(PdfW *pw);
int  pw_reserve(PdfW *pw);
void pw_record(PdfW *pw, int obj);
void pw_printf(PdfW *pw, const char *fmt, ...)
    __attribute__((format(printf,2,3)));
void pw_finish(PdfW *pw, int root_obj, int info_obj);

/* Escape `(` `)` and `\` inside a PDF string literal. */
void escape_pdf_string(const char *src, char *dst, size_t cap);

/* ---- Embedded CJK font subset (pdf_font.c) ---- */
typedef struct {
    unsigned char *font;      /* compressed subset sfnt          */
    size_t font_len;
    unsigned char *cidmap;    /* compressed 64K CID->GID map     */
    size_t cidmap_len;
    DynBuf widths;            /* "/W" body entries, NUL-terminated */
    int32_t bbox[4];
    int32_t asc, desc;
} CjkFont;

int  cjk_build(CjkFont *cf);
void cjkfont_free(CjkFont *cf);

/* ---- Paginated layout document (pdf_doc.c) ---- */
typedef struct { int page; float x1, y1, x2, y2; char url[MAX_URL_LEN]; } LinkRec;

typedef struct {
    DynBuf *pages;         /* one content stream per page */
    int     npages, pcap;

    float   y;             /* current baseline (PDF user space, bottom-up) */
    float   pw, ph;        /* page size */
    float   mg;            /* margin */
    float   fs;            /* base font size */
    float   lh;            /* line height */
    float   bot;           /* bottom limit for content (above footer) */
    float   col1, col2;    /* key / value columns */
    float   usable;
    int     uc;            /* use color? */

    PdfColor tcol, hcol, lcol;      /* title / section header / label */
    PdfColor linkcol, opencol, closedcol, mergedcol;
    const Report *report;

    LinkRec *links;        /* URI annotations */
    int      nlinks, lcap;

    char  (*otitles)[80];  /* outline bookmarks */
    int    *opages;
    int     noutline, ocap;
} Doc;

void    doc_new_page(Doc *d);
DynBuf *doc_cur(Doc *d);
void    doc_ensure(Doc *d, float need);
void    doc_text(Doc *d, float x, float y, const char *s,
                 const char *font, float sz, const PdfColor *c);
void    doc_link(Doc *d, float x, float y, float w, const char *url);
void    doc_kv(Doc *d, const char *label, const char *value);
void    doc_kv_c(Doc *d, const char *label, const char *value,
                 const PdfColor *valcolor);
void    doc_section(Doc *d, const char *title, float needed);
void    doc_item_row(Doc *d, const char *text, const char *state,
                     const char *url);
void    doc_wrapped(Doc *d, const char *text, const char *font, float sz,
                    const PdfColor *color);
void    doc_md(Doc *d, const char *md, float sz);
void    doc_hbar(Doc *d, const char *label, long value, long maxval,
                 const PdfColor *color);
void    doc_vbar(Doc *d, const char *const *labels, const int *vals, int n,
                 int maxval, float chart_w, float chart_h,
                 const PdfColor *color);

/* ---- PDF object/xref serialization (pdf_objects.c) ----
 * Writes the complete PDF file into w->buf: fonts (F1 Helvetica,
 * F2 Helvetica-Bold, F3 CJK Type0, F4 Helvetica-Oblique, F5 Courier),
 * page content streams, link annotations, outlines, pages tree, info,
 * catalog and xref. Caller owns PdfW afterwards (pw_free). */
ErrorCode pdf_write_document(PdfW *w, const Doc *d, const CjkFont *cf,
                             int embed, const Report *report,
                             float pw, float ph);

#endif
