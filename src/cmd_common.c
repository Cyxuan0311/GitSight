#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/color.h"
#include "gitsight/anim.h"
#include "gitsight/tui.h"
#include "gitsight/export/exporter.h"
#include "gitsight/export/pdf.h"
#include "gitsight/export/xlsx.h"
#include "gitsight/thread.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static const struct { const char *name; int rgb[3]; } NAMED_COLORS[] = {
    {"black",  {0,0,0}},     {"white",   {255,255,255}}, {"red",    {220,38,41}},
    {"green",  {26,127,55}}, {"blue",    {37,86,196}},   {"yellow", {212,167,21}},
    {"orange", {230,126,34}},{"purple",  {130,80,223}},  {"gray",   {119,119,119}},
    {"grey",   {119,119,119}},{"pink",   {234,120,181}}, {"brown",  {121,85,72}},
    {"cyan",   {23,178,178}},{"magenta", {186,59,212}},  {"gold",   {180,140,20}},
    {"navy",   {16,32,96}},  {"teal",   {0,96,96}},     {"silver", {192,192,192}},
    {"maroon", {128,0,0}},   {"olive",  {107,107,0}},   {NULL, {0,0,0}},
};

static int hex_nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Accepts "R,G,B", "#rrggbb", or a color name; unknown -> black. */
static void color_str_to_rgb(const char *s, unsigned char out[3]) {
    int r, g, b;
    if (sscanf(s, "%d,%d,%d", &r, &g, &b) == 3) {
        out[0] = (unsigned char)r; out[1] = (unsigned char)g; out[2] = (unsigned char)b;
        return;
    }
    if (s[0] == '#') s++;
    int h0 = hex_nib(s[0]), h1 = hex_nib(s[1]), h2 = hex_nib(s[2]), h3 = hex_nib(s[3]),
        h4 = hex_nib(s[4]), h5 = hex_nib(s[5]);
    if (s[6] == '\0' && h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0 && h5 >= 0) {
        out[0] = (unsigned char)(h0 * 16 + h1);
        out[1] = (unsigned char)(h2 * 16 + h3);
        out[2] = (unsigned char)(h4 * 16 + h5);
        return;
    }
    for (int i = 0; NAMED_COLORS[i].name; i++) {
        if (strcasecmp(s, NAMED_COLORS[i].name) == 0) {
            out[0] = (unsigned char)NAMED_COLORS[i].rgb[0];
            out[1] = (unsigned char)NAMED_COLORS[i].rgb[1];
            out[2] = (unsigned char)NAMED_COLORS[i].rgb[2];
            return;
        }
    }
    out[0] = 0; out[1] = 0; out[2] = 0;
}

