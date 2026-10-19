#include "pdf_int.h"

/* ---- Paginated layout document: page management, text rows, charts. ---- */

void doc_new_page(Doc *d) {
    if (d->npages >= d->pcap) {
        int nc = d->pcap ? d->pcap * 2 : 8;
        DynBuf *p = realloc(d->pages, (size_t)nc * sizeof(DynBuf));
        if (!p) return;
        d->pages = p;
        d->pcap = nc;
    }
    db_init(&d->pages[d->npages]);
    d->y = d->ph - d->mg;
    d->npages++;
}

DynBuf *doc_cur(Doc *d) {
    return &d->pages[d->npages - 1];
}

/* Ensure `need` units of vertical space fit on the current page. */
void doc_ensure(Doc *d, float need) {
    if (d->npages == 0) {
        doc_new_page(d);
        return;
    }
    if (d->y - need < d->bot) doc_new_page(d);
}

void doc_text(Doc *d, float x, float y, const char *s,
                     const char *font, float sz, const PdfColor *c) {
    ct_text(doc_cur(d), x, y, s, font, sz, c);
}

/* Record a URI link annotation covering [x1..x2] at baseline y. */
void doc_link(Doc *d, float x, float y, float w, const char *url) {
    if (!url || !url[0]) return;
    if (d->nlinks == d->lcap) {
        int nc = d->lcap ? d->lcap * 2 : 32;
        LinkRec *p = realloc(d->links, (size_t)nc * sizeof(LinkRec));
        if (!p) return;
        d->links = p;
        d->lcap = nc;
    }
    LinkRec *l = &d->links[d->nlinks++];
    l->page = d->npages - 1;
    l->x1 = x;
    l->x2 = x + w;
    l->y1 = y - 1.0f;
    l->y2 = y + d->fs * 1.25f;
    snprintf(l->url, sizeof(l->url), "%s", url);
}

/* key / value row; the value is right-aligned to the right margin and is
 * truncated with an ellipsis if it would overflow. */
void doc_kv_c(Doc *d, const char *label, const char *value,
              const PdfColor *valcolor) {
    doc_ensure(d, d->lh);
    doc_text(d, d->col1, d->y, label, "F1", d->fs, &d->lcol);
    const PdfColor *vc = d->uc ? valcolor : NULL;
    float vw = str_width(value, d->fs);
    float xmax = d->pw - d->mg;
    if (d->col2 + vw <= xmax) {
        doc_text(d, xmax - vw, d->y, value, "F1", d->fs, vc);
    } else {
        char buf[1024];
        ellipsize(value, xmax - d->col2, d->fs, buf, sizeof(buf));
        doc_text(d, d->col2, d->y, buf, "F1", d->fs, vc);
    }
    d->y -= d->lh;
}

void doc_kv(Doc *d, const char *label, const char *value) {
    doc_kv_c(d, label, value, NULL);
}

/* Section header with underline, plus a bookmark outline entry. */
void doc_section(Doc *d, const char *title, float needed) {
    doc_ensure(d, needed);
    float y0 = d->y;
    const PdfColor *hc = d->uc ? &d->hcol : NULL;
    doc_text(d, d->col1, y0, title, "F2", d->fs * 1.15f, hc);
    if (d->uc)
        ct_line_c(doc_cur(d), d->col1, y0 - d->lh * 0.35f, d->pw - d->mg, y0 - d->lh * 0.35f, d->hcol);
    else
        ct_line(doc_cur(d), d->col1, y0 - d->lh * 0.35f, d->pw - d->mg, y0 - d->lh * 0.35f);
    d->y = y0 - d->lh * 1.4f;

    if (d->noutline == d->ocap) {
        int nc = d->ocap ? d->ocap * 2 : 8;
        char (*t)[80] = realloc(d->otitles, (size_t)nc * sizeof(*d->otitles));
        int *p = realloc(d->opages, (size_t)nc * sizeof(int));
        if (!t || !p) { free(t); free(p); return; }
        d->otitles = t;
        d->opages = p;
        d->ocap = nc;
    }
    snprintf(d->otitles[d->noutline], 80, "%s", title);
    d->opages[d->noutline] = d->npages - 1;
    d->noutline++;
}

