/* PDF export — dynamic buffer, colors, UTF-8 text measurement. */
#include "pdf_int.h"
#include <math.h>

/* ---- Dynamic Buffer ---- */
void db_init(DynBuf *b) { memset(b, 0, sizeof(*b)); }
void db_free(DynBuf *b) { free(b->data); }

int db_grow(DynBuf *b, size_t need) {
    if (b->len + need <= b->cap) return 1;
    size_t nc = b->cap ? b->cap : 4096;
    while (nc < b->len + need) nc *= 2;
    char *p = realloc(b->data, nc);
    if (!p) return 0;
    b->data = p;
    b->cap = nc;
    return 1;
}

/* Buffer invariant: b->data[b->len] is always kept as '\0' so the buffer can
 * be used as a C string (e.g. its whole content mass). Growth reserves room. */
static void db_nul(DynBuf *b) {
    if (b->len >= b->cap)
        db_grow(b, 1);
    if (b->len < b->cap)
        b->data[b->len] = '\0';
}

void db_puts(DynBuf *b, const char *s) {
    size_t n = strlen(s);
    if (db_grow(b, n + 1)) { memcpy(b->data + b->len, s, n); b->len += n; }
    db_nul(b);
}

void db_putc(DynBuf *b, char c) {
    if (db_grow(b, 1)) b->data[b->len++] = c;
    db_nul(b);
}

/* Append raw bytes (may contain NUL) — for binary streams. */
void db_mem(DynBuf *b, const void *data, size_t n) {
    if (db_grow(b, n + 1)) { memcpy(b->data + b->len, data, n); b->len += n; }
    db_nul(b);
}