int build_fopts(FormatOptions *fopts, const Config *cfg, int close_fp, const char *format) {
    memset(fopts, 0, sizeof(*fopts));
    /* txt/md carry ANSI escapes, so color is only sane when streaming to a
     * terminal; pdf/xlsx/html embed their own colors, independent of stdout. */
    int doc_color = format && (strcmp(format, "pdf") == 0 || strcmp(format, "xlsx") == 0 ||
                               strcmp(format, "html") == 0);
    if (doc_color)
        fopts->color_active = color_doc_active();
    else
        fopts->color_active = color_active() && !close_fp;
    fopts->font_size = 11.0f;
    fopts->page_width = 595.0f;
    fopts->page_height = 842.0f;
    fopts->item_limit = cfg->item_limit;
    fopts->extra = NULL;

    /* Build flags from section visibility */
    fopts->flags = 0;
    if (cfg->show_prs)       fopts->flags |= FMT_SHOW_PRS;
    if (cfg->show_commits)   fopts->flags |= FMT_SHOW_COMMITS;
    if (cfg->show_issues)    fopts->flags |= FMT_SHOW_ISSUES;
    if (cfg->show_summary)   fopts->flags |= FMT_SHOW_SUMMARY;
    if (cfg->show_top_labels) fopts->flags |= FMT_SHOW_LABELS;
    if (cfg->show_releases)   fopts->flags |= FMT_SHOW_RELEASES;
    if (cfg->show_contributors) fopts->flags |= FMT_SHOW_CONTRIBUTORS;
    if (cfg->show_languages)  fopts->flags |= FMT_SHOW_LANGS;

    /* Format-specific table width */
    if (strcmp(format, "md") == 0)
        fopts->table_width = cfg->md_table_width;
    else if (strcmp(format, "txt") == 0)
        fopts->table_width = cfg->txt_table_width;

    /* Format-specific extra config */
    if (strcmp(format, "pdf") == 0) {
        PdfExportConfig *pec = malloc(sizeof(PdfExportConfig));
        if (!pec) return 0;
        pec->margin = cfg->pdf_margin;
        pec->line_height = cfg->pdf_line_height;
        pec->col2_padding = cfg->pdf_col2_padding;
        color_str_to_rgb(cfg->pdf_title_color, pec->title_color);
        color_str_to_rgb(cfg->pdf_header_color, pec->header_color);
        color_str_to_rgb(cfg->pdf_label_color, pec->label_color);
        color_str_to_rgb(cfg->pdf_link_color, pec->link_color);
        color_str_to_rgb(cfg->pdf_open_color, pec->open_color);
        color_str_to_rgb(cfg->pdf_closed_color, pec->closed_color);
        color_str_to_rgb(cfg->pdf_merged_color, pec->merged_color);
        fopts->font_size = 11.0f;
        fopts->page_width = 595.0f;
        fopts->page_height = 842.0f;
        fopts->extra = pec;
    } else if (strcmp(format, "html") == 0) {
        HtmlExportConfig *hec = malloc(sizeof(HtmlExportConfig));
        if (!hec) return 0;
        color_str_to_rgb(cfg->html_title_color, hec->title_color);
        color_str_to_rgb(cfg->html_header_color, hec->header_color);
        color_str_to_rgb(cfg->html_label_color, hec->label_color);
        color_str_to_rgb(cfg->html_link_color, hec->link_color);
        color_str_to_rgb(cfg->html_open_color, hec->open_color);
        color_str_to_rgb(cfg->html_closed_color, hec->closed_color);
        color_str_to_rgb(cfg->html_merged_color, hec->merged_color);
        fopts->extra = hec;
    } else if (strcmp(format, "xlsx") == 0) {
        XlsxExportConfig *xec = malloc(sizeof(XlsxExportConfig));
        if (!xec) return 0;
        xec->font_size = cfg->xlsx_font_size;
        snprintf(xec->font_name, sizeof(xec->font_name), "%s", cfg->xlsx_font_name);
        xec->col_a_width = cfg->xlsx_col_a_width;
        xec->col_b_width = cfg->xlsx_col_b_width;
        xec->border = cfg->xlsx_border;
        snprintf(xec->sheet_name, sizeof(xec->sheet_name), "%s", cfg->xlsx_sheet_name);
        fopts->extra = xec;
    }
    return 1;
}

void free_fopts(FormatOptions *fopts) {
    free(fopts->extra);
    fopts->extra = NULL;
}

void get_config_path(char *buf, size_t len, const Options *opts) {
    if (opts->config_path[0]) {
        snprintf(buf, len, "%s", opts->config_path);
        return;
    }
    config_get_default_path(buf, len);
}