/* Status tag color for doc_item_row; NULL for unknown states / no tag. */
static const PdfColor *state_color(const Doc *d, const char *state) {
    if (!state || !d->uc) return NULL;
    if (!strcmp(state, "open"))   return &d->opencol;
    if (!strcmp(state, "closed")) return &d->closedcol;
    if (!strcmp(state, "merged")) return &d->mergedcol;
    return NULL;
}

/* One body line: title (optionally link-colored), a right-placed colored
 * "[state]" tag, and a URI annotation covering the text when url is set.
 * Truncated with "..." to fit one row. */
void doc_item_row(Doc *d, const char *text, const char *state,
                  const char *url) {
    doc_ensure(d, d->lh);
    char tag[64];
    tag[0] = '\0';
    if (state && state[0])
        snprintf(tag, sizeof(tag), " [%s]", state);
    float tagw = tag[0] ? str_width(tag, d->fs) + 4.0f : 0;
    float avail = d->usable - tagw;
    if (avail < d->fs * 4) avail = d->usable;   /* never squeeze below ~4 chars */

    const char *s = text;
    float w = str_width(text, d->fs);
    char buf[2560];
    if (w > avail) {
        ellipsize(text, avail, d->fs, buf, sizeof(buf));
        s = buf;
        w = str_width(s, d->fs);
        if (w > avail) w = avail;
    }
    const PdfColor *tc = (url && url[0]) ? (d->uc ? &d->linkcol : NULL) : NULL;
    doc_text(d, d->col1, d->y, s, "F1", d->fs, tc);
    if (url && url[0])
        doc_link(d, d->col1, d->y, w, url);
    if (tag[0])
        doc_text(d, d->col1 + w + 4.0f, d->y, tag, "F1", d->fs, state_color(d, state));
    d->y -= d->lh;
}

/* Word-wrapped (ASCII) / character-wrapped (CJK) paragraph with page breaks.
 * Never modifies its input. */
