/* PDF export — embedded CJK font (TrueType subsetting).
 *
 * The non-embedded STSong-Light path only renders correctly in viewers that
 * ship Adobe-GB1 CMaps, and this writer emits Unicode scalars as CIDs, which
 * those CMaps mis-map. So when the document uses CJK glyphs we embed a subset
 * of a real system TrueType font instead, keeping original glyph IDs so that
 * composite glyphs stay valid without renumbering. */
#include "pdf_int.h"
#include "miniz.h"
#include <math.h>

/* Bitmap of BMP codepoints drawn with the CJK font (F3); used to decide
 * whether a font subset must be embedded. Single-threaded CLI use. */
static uint8_t g_cjk_used[8192];

void cjk_mark(uint32_t cp) {
    if (cp <= 0xFFFF) g_cjk_used[cp >> 3] |= (uint8_t)(1u << (cp & 7));
}

int cjk_any_used(void) {
    for (size_t i = 0; i < sizeof(g_cjk_used); i++)
        if (g_cjk_used[i]) return 1;
    return 0;
}

void cjk_reset(void) { memset(g_cjk_used, 0, sizeof(g_cjk_used)); }

void cjkfont_free(CjkFont *cf) {
    free(cf->font);
    free(cf->cidmap);
    db_free(&cf->widths);
    memset(cf, 0, sizeof(*cf));
}

