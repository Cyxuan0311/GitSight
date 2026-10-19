#ifndef GITSIGHT_EXPORTER_H
#define GITSIGHT_EXPORTER_H

#include <stdio.h>
#include "../models.h"
#include "../error.h"

#define FMT_SHOW_PRS        (1<<0)
#define FMT_SHOW_COMMITS    (1<<1)
#define FMT_SHOW_ISSUES     (1<<2)
#define FMT_SHOW_SUMMARY    (1<<3)
#define FMT_SHOW_LABELS     (1<<4)
#define FMT_SHOW_RELEASES   (1<<5)
#define FMT_SHOW_CONTRIBUTORS (1<<6)
#define FMT_SHOW_LANGS      (1<<7)

typedef struct {
    int   color_active;
    int   flags;
    float font_size;
    float page_width;
    float page_height;
    int   table_width;
    int   item_limit;
    void *extra;
} FormatOptions;

typedef struct {
    ErrorCode (*write_report)(FILE *fp, const Report *report,
                              const Vector *prs, const Vector *commits,
                              const Vector *issues, const Vector *releases,
                              const FormatOptions *opts);
} Exporter;

/* Re-flow crammed markdown (a common LLM output shape): strips '\r' and
 * inserts a newline before "N. " items and "**" runs that directly follow
 * sentence punctuation. Writes at most outcap-1 bytes + NUL; returns the
 * output length. Used by the md/txt/pdf Summary renderers. */
size_t md_prettify_text(const char *in, char *out, size_t outcap);

ErrorCode exporter_init(Exporter *exporter, const char *format);

#endif