void doc_wrapped(Doc *d, const char *text, const char *font, float sz,
                        const PdfColor *color) {
    const char *p = text;
    const char *line_start = p;
    const char *last_space = NULL;
    float line_w = 0;
    float lh = sz * d->lh / d->fs;

    while (*p) {
        const unsigned char *up = (const void*)p;
        uint32_t cp = utf8_decode(&up);
        size_t clen = (size_t)((const char*)up - p);
        float cw = cp_width(cp, sz);

        if (cp == ' ') last_space = p;

        if (line_w + cw > d->usable && line_w > 0) {
            const char *break_at;
            if (last_space && last_space >= line_start)
                break_at = last_space;
            else
                break_at = p;
            doc_ensure(d, lh);
            ct_textn(doc_cur(d), d->col1, d->y, line_start,
                     (size_t)(break_at - line_start), font, sz, color);
            d->y -= lh;

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
        doc_ensure(d, lh);
        ct_textn(doc_cur(d), d->col1, d->y, line_start,
                 (size_t)(p - line_start), font, sz, color);
        d->y -= lh;
    }
}

/* ---- Minimal Markdown renderer (LLM Summary) ----
 * Handles blank-line paragraphs, "# heading", "-"/"*"/"+" bullets,
 * "N." numbered items, ">" quotes, "---" rules and inline **bold**,
 * *italic*, `code`. Crammed "N. " items and "**Title**" runs after
 * sentence punctuation are split onto their own lines via the shared
 * md_prettify_text() (export/md.c) — common LLM output shape. */

enum { MD_REG, MD_BOLD, MD_ITAL, MD_CODE };

typedef struct { const char *p; size_t len; int style; float w; } MDTok;

static const char *md_font(int style) {
    switch (style) {
    case MD_BOLD: return "F2";
    case MD_ITAL: return "F4";
    case MD_CODE: return "F5";
    default:      return "F1";
    }
}

static float md_w(uint32_t cp, float sz, int style) {
    if (style == MD_CODE && cp >= 32 && cp <= 126) return sz * 0.6f; /* Courier is fixed-pitch */
    return cp_width_f(cp, sz, style == MD_BOLD);
}

static float md_span_w(const char *s, size_t len, float sz, int style) {
    float w = 0;
    const unsigned char *u = (const unsigned char *)s, *end = u + len;
    while (u < end) w += md_w(utf8_decode(&u), sz, style);
    return w;
}

/* Inline marker length at s: 2 for '**', 1 for '*' / '`', else 0. */
static int md_marker(const char *s) {
    if (s[0] == '*' && s[1] == '*') return 2;
    if (s[0] == '*' || s[0] == '`') return 1;
    return 0;
}

/* Split a NUL-terminated line into wrap-candidate tokens: ASCII words,
 * single CJK chars, and spaces; markers switch the text style. */
static size_t md_tokenize(const char *buf, int style0, float sz,
                          MDTok *toks, size_t max) {
    size_t n = 0;
    const char *s = buf;
    int style = style0;
    while (*s && n < max) {
        int m = md_marker(s);
        if (m) {
            int target = m == 2 ? MD_BOLD : (s[0] == '*' ? MD_ITAL : MD_CODE);
            style = (style == target) ? MD_REG : target;
            s += m;
            continue;
        }
        if (*s == ' ') {
            toks[n].p = s; toks[n].len = 1; toks[n].style = style;
            toks[n].w = md_w(' ', sz, style);
            n++; s++; continue;
        }
        const unsigned char *u = (const unsigned char *)s;
        uint32_t cp = utf8_decode(&u);
        size_t cl = (size_t)((const char *)u - s);
        if (cpansi_byte(cp) < 0) {
            toks[n].p = s; toks[n].len = cl; toks[n].style = style;
            toks[n].w = md_w(cp, sz, style);
            n++; s += cl; continue;
        }
        const char *w0 = s;
        while (*s && *s != ' ' && !md_marker(s)) {
            const unsigned char *q = (const unsigned char *)s;
            if (cpansi_byte(utf8_decode(&q)) < 0) break;
            s++;
        }
        toks[n].p = w0; toks[n].len = (size_t)(s - w0); toks[n].style = style;
        toks[n].w = md_span_w(w0, s - w0, sz, style);
        n++;
    }
    return n;
}

/* Draw one logical line (already NUL-terminated) with word/CJK wrapping. */
static void doc_md_line(Doc *d, const char *line, float indent, float sz,
                        int style0, const PdfColor *color) {
    size_t maxlen = strlen(line);
    MDTok *toks = malloc((maxlen + 2) * sizeof(MDTok));
    if (!toks) return;
    size_t nt = md_tokenize(line, style0, sz, toks, maxlen + 1);
    float lh = sz * d->lh / d->fs;
    float right = d->pw - d->mg;
    float x0 = d->col1 + indent;
    float x = x0;
    int drawn = 0;

    doc_ensure(d, lh);
    for (size_t i = 0; i < nt; i++) {
        int is_space = toks[i].len == 1 && toks[i].p[0] == ' ';
        if (drawn && x + toks[i].w > right) {
            d->y -= lh;
            doc_ensure(d, lh);
            x = x0;
            drawn = 0;
        }
        if (!drawn && is_space) continue;   /* no leading spaces on a row */
        ct_textn(doc_cur(d), x, d->y, toks[i].p, toks[i].len,
                 md_font(toks[i].style), sz, color);
        x += toks[i].w;
        drawn = 1;
    }
    d->y -= lh;
    free(toks);
}

void doc_md(Doc *d, const char *md, float sz) {
    size_t len = strlen(md);
    char *buf = malloc(len * 2 + 2);   /* room for inserted newlines */
    if (!buf) return;
    md_prettify_text(md, buf, len * 2 + 2);

    float lh = sz * d->lh / d->fs;
    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        char *s = p;
        while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
        *e = '\0';

        if (!*s) {
            d->y -= lh * 0.35f;
        } else if (s[0] == '#') {
            int lvl = 0;
            while (s[lvl] == '#') lvl++;
            char *t = s + lvl;
            while (*t == ' ') t++;
            if (lvl <= 6 && *t) {
                d->y -= lh * 0.3f;
                doc_md_line(d, t, 0, sz * 1.15f, MD_BOLD, d->uc ? &d->hcol : NULL);
            } else {
                doc_md_line(d, s, 0, sz, 0, NULL);
            }
        } else if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
            doc_ensure(d, lh);
            ct_textn(doc_cur(d), d->col1 + sz * 0.2f, d->y,
                     "\xe2\x80\xa2", 3, "F1", sz, NULL);
            doc_md_line(d, s + 2, sz * 1.4f, sz, 0, NULL);
        } else if (s[0] >= '1' && s[0] <= '9') {
            char *t = s;
            while (*t >= '0' && *t <= '9') t++;
            float ind = ((*t == '.' || *t == ')') && t[1] == ' ') ? sz * 1.2f : 0;
            doc_md_line(d, s, ind, sz, 0, NULL);
        } else if (s[0] == '>' && (s[1] == ' ' || s[1] == '\0')) {
            doc_md_line(d, s[1] ? s + 2 : s, sz * 1.2f, sz, 0,
                        d->uc ? &d->lcol : NULL);
        } else if (!strcmp(s, "---") || !strcmp(s, "***") || !strcmp(s, "___")) {
            d->y -= lh * 0.4f;
            doc_ensure(d, lh);
            ct_line(doc_cur(d), d->col1, d->y, d->pw - d->mg, d->y);
            d->y -= lh * 0.8f;
        } else {
            doc_md_line(d, s, 0, sz, 0, NULL);
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(buf);
}

/* Horizontal bar with an arbitrary value label; frac in [0,1] sets bar width. */
void doc_hbar_txt(Doc *d, const char *label, const char *value_text,
                          float frac, const PdfColor *color) {
    doc_ensure(d, d->lh);
    float xmax = d->pw - d->mg;
    float lblw = d->col2 - d->col1 - 6.0f;
    char lbuf[80];
    if (str_width(label, d->fs * 0.9f) > lblw)
        ellipsize(label, lblw, d->fs * 0.9f, lbuf, sizeof(lbuf));
    else
        snprintf(lbuf, sizeof(lbuf), "%s", label);

    doc_text(d, d->col1, d->y, lbuf, "F1", d->fs * 0.9f, &d->lcol);

    float vw = str_width(value_text, d->fs * 0.9f);
    doc_text(d, xmax - vw, d->y, value_text, "F1", d->fs * 0.9f, NULL);

    float barea = xmax - d->col2 - vw - 6.0f;
    if (barea < 0) barea = 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float bw = barea * frac;
    if (frac > 0 && bw < 2) bw = 2;
    if (bw > 0) {
        ct_rect(doc_cur(d), d->col2, d->y + d->fs * 0.35f, bw, d->fs * 0.45f, *color);
    }
    d->y -= d->lh;
}

/* Horizontal bar (commits/day activity). */
void doc_hbar(Doc *d, const char *label, long value, long maxval,
                     const PdfColor *color) {
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%ld", value);
    float frac = maxval > 0 ? (float)value / (float)maxval : 0;
    doc_hbar_txt(d, label, vbuf, frac, color);
}

/* Vertical column chart (e.g. commits by weekday). */
void doc_vbar(Doc *d, const char *const *labels, const int *vals, int n,
                     int maxval, float chart_w, float chart_h,
                     const PdfColor *color) {
    doc_ensure(d, chart_h + d->fs * 1.6f);
    float slot = chart_w / n;
    float base = d->y - chart_h;
    ct_line(doc_cur(d), d->col1, base, d->col1 + chart_w, base);
    for (int i = 0; i < n; i++) {
        float frac = maxval > 0 ? (float)vals[i] / (float)maxval : 0;
        float cw = slot * 0.55f;
        float h = chart_h * frac;
        if (vals[i] > 0 && h < 2) h = 2;
        float x = d->col1 + slot * (float)i + (slot - cw) / 2.0f;
        if (h > 0) ct_rect(doc_cur(d), x, base, cw, h, *color);
        float lblw = str_width(labels[i], d->fs * 0.8f);
        ct_text(doc_cur(d), x + cw / 2.0f - lblw / 2.0f, base - d->fs * 0.7f,
                labels[i], "F1", d->fs * 0.8f, &d->lcol);
    }
    d->y = base - d->fs * 1.8f;
}