__attribute__((format(printf,2,3)))
void db_printf(DynBuf *b, const char *fmt, ...) {
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
const PdfColor PC_BLACK  = {0, 0, 0};
const PdfColor PC_BLUE   = {25, 55, 160};
const PdfColor PC_GRAY   = {90, 90, 90};
const PdfColor PC_LINK   = {17, 85, 204};
const PdfColor PC_OPEN   = {26, 127, 55};
const PdfColor PC_CLOSED = {207, 34, 46};
const PdfColor PC_MERGED = {130, 80, 223};

/* ---- UTF-8 helpers ---- */
/* Decode one UTF-8 codepoint from s; advance s past it; return codepoint */
uint32_t utf8_decode(const unsigned char **sp) {
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

/* Map a Unicode codepoint to a WinAnsi (cp1252) byte, or -1 when it is not
 * representable in WinAnsi (e.g. CJK, emoji). WinAnsi covers ASCII,
 * Latin-1 (U+00A0..U+00FF) and the common cp1252 punctuation such as
 * em/en dashes, curly quotes, ellipsis and currency symbols. */
int cpansi_byte(uint32_t cp) {
    if (cp <= 0x7F) return (int)cp;
    if (cp >= 0xA0 && cp <= 0xFF) return (int)cp;
    switch (cp) {
        case 0x20AC: return 0x80; /* € */
        case 0x201A: return 0x82; /* ‚ */
        case 0x0192: return 0x83; /* ƒ */
        case 0x201E: return 0x84; /* „ */
        case 0x2026: return 0x85; /* … */
        case 0x2020: return 0x86; /* † */
        case 0x2021: return 0x87; /* ‡ */
        case 0x02C6: return 0x88; /* ˆ */
        case 0x2030: return 0x89; /* ‰ */
        case 0x0160: return 0x8A; /* Š */
        case 0x2039: return 0x8B; /* ‹ */
        case 0x0152: return 0x8C; /* Œ */
        case 0x017D: return 0x8E; /* Ž */
        case 0x2018: return 0x91; /* ‘ */
        case 0x2019: return 0x92; /* ’ */
        case 0x201C: return 0x93; /* “ */
        case 0x201D: return 0x94; /* ” */
        case 0x2022: return 0x95; /* • */
        case 0x2013: return 0x96; /* – */
        case 0x2014: return 0x97; /* — */
        case 0x02DC: return 0x98; /* ˜ */
        case 0x2122: return 0x99; /* ™ */
        case 0x0161: return 0x9A; /* š */
        case 0x203A: return 0x9B; /* › */
        case 0x0153: return 0x9C; /* œ */
        case 0x017E: return 0x9E; /* ž */
        case 0x0178: return 0x9F; /* Ÿ */
        default: return -1;
    }
}

/* approximate width of one CP in Helvetica at font size fs */
float cp_width(uint32_t cp, float fs) {
    if (cp <= 0x7F) {
        if (cp == ' ') return fs * 0.28f;
        if (cp == 'I') return fs * 0.25f;
        if (cp == 'M' || cp == 'W') return fs * 0.72f;
        if (cp >= 'A' && cp <= 'Z') return fs * 0.60f;
        if (cp >= 'a' && cp <= 'z') return fs * 0.50f;
        if (cp >= '0' && cp <= '9') return fs * 0.55f;
        return fs * 0.50f;
    }
    int b = cpansi_byte(cp);
    if (b < 0) {
        if (cp <= 0xFFFF) return fs;
        return fs * 1.5f;
    }
    if (b >= 0x80 && b <= 0x9F) {
        switch (b) {
            case 0x85: return fs * 1.5f;  /* … */
            case 0x99: return fs * 1.2f;  /* ™ */
            case 0x96: return fs * 0.55f; /* – */
            case 0x97: return fs * 0.55f; /* — */
            default:   return fs * 0.50f;
        }
    }
    if (b == 0xA0) return fs * 0.28f;     /* nbsp */
    if (b >= 0xC0 && b <= 0xDF) return fs * 0.62f; /* Á Á É Í Ó Ú ..., accented caps */
    return fs * 0.50f;
}

/* Helvetica / Helvetica-Bold AFM advance widths (1/1000 em), ASCII 32..126.
 * Needed when text is positioned token-by-token (markdown rendering): the
 * coarse approximations in cp_width accumulate drift across a line. */
static const uint16_t hw_reg[95] = {
    278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
    1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
    333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
    556,556,333,500,278,556,500,722,500,500,500,334,260,334,584
};
static const uint16_t hw_bold[95] = {
    278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,
    975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,
    333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,
    611,611,389,556,333,611,556,778,556,556,500,389,280,389,584
};

/* Exact-width variant for ASCII; falls back to cp_width for other scripts. */
float cp_width_f(uint32_t cp, float fs, int bold) {
    if (cp >= 32 && cp < 32 + 95)
        return (bold ? hw_bold : hw_reg)[cp - 32] * fs / 1000.0f;
    return cp_width(cp, fs) * (bold ? 1.06f : 1.0f);
}

/* approximate string width at given font size */
float str_width(const char *s, float fs) {
    float w = 0;
    const unsigned char *p = (const void*)s;
    while (*p) {
        uint32_t cp = utf8_decode(&p);
        w += cp_width(cp, fs);
    }
    return w;
}

/* Copy into `out` the longest prefix of `s` (at codepoint boundaries) that
 * fits within `max_w` at font size `fs`. Appends "..." when truncated.
 * Returns the number of bytes copied (including the ellipsis, if any). */
size_t ellipsize(const char *s, float max_w, float fs, char *out, size_t cap) {
    float w = 0;
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp = utf8_decode(&p);
        float cw = cp_width(cp, fs);
        if (w + cw > max_w) break;
        w += cw;
        n = (size_t)((const char *)p - s);
    }
    size_t full = strlen(s);
    if (n >= full || max_w <= 0) {
        if (n >= full) {
            memcpy(out, s, full + 1);
            return full;
        }
        memcpy(out, "...", 4);
        return 3;
    }
    if (n + 3 > cap - 1) n = (cap > 4) ? cap - 4 : 0;
    memcpy(out, s, n);
    out[n] = '.'; out[n+1] = '.'; out[n+2] = '.';
    out[n+3] = '\0';
    return n + 3;
}

/* Format a long with thousands separators. */
void fmt_int(char *buf, size_t cap, long v) {
    char tmp[32];
    int neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-(long long)v) : (unsigned long)v;
    snprintf(tmp, sizeof(tmp), "%lu", u);
    size_t n = strlen(tmp);
    size_t pos = 0;
    if (neg && pos < cap - 1) buf[pos++] = '-';
    int c = 0;
    for (int i = (int)n - 1; i >= 0; i--) {
        if (pos >= cap - 1) break;
        buf[pos++] = tmp[i];
        if (++c == 3 && i > 0) {
            if (pos < cap - 1) buf[pos++] = ',';
            c = 0;
        }
    }
    buf[pos] = '\0';
    size_t start = neg ? 1 : 0;
    for (size_t a = start, b = pos - 1; a < b; a++, b--) {
        char t = buf[a]; buf[a] = buf[b]; buf[b] = t;
    }
}
