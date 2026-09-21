/* PDF export — content stream operators and the object/xref file writer. */
#include "pdf_int.h"

/* ---- Content Stream Builder ---- */
void ct_printf(DynBuf *ct, const char *fmt, ...) {
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

void ct_color(DynBuf *ct, PdfColor c) {
    ct_printf(ct, "%.3f %.3f %.3f rg\n", c.r / 255.0, c.g / 255.0, c.b / 255.0);
}

void ct_font(DynBuf *ct, const char *key, float sz) {
    ct_printf(ct, "/%s %f Tf\n", key, sz);
}

/* Render text; when maxlen != -1, only the first maxlen bytes are drawn.
 * The string is split into runs by script: WinAnsi-representable codepoints
 * are drawn with `font` (Helvetica, WinAnsiEncoding) as cp1252 bytes; the
 * rest (CJK, emoji) with the Type0 font F3. Each run advances x by its
 * measured width so Latin text inside a CJK line keeps proportional spacing. */
void ct_textn(DynBuf *ct, float x, float y, const char *text, size_t maxlen,
              const char *font, float sz, const PdfColor *color) {
    const char *end = maxlen == (size_t)-1 ? NULL : text + maxlen;
    const unsigned char *p = (const unsigned char *)text;
    float cx = x;

    while (*p && (end ? (const char *)p < end : 1)) {
        const unsigned char *probe = p;
        int winansi_run = cpansi_byte(utf8_decode(&probe)) >= 0;
        float run_w = 0;

        ct_printf(ct, "BT\n");
        if (color) ct_color(ct, *color); else ct_color(ct, PC_BLACK);

        if (winansi_run) {
            ct_font(ct, font, sz);
            ct_printf(ct, "1 0 0 1 %.2f %.2f Tm\n", cx, y);
            db_putc(ct, '(');
            while (*p && (end ? (const char *)p < end : 1)) {
                const unsigned char *q = p;
                uint32_t cp = utf8_decode(&q);
                if (end && (const char *)q > end) break;
                int b = cpansi_byte(cp);
                if (b < 0) break;
                run_w += cp_width(cp, sz);
                if (b == '(' || b == ')' || b == '\\')
                    ct_printf(ct, "\\%c", b);
                else if (b < 32)
                    ct_printf(ct, "\\%03o", b);
                else
                    db_putc(ct, (char)b);
                p = q;
            }
            db_putc(ct, ')');
        } else {
            ct_font(ct, "F3", sz);
            ct_printf(ct, "1 0 0 1 %.2f %.2f Tm\n", cx, y);
            db_putc(ct, '<');
            while (*p && (end ? (const char *)p < end : 1)) {
                const unsigned char *q = p;
                uint32_t cp = utf8_decode(&q);
                if (end && (const char *)q > end) break;
                if (cpansi_byte(cp) >= 0) break;
                cjk_mark(cp);
                run_w += cp_width(cp, sz);
                if (cp <= 0xFFFF)
                    ct_printf(ct, "%04X", (unsigned int)cp);
                else {
                    uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
                    uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
                    ct_printf(ct, "%04X%04X", hi, lo);
                }
                p = q;
            }
            db_putc(ct, '>');
        }
        db_puts(ct, " Tj\nET\n");
        cx += run_w;
    }
}

void ct_text(DynBuf *ct, float x, float y, const char *text,
             const char *font, float sz, const PdfColor *color) {
    ct_textn(ct, x, y, text, TEXT_FULL, font, sz, color);
}

void ct_line(DynBuf *ct, float x1, float y1, float x2, float y2) {
    ct_printf(ct, "0 0 0 RG\n%.2f %.2f m %.2f %.2f l S\n", x1, y1, x2, y2);
}

/* Stroked line in an explicit color ("RG" sets the stroke color). */
void ct_line_c(DynBuf *ct, float x1, float y1, float x2, float y2, PdfColor c) {
    ct_printf(ct, "%.3f %.3f %.3f RG\n%.2f %.2f m %.2f %.2f l S\n",
              c.r / 255.0, c.g / 255.0, c.b / 255.0, x1, y1, x2, y2);
}

void ct_rect(DynBuf *ct, float x, float y, float w, float h, PdfColor c) {
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    ct_color(ct, c);
    ct_printf(ct, "%.2f %.2f %.2f %.2f re f\n", x, y, w, h);
}

/* ---- PDF Writer ---- */
void pw_init(PdfW *pw) {
    memset(pw, 0, sizeof(*pw));
    /* Object numbers start at 1; xref entry 0 is the free-list head. */
    pw->nobjs = 1;
}

void pw_free(PdfW *pw) {
    db_free(&pw->buf);
    free(pw->offsets);
}

/* Reserve an object number without recording an offset yet.
 *
 * Object numbering and file layout are two separate concerns: the numbers
 * must be assigned up front so bodies can contain forward references (e.g.
 * a font whose body precedes its descendant/cmap objects), while the xref
 * offset may only be recorded the moment each body actually starts. */
int pw_reserve(PdfW *pw) {
    if (pw->nobjs >= pw->ocap) {
        int nc = pw->ocap ? pw->ocap * 2 : 32;
        size_t *p = realloc(pw->offsets, (size_t)nc * sizeof(size_t));
        if (!p) return -1;
        for (int i = pw->ocap; i < nc; i++) p[i] = (size_t)-1;
        pw->offsets = p;
        pw->ocap = nc;
    }
    pw->offsets[pw->nobjs] = (size_t)-1;
    return pw->nobjs++;
}

/* Record the file offset of the body of object `obj` (current write pos). */
void pw_record(PdfW *pw, int obj) {
    if (obj < 1) return;
    if (obj >= pw->ocap) {
        int nc = pw->ocap ? pw->ocap * 2 : 32;
        while (nc <= obj) nc *= 2;
        size_t *p = realloc(pw->offsets, (size_t)nc * sizeof(size_t));
        if (!p) return;
        for (int i = pw->ocap; i < nc; i++) p[i] = (size_t)-1;
        pw->offsets = p;
        pw->ocap = nc;
    }
    pw->offsets[obj] = pw->buf.len;
}

void pw_printf(PdfW *pw, const char *fmt, ...) {
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
void pw_finish(PdfW *pw, int root_obj, int info_obj) {
    size_t xref_off = pw->buf.len;
    db_printf(&pw->buf, "xref\n%d %d\n", 0, pw->nobjs);
    /* entry 0 (free) */
    db_puts(&pw->buf, "0000000000 65535 f \n");
    for (int i = 1; i < pw->nobjs; i++) {
        db_printf(&pw->buf, "%010zu 00000 n \n", pw->offsets[i]);
    }

    db_printf(&pw->buf, "trailer\n<< /Size %d /Root %d 0 R", pw->nobjs, root_obj);
    if (info_obj >= 0) db_printf(&pw->buf, " /Info %d 0 R", info_obj);
    db_printf(&pw->buf, " >>\nstartxref\n%zu\n%%%%EOF\n", xref_off);
}

/* Escape `(` `)` and `\` inside a PDF string literal. */
void escape_pdf_string(const char *src, char *dst, size_t cap) {
    size_t p = 0;
    for (; *src && p + 6 < cap; src++) {
        if (*src == '(' || *src == ')' || *src == '\\')
            dst[p++] = '\\';
        dst[p++] = *src;
    }
    dst[p] = '\0';
}
