#include "gitsight/chart.h"

#include <string.h>

#define CHART_FULL  "\xe2\x96\x88" /* █ */
#define CHART_EMPTY "\xe2\x96\x91" /* ░ */

static void render_hbar(FILE *out, const char *const *labels,
                        const long long *values, int n, const ChartOptions *o)
{
    long long max = 0;
    for (int i = 0; i < n; i++)
        if (values[i] > max) max = values[i];

    int lw = 0;
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(labels[i]);
        if (len > lw) lw = len;
    }

    int width = o->width > 0 ? o->width : 24;
    const char *on  = o->color ? "\x1b[36m" : "";
    const char *off = o->color ? "\x1b[0m"  : "";

    for (int i = 0; i < n; i++) {
        long long v = values[i] > 0 ? values[i] : 0;
        int filled = 0;
        if (max > 0) filled = (int)((long long)width * v / max);
        if (filled < 0) filled = 0;
        if (filled > width) filled = width;

        fprintf(out, "  %-*s ", lw, labels[i]);
        if (o->color) fputs(on, out);
        for (int c = 0; c < filled; c++) fputs(CHART_FULL, out);
        if (o->color) fputs(off, out);
        for (int c = filled; c < width; c++) fputs(CHART_EMPTY, out);
        if (o->unit)
            fprintf(out, "  %lld %s\n", v, o->unit);
        else
            fprintf(out, "  %lld\n", v);
    }
}

static void render_vbar(FILE *out, const char *const *labels,
                        const long long *values, int n, const ChartOptions *o)
{
    long long max = 0;
    for (int i = 0; i < n; i++)
        if (values[i] > max) max = values[i];

    int rows = o->width > 0 ? o->width : 8;
    int lw = 0;
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(labels[i]);
        if (len > lw) lw = len;
    }

    const char *on  = o->color ? "\x1b[36m" : "";
    const char *off = o->color ? "\x1b[0m"  : "";

    for (int r = rows; r >= 1; r--) {
        fputs("  ", out);
        for (int i = 0; i < n; i++) {
            long long v = values[i] > 0 ? values[i] : 0;
            int h = 0;
            if (max > 0) h = (int)((long long)rows * v / max);
            if (h < 0) h = 0;
            if (h > rows) h = rows;
            if (h >= r) {
                if (o->color) fputs(on, out);
                fputs(CHART_FULL, out);
                if (o->color) fputs(off, out);
            } else {
                fputc(' ', out);
            }
            fputc(' ', out);
        }
        fputc('\n', out);
    }

    fputs("  ", out);
    for (int i = 0; i < n; i++)
        fprintf(out, "%-*s ", lw, labels[i]);
    fputc('\n', out);

    fputs("  ", out);
    for (int i = 0; i < n; i++)
        fprintf(out, "%-*lld ", lw, values[i] > 0 ? values[i] : 0);
    fputc('\n', out);
}

void chart_render(FILE *out, const char *title,
                  const char *const *labels, const long long *values, int n,
                  const ChartOptions *opts)
{
    if (title && title[0])
        fprintf(out, "%s\n", title);

    if (n <= 0) {
        fprintf(out, "(no data)\n");
        return;
    }

    if (opts && opts->type == CHART_VBAR)
        render_vbar(out, labels, values, n, opts);
    else {
        ChartOptions h = { CHART_HBAR, 24, 0, NULL };
        if (opts) h = *opts;
        render_hbar(out, labels, values, n, &h);
    }
}