void print_batch_summary(const char *const *repos, const int *statuses, int n) {
    TuiTable t;
    char idx[32][8], result[32][8];
    const char *cells[3];
    tui_table_init(&t, 3);
    tui_table_header(&t, 0, "#");
    tui_table_header(&t, 1, "Repository");
    tui_table_header(&t, 2, "Result");
    tui_table_align(&t, 0, TUI_ALIGN_RIGHT);
    tui_table_align(&t, 2, TUI_ALIGN_CENTER);
    tui_table_cell_color(&t, 1, CLR_BLUE);
    for (int i = 0; i < n && i < 32; i++) {
        snprintf(idx[i], sizeof(idx[i]), "%d", i + 1);
        snprintf(result[i], sizeof(result[i]), "%s", statuses[i] == 0 ? "OK" : "FAIL");
        cells[0] = idx[i];
        cells[1] = repos[i];
        cells[2] = result[i];
        tui_table_row(&t, cells);
    }
    t.use_color = color_active();
    t.border = TUI_BORDER_ASCII;
    t.zebra = 1;
    printf("\nBatch summary:\n");
    tui_table_render(&t, stdout);
}

int batch_parallelism(const Options *opts, const Config *cfg) {
    int has_token = opts->token[0] || cfg->github_token[0] ||
                    cfg->gitlab_token[0] || cfg->codeberg_token[0];
    int par = opts->parallel;
    if (par == 0) par = has_token ? 4 : 1;
    if (par < 1) par = 1;
    if (par > GS_BATCH_MAX_THREADS) par = GS_BATCH_MAX_THREADS;
    if (par > 1 && !has_token && opts->parallel > 1)
        fprintf(stderr, "Warning: unauthenticated API access is rate limited (~60 req/h on github.com); "
                        "parallel runs may hit HTTP 403.\n");
    return par;
}

struct BatchPool {
    BatchTask   *tasks;
    int          n;
    atomic_int   next;
};

/* Run one task, handing back the full analysis when the caller wants it. */
int batch_run_one(BatchTask *t) {
    if (!t->result_out) return analyze_repo(&t->opts, *t->cfg, t->repo, t->batch, NULL);
    analysis_result_init(t->result_out);
    return analyze_repo(&t->opts, *t->cfg, t->repo, t->batch, t->result_out);
}

static void *batch_pool_worker(void *arg) {
    struct BatchPool *pool = (struct BatchPool *)arg;
    for (;;) {
        int i = atomic_fetch_add(&pool->next, 1);
        if (i >= pool->n) break;
        BatchTask *t = &pool->tasks[i];
        t->result = batch_run_one(t);
    }
    return NULL;
}

int run_batch_tasks(BatchTask *tasks, int n, int par, int quiet) {
    int rc = 0;
    if (par <= 1 || n <= 1) {
        for (int i = 0; i < n; i++) {
            if (!quiet) fprintf(stderr, "\n== %s ==\n", tasks[i].label);
            tasks[i].result = batch_run_one(&tasks[i]);
            rc |= tasks[i].result;
        }
        return rc;
    }

    /* Concurrent spinners would interleave; silence per-repo output and
     * report progress through the final batch summary table instead.
     *
     * Each repo is already analyzed on its own thread, so this also switches
     * its 7-resource fetch to serial: a batch of N repos then costs N threads
     * instead of N*7, with identical results. */
    anim_set_quiet(1);
    struct BatchPool pool;
    pool.tasks = tasks;
    pool.n = n;
    atomic_store(&pool.next, 0);

    for (int i = 0; i < n; i++)
        tasks[i].opts.serial_fetch = 1;

    int nthreads = par < n ? par : n;
    pthread_t th[GS_BATCH_MAX_THREADS];
    int created = 0;
    for (int i = 0; i < nthreads; i++) {
        if (gs_thread_create(&th[i], batch_pool_worker, &pool) == 0)
            created++;
    }
    for (int i = 0; i < created; i++)
        pthread_join(th[i], NULL);
    if (created == 0) {
        /* no threads could start: run everything serially on this thread */
        for (int i = 0; i < n; i++) {
            tasks[i].result = batch_run_one(&tasks[i]);
        }
    }
    anim_set_quiet(quiet);

    for (int i = 0; i < n; i++) rc |= tasks[i].result;
    return rc;
}
