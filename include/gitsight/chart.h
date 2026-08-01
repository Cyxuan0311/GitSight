#ifndef GITSIGHT_CHART_H
#define GITSIGHT_CHART_H

#include <stdio.h>

/* Generic unicode bar-chart renderer, decoupled from the rest of GitSight.
 * Feed it any labels + values; it draws the chart to a FILE*. */

typedef enum {
    CHART_HBAR,   /* horizontal bars:  label ██████████░░ 1200 tokens */
    CHART_VBAR,   /* vertical columns: column chart with bottom axis  */
} ChartType;

typedef struct {
    ChartType type;
    int  width;   /* HBAR: max bar length in cells (default 24);
                     VBAR: number of vertical rows (default 8) */
    int  color;   /* nonzero: colorize the bars with ANSI */
    const char *unit;  /* optional value suffix (e.g. " tokens"), may be NULL */
} ChartOptions;

/* Render a chart. `title` may be NULL/empty to omit. Values < 0 are clamped
 * to 0; a chart of all-zero values draws empty bars but keeps labels. */
void chart_render(FILE *out, const char *title,
                  const char *const *labels, const long long *values, int n,
                  const ChartOptions *opts);

#endif
