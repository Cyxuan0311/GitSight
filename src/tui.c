#include "gitsight/tui.h"
#include "gitsight/color.h"
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>

#define TUI_PAD         2
#define TUI_BG_ZEBRA    "\x1b[48;5;236m"
#define TUI_HDR_DEFAULT "\x1b[1;36m"

static int utf8_cp(const char *s, size_t *len) {
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) { *len = 1; return p[0]; }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *len = 2;
        return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *len = 3;
        return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *len = 4;
        return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
               ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
    *len = 1;
    return p[0];
}

static int is_wide_cp(int cp) {
    return (cp >= 0x1100 && cp <= 0x115F) ||
           (cp >= 0x2E80 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);
}

static int utf8_width(const char *s) {
    int w = 0;
    const char *p = s;
    while (*p) {
        size_t l;
        int cp = utf8_cp(p, &l);
        w += is_wide_cp(cp) ? 2 : 1;
        p += l;
    }
    return w;
}

static int tui_term_width(void) {
    const char *col = getenv("COLUMNS");
    if (col && col[0]) {
        int n = atoi(col);
        if (n > 20) return n;
    }
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20)
        return (int)ws.ws_col;
    return 80;
}

static void print_spaces(FILE *out, int n) {
    for (int i = 0; i < n; i++) fputc(' ', out);
}

static void print_prefix(FILE *out, const char *s, int chars) {
    size_t len = 0;
    int w = 0;
    while (s[len]) {
        size_t l;
        int cp = utf8_cp(s + len, &l);
        int cw = is_wide_cp(cp) ? 2 : 1;
        if (w + cw > chars) break;
        w += cw;
        len += l;
    }
    fwrite(s, 1, len, out);
}

static void print_padded(FILE *out, const char *s, int width, TuiAlign align,
                         int truncate) {
    int disp = utf8_width(s);
    int over = disp > width;
    int avail = over && truncate ? width - 1 : width;
    int left = 0, right = 0;
    if (avail > 0) {
        if (align == TUI_ALIGN_RIGHT)
            left = avail - disp;
        else if (align == TUI_ALIGN_CENTER)
            left = (avail - disp) / 2;
        if (left < 0) left = 0;
        right = avail - disp - left;
        if (right < 0) right = 0;
    }
    print_spaces(out, left);
    if (!over) {
        fputs(s, out);
    } else {
        print_prefix(out, s, truncate ? width - 1 : width);
        if (truncate) fputs("\xE2\x80\xA6", out);
    }
    print_spaces(out, right);
}

static void print_cell(FILE *out, const char *s, int width, TuiAlign align,
                       int truncate, const char *fg, const char *bg) {
    if (bg) fputs(bg, out);
    if (fg) fputs(fg, out);
    print_padded(out, s, width, align, truncate);
    if (fg || bg) fputs(CLR_RESET, out);
}

static const char *box_gl(const TuiTable *t, int which) {
    /* which: 0=tl 1=tr 2=bl 3=br 4=ju 5=jd 6=jl 7=jr 8=jx 9=h 10=v */
    if (t->border == TUI_BORDER_ASCII) {
        static const char *g[11] = {"+", "+", "+", "+", "+", "+", "+", "+", "+", "-", "|"};
        return g[which];
    }
    static const char *g[11] = {
        "\xE2\x94\x8C", "\xE2\x94\x90", "\xE2\x94\x94", "\xE2\x94\x98",
        "\xE2\x94\xAC", "\xE2\x94\xB4", "\xE2\x94\x9C", "\xE2\x94\xA4",
        "\xE2\x94\xBC", "\xE2\x94\x80", "\xE2\x94\x82"
    };
    return g[which];
}

static void shrink_widths(const TuiTable *t, int *w, int *total) {
    int maxw = t->max_width > 0 ? t->max_width : tui_term_width();
    while (t->truncate && *total > maxw) {
        int bi = -1;
        for (int c = 0; c < t->ncols; c++) {
            if (w[c] > 1 && (bi < 0 || w[c] > w[bi])) bi = c;
        }
        if (bi < 0) break;
        w[bi]--;
        (*total)--;
    }
}

void tui_table_init(TuiTable *t, int ncols) {
    if (ncols > TUI_MAX_COLS) ncols = TUI_MAX_COLS;
    memset(t, 0, sizeof(*t));
    t->ncols = ncols;
    t->border = TUI_BORDER_ROUNDED;
    t->truncate = 1;
    t->zebra = 1;
}

void tui_table_header(TuiTable *t, int col, const char *header) {
    if (col < 0 || col >= t->ncols) return;
    t->headers[col] = header;
    int w = utf8_width(header ? header : "");
    if (w > t->widths[col]) t->widths[col] = w;
}