static uint16_t be16v(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32v(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

typedef struct {
    const unsigned char *img;   /* whole file image */
    size_t len;
    const unsigned char *sfnt;  /* font start (past TTC header when present) */
} Ttf;

static const unsigned char *ttf_find(const Ttf *t, const char *tag, uint32_t *len) {
    uint32_t n = be16v(t->sfnt + 4);
    const unsigned char *dir = t->sfnt + 12;
    for (uint32_t i = 0; i < n; i++) {
        const unsigned char *e = dir + i * 16;
        if (!memcmp(e, tag, 4)) {
            uint32_t off = be32v(e + 8), ln = be32v(e + 12);
            if (off > t->len || ln > t->len - off) return NULL;
            *len = ln;
            /* table offsets are absolute in a TTC; relative elsewhere (img==sfnt) */
            return t->img + off;
        }
    }
    return NULL;
}

static const unsigned char *ttf_cmap_sub(const Ttf *t, int want12, uint32_t *avail) {
    uint32_t clen;
    const unsigned char *cm = ttf_find(t, "cmap", &clen);
    if (!cm || clen < 4) return NULL;
    uint16_t n = be16v(cm + 2);
    for (uint16_t i = 0; i < n; i++) {
        const unsigned char *r = cm + 4 + (size_t)i * 8;
        if (r + 8 > cm + clen) break;
        uint16_t pid = be16v(r), eid = be16v(r + 2);
        uint32_t off = be32v(r + 4);
        if (off >= clen) continue;
        if (!((pid == 3 && (eid == 1 || eid == 10)) || pid == 0)) continue;
        const unsigned char *s = cm + off;
        if (s + 2 > cm + clen) continue;
        uint16_t fmt = be16v(s);
        if (fmt == (want12 ? 12 : 4)) { *avail = clen - off; return s; }
    }
    return NULL;
}

static uint16_t cmap4_lookup(const unsigned char *s, uint32_t avail, uint32_t cp) {
    if (cp > 0xFFFF) return 0;
    uint16_t segx2 = be16v(s + 6);
    uint32_t seg = segx2 / 2;
    const unsigned char *ends = s + 14;
    const unsigned char *starts = ends + segx2 + 2;
    const unsigned char *deltas = starts + segx2;
    const unsigned char *ranges = deltas + segx2;
    if (ranges + segx2 + segx2 > s + avail) return 0;
    for (uint32_t i = 0; i < seg; i++) {
        if ((uint32_t)be16v(ends + i * 2) < cp) continue;
        if ((uint32_t)be16v(starts + i * 2) > cp) return 0;
        uint16_t ro = be16v(ranges + i * 2);
        if (ro == 0) return (uint16_t)(cp + be16v(deltas + i * 2));
        const unsigned char *g = ranges + i * 2 + ro + (cp - be16v(starts + i * 2)) * 2;
        if (g + 2 > s + avail) return 0;
        uint16_t r = be16v(g);
        return r ? (uint16_t)(r + be16v(deltas + i * 2)) : 0;
    }
    return 0;
}

static uint16_t cmap12_lookup(const unsigned char *s, uint32_t avail, uint32_t cp) {
    if (avail < 16) return 0;
    uint32_t ng = be32v(s + 12);
    if (avail < 16 + (size_t)ng * 12) return 0;
    for (uint32_t i = 0; i < ng; i++) {
        const unsigned char *g = s + 16 + (size_t)i * 12;
        if (cp >= be32v(g) && cp <= be32v(g + 4))
            return (uint16_t)(be32v(g + 8) + (cp - be32v(g)));
    }
    return 0;
}

static uint16_t ttf_gid(const Ttf *t, uint32_t cp) {
    uint32_t a4 = 0, a12 = 0;
    const unsigned char *f12 = ttf_cmap_sub(t, 1, &a12);
    if (f12) {
        uint16_t g = cmap12_lookup(f12, a12, cp);
        if (g) return g;
    }
    const unsigned char *f4 = ttf_cmap_sub(t, 0, &a4);
    if (f4 && cp <= 0xFFFF) return cmap4_lookup(f4, a4, cp);
    return 0;
}

static uint32_t loca_get(const unsigned char *loca, int fmt, uint32_t i) {
    return fmt ? be32v(loca + i * 4) : (uint32_t)be16v(loca + i * 2) * 2u;
}

static void mark_glyph(uint8_t *keep, uint32_t numGlyphs,
                       const unsigned char *glyf, uint32_t glyf_len,
                       const unsigned char *loca, int locaFmt,
                       uint32_t gid, int depth) {
    if (gid >= numGlyphs || keep[gid] || depth > 8) return;
    keep[gid] = 1;
    uint32_t gs = loca_get(loca, locaFmt, gid), ge = loca_get(loca, locaFmt, gid + 1);
    if (ge <= gs || gs > glyf_len || ge > glyf_len) return;
    const unsigned char *g = glyf + gs;
    if ((int16_t)be16v(g) >= 0) return;   /* simple glyph */
    const unsigned char *p = g + 10;      /* composite component list */
    for (;;) {
        if (p + 4 > g + ge) return;
        uint16_t flags = be16v(p);
        mark_glyph(keep, numGlyphs, glyf, glyf_len, loca, locaFmt, be16v(p + 2), depth + 1);
        p += 4 + (flags & 0x0040 ? 8 : (flags & (0x0001 | 0x0008)) ? 4 : 2);
        if (!(flags & 0x0020)) break;          /* MORE_COMPONENTS */
    }
}

static unsigned char *read_whole_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 12 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    unsigned char *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return NULL;
    }
    fclose(fp);
    *out_len = (size_t)sz;
    return buf;
}

/* env override, then a font shipped in the config dir, then common system fonts */
static int ttf_load(Ttf *t, unsigned char **img, size_t *ilen) {
    char cfgpath[1024];
    static const char *cands[] = {
        "/mnt/c/Windows/Fonts/msyh.ttc",
        "/mnt/c/Windows/Fonts/simhei.ttf",
        "/mnt/c/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        NULL
    };
    const char *env = getenv("GITSIGHT_PDF_FONT");
    char *try_paths[3 + 9];
    int nt = 0;
    if (env && env[0]) try_paths[nt++] = (char *)env;
    {
        char base[1024];
        config_get_default_path(base, sizeof(base));
        char *slash = strrchr(base, '/');
        if (slash) {
            *slash = '\0';
            snprintf(cfgpath, sizeof(cfgpath), "%s/fonts/cjk.ttf", base);
            try_paths[nt++] = cfgpath;
        }
    }
    for (int i = 0; cands[i]; i++) try_paths[nt++] = (char *)cands[i];
    try_paths[nt] = NULL;

    for (int i = 0; i < nt; i++) {
        size_t len;
        unsigned char *buf = read_whole_file(try_paths[i], &len);
        if (!buf) continue;
        const unsigned char *sfnt = buf;
        if (len >= 16 && !memcmp(buf, "ttcf", 4)) {
            uint32_t fo = be32v(buf + 12);   /* first font in collection */
            if (fo == 0 || fo + 12 > len) { free(buf); continue; }
            sfnt = buf + fo;
        }
        uint32_t ver = be32v(sfnt);
        if (ver != 0x00010000u && ver != 0x74727565u /* 'true' */) { free(buf); continue; }
        *img = buf;
        *ilen = len;
        t->img = buf;
        t->len = len;
        t->sfnt = sfnt;
        return 1;
    }
    return 0;
}

