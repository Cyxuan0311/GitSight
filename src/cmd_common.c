#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/color.h"
#include "gitsight/anim.h"
#include "gitsight/tui.h"
#include "gitsight/export/exporter.h"
#include "gitsight/export/pdf.h"
#include "gitsight/export/xlsx.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void color_str_to_rgb(const char *s, unsigned char out[3]) {
    int r, g, b;
    if (sscanf(s, "%d,%d,%d", &r, &g, &b) == 3) {
        out[0] = (unsigned char)r; out[1] = (unsigned char)g; out[2] = (unsigned char)b;
    } else {
        out[0] = 0; out[1] = 0; out[2] = 0;
    }
}

int build_fopts(FormatOptions *fopts, const Config *cfg, int close_fp, const char *format) {
    memset(fopts, 0, sizeof(*fopts));
    /* txt/md carry ANSI escapes, so color is only sane when streaming to a
     * terminal; pdf/xlsx use the flag for in-document color, files included. */
    int doc_color = format && (strcmp(format, "pdf") == 0 || strcmp(format, "xlsx") == 0);
    fopts->color_active = color_active() && (!close_fp || doc_color);
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

static void *batch_pool_worker(void *arg) {
    struct BatchPool *pool = (struct BatchPool *)arg;
    for (;;) {
        int i = atomic_fetch_add(&pool->next, 1);
        if (i >= pool->n) break;
        BatchTask *t = &pool->tasks[i];
        t->result = analyze_repo(&t->opts, *t->cfg, t->repo, t->batch, NULL);
    }
    return NULL;
}

int run_batch_tasks(BatchTask *tasks, int n, int par, int quiet) {
    int rc = 0;
    if (par <= 1 || n <= 1) {
        for (int i = 0; i < n; i++) {
            if (!quiet) fprintf(stderr, "\n== %s ==\n", tasks[i].label);
            tasks[i].result = analyze_repo(&tasks[i].opts, *tasks[i].cfg,
                                           tasks[i].repo, tasks[i].batch, NULL);
            rc |= tasks[i].result;
        }
        return rc;
    }

    /* Concurrent spinners would interleave; silence per-repo output and
     * report progress through the final batch summary table instead. */
    anim_set_quiet(1);
    struct BatchPool pool;
    pool.tasks = tasks;
    pool.n = n;
    atomic_store(&pool.next, 0);

    int nthreads = par < n ? par : n;
    pthread_t th[GS_BATCH_MAX_THREADS];
    int created = 0;
    for (int i = 0; i < nthreads; i++) {
        if (pthread_create(&th[i], NULL, batch_pool_worker, &pool) == 0)
            created++;
    }
    for (int i = 0; i < created; i++)
        pthread_join(th[i], NULL);
    if (created == 0) {
        /* no threads could start: run everything serially on this thread */
        for (int i = 0; i < n; i++) {
            tasks[i].result = analyze_repo(&tasks[i].opts, *tasks[i].cfg,
                                           tasks[i].repo, tasks[i].batch, NULL);
        }
    }
    anim_set_quiet(quiet);

    for (int i = 0; i < n; i++) rc |= tasks[i].result;
    return rc;
}