void tui_table_align(TuiTable *t, int col, TuiAlign align) {
    if (col < 0 || col >= t->ncols) return;
    t->aligns[col] = align;
}

void tui_table_header_color(TuiTable *t, int col, const char *ansi) {
    if (col < 0 || col >= t->ncols) return;
    t->header_colors[col] = ansi;
}

void tui_table_cell_color(TuiTable *t, int col, const char *ansi) {
    if (col < 0 || col >= t->ncols) return;
    t->cell_colors[col] = ansi;
}

void tui_table_row(TuiTable *t, const char *const *cells) {
    if (t->nrows >= TUI_MAX_ROWS) return;
    for (int c = 0; c < t->ncols; c++) {
        const char *v = cells[c] ? cells[c] : "";
        t->rows[t->nrows][c] = v;
        int w = utf8_width(v);
        if (w > t->widths[c]) t->widths[c] = w;
    }
    t->nrows++;
}

static void render_none(const TuiTable *t, const int *w, FILE *out) {
    const char *hfg = t->use_color ? TUI_HDR_DEFAULT : NULL;
    for (int c = 0; c < t->ncols; c++) {
        print_cell(out, t->headers[c], w[c], t->aligns[c], t->truncate,
                   hfg, NULL);
        fputs("  ", out);
    }
    fputc('\n', out);
    for (int c = 0; c < t->ncols; c++) {
        for (int i = 0; i < w[c]; i++) fputc('-', out);
        fputs("  ", out);
    }
    fputc('\n', out);

    const char *zebra = (t->use_color && t->zebra) ? TUI_BG_ZEBRA : NULL;
    for (int r = 0; r < t->nrows; r++) {
        for (int c = 0; c < t->ncols; c++) {
            const char *fg = t->use_color ? t->cell_colors[c] : NULL;
            const char *bg = zebra && (r % 2 == 0) ? zebra : NULL;
            print_cell(out, t->rows[r][c], w[c], t->aligns[c],
                       t->truncate, fg, bg);
            fputs("  ", out);
        }
        fputc('\n', out);
    }
}

static void render_box(const TuiTable *t, const int *w, FILE *out) {
    const char *h = box_gl(t, 9), *v = box_gl(t, 10);
    const char *hfg = t->use_color ? TUI_HDR_DEFAULT : NULL;
    const char *zebra = (t->use_color && t->zebra) ? TUI_BG_ZEBRA : NULL;

    fputs(box_gl(t, 0), out);
    for (int c = 0; c < t->ncols; c++) {
        for (int i = 0; i < w[c] + TUI_PAD; i++) fputs(h, out);
        fputs(c + 1 < t->ncols ? box_gl(t, 4) : box_gl(t, 1), out);
    }
    fputc('\n', out);

    fputs(v, out);
    for (int c = 0; c < t->ncols; c++) {
        fputc(' ', out);
        print_cell(out, t->headers[c], w[c], t->aligns[c], t->truncate,
                   hfg, NULL);
        fputc(' ', out);
        fputs(v, out);
    }
    fputc('\n', out);

    fputs(box_gl(t, 6), out);
    for (int c = 0; c < t->ncols; c++) {
        for (int i = 0; i < w[c] + TUI_PAD; i++) fputs(h, out);
        fputs(c + 1 < t->ncols ? box_gl(t, 8) : box_gl(t, 7), out);
    }
    fputc('\n', out);

    for (int r = 0; r < t->nrows; r++) {
        fputs(v, out);
        for (int c = 0; c < t->ncols; c++) {
            const char *fg = t->use_color ? t->cell_colors[c] : NULL;
            const char *bg = zebra && (r % 2 == 0) ? zebra : NULL;
            fputc(' ', out);
            print_cell(out, t->rows[r][c], w[c], t->aligns[c],
                       t->truncate, fg, bg);
            fputc(' ', out);
            fputs(v, out);
        }
        fputc('\n', out);
    }

    fputs(box_gl(t, 2), out);
    for (int c = 0; c < t->ncols; c++) {
        for (int i = 0; i < w[c] + TUI_PAD; i++) fputs(h, out);
        fputs(c + 1 < t->ncols ? box_gl(t, 5) : box_gl(t, 3), out);
    }
    fputc('\n', out);
}

void tui_table_render(const TuiTable *t, FILE *out) {
    if (!t || t->ncols <= 0) return;
    int w[TUI_MAX_COLS];
    int total = 0;
    for (int c = 0; c < t->ncols; c++) {
        w[c] = t->widths[c];
        total += w[c] + TUI_PAD;
    }
    if (t->border != TUI_BORDER_NONE) total += t->ncols + 1;
    shrink_widths(t, w, &total);

    if (t->border == TUI_BORDER_NONE)
        render_none(t, w, out);
    else
        render_box(t, w, out);
}
