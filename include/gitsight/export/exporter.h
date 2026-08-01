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

ErrorCode exporter_init(Exporter *exporter, const char *format);

#endif