static int flate_mem(const unsigned char *src, size_t srclen,
                     unsigned char **dst, size_t *dstlen) {
    mz_ulong bound = mz_compressBound((mz_ulong)srclen);
    unsigned char *buf = malloc(bound);
    if (!buf) return 0;
    mz_ulong dl = bound;
    if (mz_compress(buf, &dl, src, (mz_ulong)srclen) != MZ_OK) {
        free(buf);
        return 0;
    }
    *dst = buf;
    *dstlen = (size_t)dl;
    return 1;
}

static void put_be16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* Build the subset font, CID->GID map and /W entries for the marked codepoints. */
int cjk_build(CjkFont *cf) {
    Ttf t;
    unsigned char *img = NULL;
    size_t ilen = 0;
    if (!ttf_load(&t, &img, &ilen)) return 0;

    int ok = 0;
    uint32_t len, hlen, hhealen, maxplen, localen, glyflen;
    const unsigned char *head = ttf_find(&t, "head", &len);
    const unsigned char *hhea = ttf_find(&t, "hhea", &hhealen);
    const unsigned char *maxp = ttf_find(&t, "maxp", &maxplen);
    const unsigned char *hmtx = ttf_find(&t, "hmtx", &hlen);
    const unsigned char *loca = ttf_find(&t, "loca", &localen);
    const unsigned char *glyf = ttf_find(&t, "glyf", &glyflen);
    if (!head || len < 54 || !hhea || hhealen < 36 || !maxp || maxplen < 6 ||
        !hmtx || !loca || !glyf) goto done;

    uint32_t numGlyphs = be16v(maxp + 4);
    if (!numGlyphs) goto done;
    int locaFmt = (int16_t)be16v(head + 50);
    if (locaFmt != 0 && locaFmt != 1) goto done;
    /* Trust the actual table size over the flag when it is unambiguous. */
    if (localen == (numGlyphs + 1) * 4u) locaFmt = 1;
    else if (localen == (numGlyphs + 1) * 2u) locaFmt = 0;
    if (localen < (uint32_t)(numGlyphs + 1) * (locaFmt ? 4u : 2u)) goto done;
    uint16_t upem = be16v(head + 18);
    if (!upem) upem = 1000;
    uint16_t nhm = be16v(hhea + 34);
    if (!nhm || hlen < (uint32_t)nhm * 4) goto done;

    uint8_t *keep = calloc(numGlyphs, 1);
    if (!keep) goto done;

    db_init(&cf->widths);
    unsigned char *cidmap = calloc(65536 * 2, 1);
    if (!cidmap) { free(keep); goto done; }

    /* mark used glyphs + collect widths and CID->GID entries */
    for (int w = 0; w < 8192; w++) {
        uint8_t bits = g_cjk_used[w];
        while (bits) {
            uint32_t cp = (uint32_t)w * 8 + (uint32_t)__builtin_ctz(bits);
            bits &= (uint8_t)(bits - 1);
            uint16_t gid = ttf_gid(&t, cp);
            if (!gid) continue;
            mark_glyph(keep, numGlyphs, glyf, glyflen, loca, locaFmt, gid, 0);
            put_be16(cidmap + cp * 2, gid);
            uint32_t adv = be16v(hmtx + (gid < nhm ? gid : nhm - 1) * 4);
            db_printf(&cf->widths, "%u %u %d ", cp, cp, (int)(adv * 1000 / upem));
        }
    }

    /* subset glyf, keeping original glyph IDs (unused slots collapse) */
    DynBuf gsub;
    db_init(&gsub);
    uint32_t *newloca = malloc((size_t)(numGlyphs + 1) * 4);
    if (!newloca) { free(cidmap); free(keep); goto done; }
    for (uint32_t gid = 0; gid < numGlyphs; gid++) {
        newloca[gid] = (uint32_t)gsub.len;
        if (!keep[gid]) continue;
        uint32_t gs = loca_get(loca, locaFmt, gid), ge = loca_get(loca, locaFmt, gid + 1);
        if (ge > gs && ge <= glyflen) {
            db_mem(&gsub, glyf + gs, ge - gs);
            while (gsub.len & 3) db_putc(&gsub, 0);
        }
    }
    newloca[numGlyphs] = (uint32_t)gsub.len;

    /* assemble sfnt: head hhea maxp hmtx loca glyf */
    static const char *tabs[] = { "head", "hhea", "maxp", "hmtx", "loca", "glyf" };
    uint32_t tlen[6];
    const unsigned char *tptr[6];
    unsigned char *headcopy = NULL;
    unsigned char *locacopy = NULL;
    tlen[0] = len;   tptr[0] = head;
    tlen[1] = hhealen; tptr[1] = hhea;
    tlen[2] = maxplen; tptr[2] = maxp;
    tlen[3] = hlen;    tptr[3] = hmtx;
    tlen[4] = (numGlyphs + 1) * 4;
    tlen[5] = (uint32_t)gsub.len;
    headcopy = malloc(len);
    locacopy = malloc(tlen[4]);
    if (!headcopy || !locacopy) { free(headcopy); free(locacopy); free(newloca); free(cidmap); free(keep); db_free(&gsub); goto done; }
    memcpy(headcopy, head, len);
    put_be32(headcopy + 8, 0);            /* checkSumAdjustment */
    put_be16(headcopy + 50, 1);           /* indexToLocFormat: long (4-byte loca entries) */
    tptr[0] = headcopy;
    for (uint32_t i = 0; i <= numGlyphs; i++) put_be32(locacopy + i * 4, newloca[i]);
    tptr[4] = locacopy;
    tptr[5] = (const unsigned char *)gsub.data;

    size_t total = 12 + 6 * 16;
    for (int i = 0; i < 6; i++) total += (tlen[i] + 3) & ~3u;
    unsigned char *sfnt = malloc(total ? total : 1);
    if (!sfnt) { free(headcopy); free(locacopy); free(newloca); free(cidmap); free(keep); db_free(&gsub); goto done; }
    put_be32(sfnt, 0x00010000u);
    put_be16(sfnt + 4, 6);
    uint32_t off = 12 + 6 * 16;
    for (int i = 0; i < 6; i++) {
        unsigned char *e = sfnt + 12 + i * 16;
        memcpy(e, tabs[i], 4);
        put_be32(e + 4, 0);               /* checkSum (viewers ignore) */
        put_be32(e + 8, off);
        put_be32(e + 12, tlen[i]);
        memcpy(sfnt + off, tptr[i], tlen[i]);
        off += (tlen[i] + 3) & ~3u;
    }

    /* compress font program and CIDToGIDMap */
    ok = flate_mem(sfnt, off, &cf->font, &cf->font_len) &&
         flate_mem(cidmap, 65536 * 2, &cf->cidmap, &cf->cidmap_len);
    if (ok) {
        double sc = 1000.0 / upem;
        cf->bbox[0] = (int32_t)((int16_t)be16v(head + 36) * sc);
        cf->bbox[1] = (int32_t)((int16_t)be16v(head + 38) * sc);
        cf->bbox[2] = (int32_t)((int16_t)be16v(head + 40) * sc);
        cf->bbox[3] = (int32_t)((int16_t)be16v(head + 42) * sc);
        cf->asc = (int32_t)((int16_t)be16v(hhea + 4) * sc);
        cf->desc = (int32_t)((int16_t)be16v(hhea + 6) * sc);
    }

    free(sfnt);
    free(headcopy);
    free(locacopy);
    free(newloca);
    free(cidmap);
    free(keep);
    db_free(&gsub);

done:
    free(img);
    return ok;
}
