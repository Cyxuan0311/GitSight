#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/error.h"
#include "gitsight/color.h"
#include "gitsight/anim.h"
#include "gitsight/chart.h"
#include "gitsight/usage.h"
#include "gitsight/tui.h"
#include "gitsight/completions.h"
#include "gitsight/models.h"
#include "gitsight/container.h"
#include "gitsight/platform/client.h"
#include "gitsight/platform/http.h"
#include "gitsight/analyzer/engine.h"
#include "gitsight/export/exporter.h"
#include "gitsight/export/pdf.h"
#include "gitsight/export/xlsx.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <curl/curl.h>

#define GS_VERSION "0.1.0"

typedef struct {
    const Config *cfg;
    char owner[128];
    char repo[128];
    int  since_days;
    int  kind;              /* 0=prs 1=commits 2=issues 3=releases 4=meta 5=contributors */
    Vector *vec;
    RepoMeta *meta;
    PlatformClient pclient;
    int  client_ready;
    ErrorCode err;
} FetchJob;

static void *fetch_worker(void *arg) {
    FetchJob *j = (FetchJob *)arg;
    ErrorCode e = platform_client_init(&j->pclient, j->cfg, j->owner, j->repo);
    if (e != GS_OK) { j->err = e; return NULL; }
    j->client_ready = 1;
    switch (j->kind) {
        case 0: j->err = j->pclient.fetch_prs(j->pclient.ctx, j->vec, j->since_days); break;
        case 1: j->err = j->pclient.fetch_commits(j->pclient.ctx, j->vec, j->since_days); break;
        case 2: j->err = j->pclient.fetch_issues(j->pclient.ctx, j->vec, j->since_days); break;
        case 3: j->err = j->pclient.fetch_releases(j->pclient.ctx, j->vec, j->since_days); break;
        case 4: j->err = j->pclient.fetch_meta(j->pclient.ctx, j->meta); break;
        case 5: j->err = j->pclient.fetch_contributors(j->pclient.ctx, j->vec); break;
        default: j->err = ERR_INVALID_ARGS; break;
    }
    return NULL;
}

static void color_str_to_rgb(const char *s, unsigned char out[3]) {
    int r, g, b;
    if (sscanf(s, "%d,%d,%d", &r, &g, &b) == 3) {
        out[0] = (unsigned char)r; out[1] = (unsigned char)g; out[2] = (unsigned char)b;
    } else {
        out[0] = 0; out[1] = 0; out[2] = 0;
    }
}

static int build_fopts(FormatOptions *fopts, const Config *cfg, int close_fp, const char *format) {
    memset(fopts, 0, sizeof(*fopts));
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

static void free_fopts(FormatOptions *fopts) {
    free(fopts->extra);
    fopts->extra = NULL;
}

static int get_config_path(char *buf, size_t len, const Options *opts) {
    if (opts->config_path[0]) {
        snprintf(buf, len, "%s", opts->config_path);
        return 0;
    }
    config_get_default_path(buf, len);
    return 0;
}

static int analyze_repo(const Options *opts, Config cfg, const char *repo_arg, int batch, Report *report_out);

static int run_compare(const Options *opts, Config cfg, const char *repo_arg, int d1, int d2);

static void print_batch_summary(const char *const *repos, const int *statuses, int n) {
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

static int usage_day_is_empty(const UsageDay *d) {
    return d->calls == 0 && d->total_tokens == 0;
}

static int cmd_usage(const Options *opts) {
    char cfg_path[1024];
    get_config_path(cfg_path, sizeof(cfg_path), opts);

    char usage_path[1024];
    usage_path_from_config(cfg_path, usage_path, sizeof(usage_path));
    usage_set_file(usage_path);

    char eff_path[1024];
    usage_file_path(eff_path, sizeof(eff_path));

    color_init(opts->color_mode >= 0 ? opts->color_mode : 0);

    if (opts->usage_clear) {
        ErrorCode e = usage_clear(NULL);
        if (e != GS_OK) {
            fprintf(stderr, "Failed to clear usage log: %s\n", gs_error_string(e));
            return 1;
        }
        fprintf(stderr, "Usage log cleared: %s\n", eff_path);
        return 0;
    }

    UsageRecord *recs = NULL;
    long n = usage_load(NULL, &recs);
    if (n < 0) {
        fprintf(stderr, "Failed to read usage log: %s\n", eff_path);
        return 1;
    }

    if (n == 0) {
        fprintf(stderr, "No LLM usage recorded yet. Run an analysis with --llm first.\n");
        fprintf(stderr, "Usage log: %s\n", eff_path);
        usage_free(recs);
        return 0;
    }

    long long calls = 0, pt = 0, ct = 0, tt = 0;
    usage_totals(recs, n, &calls, &pt, &ct, &tt);

    if (opts->usage_json) {
        cJSON *root = cJSON_CreateObject();
        cJSON *total = cJSON_AddObjectToObject(root, "total");
        cJSON_AddNumberToObject(total, "calls", (double)calls);
        cJSON_AddNumberToObject(total, "prompt_tokens", (double)pt);
        cJSON_AddNumberToObject(total, "completion_tokens", (double)ct);
        cJSON_AddNumberToObject(total, "total_tokens", (double)tt);
        cJSON_AddNumberToObject(root, "records", (double)n);
        if (opts->usage_by_llm) {
            UsageGroup groups[USAGE_MAX_GROUPS];
            int g = usage_by_llm(recs, n, groups, USAGE_MAX_GROUPS);
            cJSON *arr = cJSON_AddArrayToObject(root, "by_llm");
            for (int i = 0; i < g; i++) {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "name", groups[i].name);
                cJSON_AddNumberToObject(o, "calls", (double)groups[i].calls);
                cJSON_AddNumberToObject(o, "prompt_tokens", (double)groups[i].prompt_tokens);
                cJSON_AddNumberToObject(o, "completion_tokens", (double)groups[i].completion_tokens);
                cJSON_AddNumberToObject(o, "total_tokens", (double)groups[i].total_tokens);
                cJSON_AddItemToArray(arr, o);
            }
        }
        char *str = cJSON_Print(root);
        if (str) { fputs(str, stdout); fputc('\n', stdout); free(str); }
        cJSON_Delete(root);
        usage_free(recs);
        return 0;
    }

    if (opts->usage_by_llm) {
        UsageGroup groups[USAGE_MAX_GROUPS];
        int g = usage_by_llm(recs, n, groups, USAGE_MAX_GROUPS);
        const char *labels[USAGE_MAX_GROUPS];
        long long vals[USAGE_MAX_GROUPS];
        for (int i = 0; i < g; i++) {
            labels[i] = groups[i].name;
            vals[i] = groups[i].total_tokens;
        }
        ChartOptions co = { CHART_HBAR, 24, color_active(), "tokens" };
        chart_render(stdout, "LLM usage by provider", labels, vals, g, &co);
        printf("\nTotal: %lld calls, %lld tokens (%lld prompt + %lld completion)\n",
               calls, tt, pt, ct);
        usage_free(recs);
        return 0;
    }

    int days = opts->usage_days > 0 ? opts->usage_days : 14;
    UsageDay *day = calloc((size_t)days, sizeof(UsageDay));
    if (!day) { usage_free(recs); return 1; }
    usage_by_day(recs, n, days, day);

    int lo = -1, hi = -1;
    for (int i = 0; i < days; i++) {
        if (usage_day_is_empty(&day[i])) continue;
        if (lo < 0) lo = i;
        hi = i;
    }

    if (lo < 0) {
        fprintf(stderr, "No LLM usage in the last %d days.\n", days);
        free(day);
        usage_free(recs);
        return 0;
    }

    int span = hi - lo + 1;
    const char **lbl = malloc((size_t)span * sizeof(const char *));
    long long *vals = malloc((size_t)span * sizeof(long long));
    char *label_buf = malloc((size_t)span * 16);
    for (int i = 0; i < span; i++) {
        time_t ds = (time_t)day[lo + i].day_start;
        struct tm *tmv = gmtime(&ds); /* not thread-safe, but this command is single-threaded */
        snprintf(label_buf + i * 16, 16, "%02d-%02d", tmv->tm_mon + 1, tmv->tm_mday);
        lbl[i] = label_buf + i * 16;
        vals[i] = day[lo + i].total_tokens;
    }

    char title[96];
    snprintf(title, sizeof(title), "LLM usage - last %d days", days);
    ChartOptions co = { CHART_VBAR, 8, color_active(), "tokens" };
    chart_render(stdout, title, lbl, vals, span, &co);
    printf("\nTotal: %lld calls, %lld tokens (%lld prompt + %lld completion)\n",
           calls, tt, pt, ct);

    free(lbl);
    free(vals);
    free(label_buf);
    free(day);
    usage_free(recs);
    return 0;
}

int main(int argc, char **argv) {
    Options opts;
    ErrorCode err = cli_parse(argc, argv, &opts);
    if (err != GS_OK) {
        if (err == ERR_INVALID_ARGS) return 1;
        return 1;
    }

    if (opts.command == 2) {
        printf("GitSight version %s\n", GS_VERSION);
        return 0;
    }

    if (opts.command == 5) {
        return cmd_usage(&opts);
    }

    if (opts.command == 4) {
        const char *s = opts.completion_shell;
        if (s[0] && strcmp(s, "bash") == 0) { fputs(gs_completion_bash, stdout); return 0; }
        if (s[0] && strcmp(s, "zsh") == 0)  { fputs(gs_completion_zsh, stdout);  return 0; }
        if (s[0] && strcmp(s, "fish") == 0) { fputs(gs_completion_fish, stdout); return 0; }
        fprintf(stderr, "Usage: gs completion <shell>\n");
        fprintf(stderr, "Available shells: bash, zsh, fish\n\n");
        fprintf(stderr, "  bash: gs completion bash > ~/.local/share/bash-completion/completions/gs\n");
        fprintf(stderr, "  zsh:  gs completion zsh > ~/.zfunc/_gs   (then add ~/.zfunc to fpath)\n");
        fprintf(stderr, "  fish: gs completion fish > ~/.config/fish/completions/gs.fish\n");
        return 1;
    }

    if (opts.command == 1) {
        char path[1024];
        get_config_path(path, sizeof(path), &opts);

        if (opts.list_llms) {
            Config cfg;
            config_init(&cfg);
            config_load(&cfg, path);
            if (cfg.llm_count == 0) {
                fprintf(stderr, "No LLM providers configured.\n");
            } else {
                fprintf(stderr, "Available LLM providers:\n");
                color_init(opts.color_mode >= 0 ? opts.color_mode : cfg.color_mode);
                config_list_llms(&cfg);
            }
            return 0;
        }

        if (opts.set_llm) {
            err = config_set_active_llm(path, opts.set_llm_value);
            if (err == GS_OK) {
                fprintf(stderr, "Active LLM set to '%s'\n", opts.set_llm_value);
            } else {
                fprintf(stderr, "Error: LLM provider '%s' not found.\n", opts.set_llm_value);
                return 1;
            }
            return 0;
        }

        if (opts.set_token) {
            err = config_set_token(path, opts.set_token_value);
            if (err == GS_OK) {
                fprintf(stderr, "GitHub token saved to %s\n", path);
            } else {
                fprintf(stderr, "Failed to save token: %s\n", gs_error_string(err));
                return 1;
            }
            return 0;
        }

        if (opts.set_config) {
            err = config_set(path, opts.set_key, opts.set_value);
            if (err == GS_OK) {
                fprintf(stderr, "Set %s = %s in %s\n", opts.set_key, opts.set_value, path);
            } else {
                fprintf(stderr, "Failed to set %s: %s\n", opts.set_key, gs_error_string(err));
                return 1;
            }
            return 0;
        }

        if (opts.show_config) {
            FILE *fp = fopen(path, "r");
            if (!fp) {
                fprintf(stderr, "Config file does not exist: %s\n", path);
                return 1;
            }
            char line[1024];
            while (fgets(line, sizeof(line), fp))
                printf("%s", line);
            fclose(fp);
            return 0;
        }

        if (opts.edit_config) {
            FILE *fp = fopen(path, "r");
            if (fp) {
                fclose(fp);
            } else {
                err = config_save_default(path);
                if (err != GS_OK) {
                    fprintf(stderr, "Failed to create default config: %s\n", gs_error_string(err));
                    return 1;
                }
                fprintf(stderr, "Created default config at %s\n", path);
            }
            const char *editor = getenv("VISUAL");
            if (!editor || !editor[0]) editor = getenv("EDITOR");
            if (!editor || !editor[0]) editor = "vi";
            char cmd[1400];
            snprintf(cmd, sizeof(cmd), "%s \"%s\"", editor, path);
            int rc = system(cmd);
            if (rc != 0) {
                fprintf(stderr, "Editor exited with status %d\n", rc);
                return 1;
            }
            return 0;
        }

        fprintf(stderr, "Config path: %s\n", path);

        FILE *fp = fopen(path, "r");
        if (fp) {
            fprintf(stderr, "Config file exists.\n");
            fclose(fp);
        } else {
            fprintf(stderr, "Config file does not exist. Creating default...\n");
            err = config_save_default(path);
            if (err == GS_OK) {
                fprintf(stderr, "Default config created at %s\n", path);
            } else {
                fprintf(stderr, "Failed to create config: %s\n", gs_error_string(err));
                return 1;
            }
        }
        return 0;
    }

    if (opts.command == 3) {
        char path[1024];
        get_config_path(path, sizeof(path), &opts);

        char usage_path[1024];
        usage_path_from_config(path, usage_path, sizeof(usage_path));
        usage_set_file(usage_path);

        if (opts.watch_cmd == 3) { /* list */
            Config cfg;
            config_init(&cfg);
            config_load(&cfg, path);
            if (opts.watch_json) {
                printf("{\n  \"subscriptions\": [\n");
                for (int i = 0; i < cfg.subscription_count; i++) {
                    const Subscription *s = &cfg.subscriptions[i];
                    printf("    {\"index\": %d, \"repo\": \"%s\", \"alias\": \"%s\", "
                           "\"platform\": \"%s\", \"days\": %d, \"format\": \"%s\"}%s\n",
                           i, s->repo, s->alias, s->platform, s->days, s->format,
                           i + 1 < cfg.subscription_count ? "," : "");
                }
                printf("  ]\n}\n");
            } else {
                color_init(opts.color_mode >= 0 ? opts.color_mode : cfg.color_mode);
                TuiTable t;
                char idx[MAX_SUBSCRIPTIONS][8];
                char days[MAX_SUBSCRIPTIONS][12];
                const char *cells[6];
                tui_table_init(&t, 6);
                tui_table_header(&t, 0, "Idx");
                tui_table_header(&t, 1, "Repository");
                tui_table_header(&t, 2, "Alias");
                tui_table_header(&t, 3, "Platform");
                tui_table_header(&t, 4, "Days");
                tui_table_header(&t, 5, "Format");
                tui_table_align(&t, 0, TUI_ALIGN_RIGHT);
                tui_table_align(&t, 4, TUI_ALIGN_RIGHT);
                tui_table_cell_color(&t, 1, CLR_BLUE);
                tui_table_cell_color(&t, 2, CLR_CYAN);
                tui_table_cell_color(&t, 3, CLR_GRAY);
                tui_table_cell_color(&t, 4, CLR_YELLOW);
                tui_table_cell_color(&t, 5, CLR_GREEN);
                for (int i = 0; i < cfg.subscription_count; i++) {
                    const Subscription *s = &cfg.subscriptions[i];
                    snprintf(idx[i], sizeof(idx[i]), "%d", i);
                    snprintf(days[i], sizeof(days[i]), "%d", s->days);
                    cells[0] = idx[i];
                    cells[1] = s->repo;
                    cells[2] = s->alias[0] ? s->alias : "-";
                    cells[3] = s->platform[0] ? s->platform : "-";
                    cells[4] = s->days ? days[i] : "-";
                    cells[5] = s->format[0] ? s->format : "-";
                    tui_table_row(&t, cells);
                }
                t.use_color = color_active();
                t.border = TUI_BORDER_ROUNDED;
                t.zebra = 1;
                tui_table_render(&t, stdout);
            }
            if (cfg.subscription_count == 0)
                fprintf(stderr, "No subscriptions. Use: gs watch add owner/repo\n");
            return 0;
        }

        if (opts.watch_cmd == 0) { /* add */
            Subscription sub;
            memset(&sub, 0, sizeof(sub));
            snprintf(sub.repo, sizeof(sub.repo), "%s", opts.watch_ref);
            snprintf(sub.alias, sizeof(sub.alias), "%s", opts.watch_alias);
            snprintf(sub.platform, sizeof(sub.platform), "%s", opts.watch_platform);
            sub.days = opts.watch_days;
            snprintf(sub.format, sizeof(sub.format), "%s", opts.watch_format);

            err = config_add_subscription(path, &sub);
            if (err != GS_OK) {
                fprintf(stderr, "Error: cannot subscribe '%s' (%s). It may already exist.\n",
                        sub.repo, gs_error_string(err));
                return 1;
            }
            fprintf(stderr, "Subscribed to %s\n", sub.repo);
            return 0;
        }

        if (opts.watch_cmd == 1) { /* remove */
            err = config_remove_subscription(path, opts.watch_ref);
            if (err != GS_OK) {
                fprintf(stderr, "Error: subscription '%s' not found.\n", opts.watch_ref);
                return 1;
            }
            fprintf(stderr, "Removed subscription '%s'\n", opts.watch_ref);
            return 0;
        }

        if (opts.watch_cmd == 2) { /* update */
            Subscription changes;
            memset(&changes, 0, sizeof(changes));
            snprintf(changes.repo, sizeof(changes.repo), "%s", opts.watch_new_repo);
            snprintf(changes.alias, sizeof(changes.alias), "%s", opts.watch_alias);
            snprintf(changes.platform, sizeof(changes.platform), "%s", opts.watch_platform);
            if (opts.watch_days_set) changes.days = opts.watch_days;
            snprintf(changes.format, sizeof(changes.format), "%s", opts.watch_format);

            err = config_update_subscription(path, opts.watch_ref, &changes);
            if (err != GS_OK) {
                fprintf(stderr, "Error: cannot update subscription '%s' (%s).\n",
                        opts.watch_ref, gs_error_string(err));
                return 1;
            }
            fprintf(stderr, "Updated subscription '%s'\n", opts.watch_ref);
            return 0;
        }

        /* watch_cmd == 4: analyze all subscriptions */
        Config cfg;
        config_init(&cfg);
        config_load(&cfg, path);
        config_resolve_prompts(&cfg, path);

        if (cfg.subscription_count == 0) {
            fprintf(stderr, "No subscriptions. Use: gs watch add owner/repo\n");
            return 1;
        }

        color_init(opts.color_mode >= 0 ? opts.color_mode : cfg.color_mode);
        anim_set_quiet(opts.quiet);
        http_set_cache_enabled(!opts.no_cache);
        curl_global_init(CURL_GLOBAL_DEFAULT);

        int batch = cfg.subscription_count > 1;
        if (batch && opts.output[0]) {
            fprintf(stderr, "Error: -o/--output cannot be used with multiple subscriptions.\n");
            curl_global_cleanup();
            return 1;
        }

        int rc = 0;
        int statuses[TUI_MAX_ROWS];
        const char *repos[TUI_MAX_ROWS];
        for (int i = 0; i < cfg.subscription_count; i++) {
            const Subscription *s = &cfg.subscriptions[i];
            Options o2 = opts;
            if (s->days > 0) o2.since_days = s->days;
            if (s->format[0]) snprintf(o2.format, sizeof(o2.format), "%s", s->format);
            if (s->platform[0]) snprintf(o2.platform, sizeof(o2.platform), "%s", s->platform);

            if (batch && !opts.quiet)
                fprintf(stderr, "\n== Analyzing subscription %s ==\n", s->repo);
            int r = analyze_repo(&o2, cfg, s->repo, batch, NULL);
            statuses[i] = r;
            repos[i] = s->repo;
            rc |= r;
        }

        if (batch && !opts.quiet)
            print_batch_summary(repos, statuses, cfg.subscription_count);

        curl_global_cleanup();
        return rc;
    }

    char config_path[1024];
    get_config_path(config_path, sizeof(config_path), &opts);

    char usage_path[1024];
    usage_path_from_config(config_path, usage_path, sizeof(usage_path));
    usage_set_file(usage_path);

    Config cfg;
    config_init(&cfg);
    config_load(&cfg, config_path);

    config_resolve_prompts(&cfg, config_path);

    color_init(opts.color_mode >= 0 ? opts.color_mode : cfg.color_mode);
    anim_set_quiet(opts.quiet);
    http_set_cache_enabled(!opts.no_cache);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int batch = opts.repo_count > 1;
    if (batch && opts.output[0]) {
        fprintf(stderr, "Error: -o/--output cannot be used with multiple repositories.\n");
        curl_global_cleanup();
        return 1;
    }

    if (opts.compare[0]) {
        if (opts.repo_count != 1) {
            fprintf(stderr, "Error: --compare requires exactly one repository.\n");
            curl_global_cleanup();
            return 1;
        }
        int d1 = 0, d2 = 0;
        if (sscanf(opts.compare, "%d:%d", &d1, &d2) != 2 || d1 <= 0 || d2 <= 0) {
            fprintf(stderr, "Error: --compare expects two periods like 7:30.\n");
            curl_global_cleanup();
            return 1;
        }
        int rc = run_compare(&opts, cfg, opts.repos[0], d1, d2);
        curl_global_cleanup();
        return rc;
    }

    int rc = 0;
    int statuses[16];
    const char *repo_ptrs[16];
    for (int i = 0; i < opts.repo_count; i++) {
        if (batch && !opts.quiet)
            fprintf(stderr, "\n== Analyzing %s ==\n", opts.repos[i]);
        int r = analyze_repo(&opts, cfg, opts.repos[i], batch, NULL);
        statuses[i] = r;
        repo_ptrs[i] = opts.repos[i];
        rc |= r;
    }

    if (batch && !opts.quiet)
        print_batch_summary(repo_ptrs, statuses, opts.repo_count);

    curl_global_cleanup();
    return rc;
}

static int analyze_repo(const Options *opts, Config cfg, const char *repo_arg, int batch, Report *report_out) {
    ErrorCode err;
    char owner[128] = {0}, repo[128] = {0};
    char detected_platform[16] = {0};
    {
        char input[512];
        snprintf(input, sizeof(input), "%s", repo_arg);

        char *path = input;
        char *proto = strstr(path, "://");
        if (proto) {
            path = proto + 3;
            char *host_slash = strchr(path, '/');
            if (host_slash) {
                size_t host_len = (size_t)(host_slash - path);
                if (host_len > 0) {
                    char host[256];
                    memcpy(host, path, host_len);
                    host[host_len] = '\0';
                    if (strstr(host, "gitlab"))
                        snprintf(detected_platform, sizeof(detected_platform), "gitlab");
                }
                path = host_slash + 1;
            } else {
                path = "";
            }
        }

        char *at = strchr(path, '@');
        if (at) {
            char *colon = strchr(at, ':');
            if (colon) path = colon + 1;
        }

        char *segments[16];
        int nseg = 0;
        char *p = path;
        while (*p && nseg < 16) {
            while (*p == '/') p++;
            if (!*p) break;
            segments[nseg++] = p;
            while (*p && *p != '/') p++;
            if (*p) *p++ = '\0';
        }

        if (nseg >= 2) {
            char *r = segments[nseg - 1];
            size_t rlen = strlen(r);
            if (rlen > 4 && strcmp(r + rlen - 4, ".git") == 0) r[rlen - 4] = '\0';
            snprintf(repo, sizeof(repo), "%s", r);
            snprintf(owner, sizeof(owner), "%s", segments[nseg - 2]);
        } else {
            fprintf(stderr, "Error: Invalid format '%s'. Use owner/repo or a URL.\n", repo_arg);
            return 1;
        }
    }

    if (detected_platform[0] && !opts->platform[0]) {
        snprintf(cfg.platform, sizeof(cfg.platform), "%s", detected_platform);
        if (strcmp(detected_platform, "gitlab") == 0 && !opts->api_base[0])
            snprintf(cfg.api_base, sizeof(cfg.api_base), "https://gitlab.com/api/v4");
    }

    if (opts->export_extra[0])
        snprintf(cfg.export_extra, sizeof(cfg.export_extra), "%s", opts->export_extra);

    config_merge(&cfg, opts->since_days, opts->max_pages,
                 opts->format, opts->output, opts->proxy,
                 opts->api_base, opts->color_mode, opts->token,
                 opts->llm_name, opts->platform);

    Vector prs, commits, issues, releases, contributors;
    vector_init(&prs);
    vector_init(&commits);
    vector_init(&issues);
    vector_init(&releases);
    vector_init(&contributors);

    const LLMConfig *active_llm = config_get_active_llm(&cfg);
    int use_llm = active_llm && active_llm->api_key[0] && active_llm->api_base[0];

    RepoMeta meta;
    memset(&meta, 0, sizeof(meta));

    const char *fetch_labels[6] = {
        "pull requests", "commits", "issues", "releases",
        "repository metadata", "contributors"
    };

    FetchJob jobs[6];
    pthread_t threads[6];
    memset(jobs, 0, sizeof(jobs));
    int jidx = 0;
    jobs[jidx++].kind = 0;
    jobs[jidx++].kind = 1;
    jobs[jidx++].kind = 2;
    jobs[jidx++].kind = 3;
    jobs[jidx].kind = 4; jobs[jidx].meta = &meta; jidx++;
    jobs[jidx].kind = 5; jidx++;
    jobs[0].vec = &prs;
    jobs[1].vec = &commits;
    jobs[2].vec = &issues;
    jobs[3].vec = &releases;
    jobs[5].vec = &contributors;

    for (int i = 0; i < 6; i++) {
        jobs[i].cfg = &cfg;
        snprintf(jobs[i].owner, sizeof(jobs[i].owner), "%s", owner);
        snprintf(jobs[i].repo, sizeof(jobs[i].repo), "%s", repo);
        jobs[i].since_days = cfg.default_days;
    }

    Anim sp;
    anim_start(&sp, ANIM_BAR, 0, 6, "Fetching repository data");
    {
        int created[6];
        int ncreated = 0;
        for (int i = 0; i < 6; i++) {
            if (pthread_create(&threads[i], NULL, fetch_worker, &jobs[i]) == 0)
                created[ncreated++] = i;
            else
                jobs[i].err = ERR_THREAD;
        }
        int done = 0;
        for (int t = 0; t < ncreated; t++) {
            pthread_join(threads[created[t]], NULL);
            anim_update(&sp, ++done, 6);
        }
    }
    anim_stop(&sp, 1);

    for (int i = 0; i < 6; i++) {
        if (jobs[i].err != GS_OK) {
            const char *detail = (jobs[i].client_ready && jobs[i].pclient.last_error[0])
                                 ? jobs[i].pclient.last_error : gs_error_string(jobs[i].err);
            color_printf_color(CLR_RED, "  -> Fetching %s failed: %s\n", fetch_labels[i], detail);
        }
        if (jobs[i].client_ready)
            jobs[i].pclient.destroy(jobs[i].pclient.ctx);
    }

    Report report;

    if (use_llm) {
        anim_start(&sp, ANIM_DOTS, 0, 0, "Analyzing with LLM");
        analyzer_run(&report, owner, repo, cfg.default_days, &prs, &commits, &issues, &releases, &contributors);
        report.meta = meta;
        err = analyzer_llm_summary(report.summary, sizeof(report.summary),
                                   active_llm, owner, repo, cfg.default_days,
                                   &prs, &commits, &issues, &releases,
                                   opts->llm_mode[0] ? opts->llm_mode : "health");
        if (err != GS_OK) {
            color_printf_color(CLR_YELLOW, "\nLLM analysis failed (%s), using local summary.\n",
                               gs_error_string(err));
        }
        anim_stop(&sp, 1);
    } else {
        anim_start(&sp, ANIM_BRAILLE, 0, 0, "Analyzing repository");
        analyzer_run(&report, owner, repo, cfg.default_days, &prs, &commits, &issues, &releases, &contributors);
        report.meta = meta;
        anim_stop(&sp, 1);
    }

    if (report_out) {
        *report_out = report;
        vector_free(&prs, pr_free);
        vector_free(&commits, commit_free);
        vector_free(&issues, issue_free);
        vector_free(&releases, release_free);
        vector_free(&contributors, contributor_free);
        return 0;
    }

    anim_start(&sp, ANIM_UNICODE, 0, 0, "Writing report");
    {
        if (strchr(cfg.format, ',')) {
            char fmt_copy[256];
            snprintf(fmt_copy, sizeof(fmt_copy), "%s", cfg.format);
            char *token = strtok(fmt_copy, ",");
            while (token) {
                while (*token == ' ') token++;
                if (*token == '\0') { token = strtok(NULL, ","); continue; }

                Exporter e;
                if (exporter_init(&e, token) != GS_OK) {
                    color_printf_color(CLR_RED, "  -> Unsupported format: %s\n", token);
                    token = strtok(NULL, ",");
                    continue;
                }

                char filename[1024];
                if (cfg.output[0]) {
                    char base[512];
                    snprintf(base, sizeof(base), "%s", cfg.output);
                    char *slash = strrchr(base, '/');
                    char *dot = strrchr(slash ? slash + 1 : base, '.');
                    if (dot) *dot = '\0';
                    snprintf(filename, sizeof(filename), "%s.%s", base, token);
                } else {
                    snprintf(filename, sizeof(filename), "%s_%s_report.%s", owner, repo, token);
                }

                FILE *fp = fopen(filename, "w");
                if (!fp) {
                    color_printf_color(CLR_RED, "  -> Cannot open %s\n", filename);
                } else {
                    FormatOptions fopts;
                    build_fopts(&fopts, &cfg, 1, token);
                    e.write_report(fp, &report, &prs, &commits, &issues, &releases, &fopts);
                    free_fopts(&fopts);
                    fclose(fp);
                    if (!opts->quiet)
                        fprintf(stderr, "  -> Written to %s\n", filename);
                }
                token = strtok(NULL, ",");
            }
        } else {
            Exporter exporter;
            err = exporter_init(&exporter, cfg.format);
            if (err != GS_OK) {
                anim_stop(&sp, 0);
                color_printf_color(CLR_RED, "  -> %s\n", gs_error_string(err));
                vector_free(&prs, pr_free);
                vector_free(&commits, commit_free);
                vector_free(&issues, issue_free);
                vector_free(&releases, release_free);
                vector_free(&contributors, contributor_free);
                return 1;
            }

            FILE *fp;
            int close_fp = 0;
            char fname[1024];
            if (cfg.output[0] || batch) {
                if (cfg.output[0]) {
                    snprintf(fname, sizeof(fname), "%s", cfg.output);
                } else {
                    snprintf(fname, sizeof(fname), "%s_%s_report.%s", owner, repo, cfg.format);
                }
                fp = fopen(fname, "w");
                if (!fp) {
                    anim_stop(&sp, 0);
                    color_printf_color(CLR_RED, "  -> Cannot open %s\n", fname);
                    vector_free(&prs, pr_free);
                    vector_free(&commits, commit_free);
                    vector_free(&issues, issue_free);
                    vector_free(&releases, release_free);
                    vector_free(&contributors, contributor_free);
                    return 1;
                }
                close_fp = 1;
            } else {
                if (strcmp(cfg.format, "xlsx") == 0 || strcmp(cfg.format, "pdf") == 0) {
                    anim_stop(&sp, 0);
                    color_printf_color(CLR_RED, "  -> %s format requires -o output file (cannot write to stdout)\n", cfg.format);
                    vector_free(&prs, pr_free);
                    vector_free(&commits, commit_free);
                    vector_free(&issues, issue_free);
                    vector_free(&releases, release_free);
                    vector_free(&contributors, contributor_free);
                    return 1;
                }
                fp = stdout;
            }

            FormatOptions fopts;
            build_fopts(&fopts, &cfg, close_fp, cfg.format);
            exporter.write_report(fp, &report, &prs, &commits, &issues, &releases, &fopts);
            free_fopts(&fopts);

            if (close_fp) {
                fclose(fp);
                if (!opts->quiet)
                    fprintf(stderr, "  -> Written to %s\n", cfg.output[0] ? cfg.output : fname);
            }
        }
    }
    anim_stop(&sp, 1);

    vector_free(&prs, pr_free);
    vector_free(&commits, commit_free);
    vector_free(&issues, issue_free);
    vector_free(&releases, release_free);
    vector_free(&contributors, contributor_free);

    return 0;
}

static void print_compare_delta(FILE *fp, const char *label, long a, long b) {
    if (a == 0 && b == 0) {
        fprintf(fp, "| %s | 0 | 0 | - |\n", label);
    } else if (a == 0) {
        fprintf(fp, "| %s | 0 | %ld | +%ld |\n", label, b, b);
    } else {
        fprintf(fp, "| %s | %ld | %ld | %+.0f%% |\n", label, a, b,
                ((double)(b - a) / (double)a) * 100.0);
    }
}

static int run_compare(const Options *opts, Config cfg, const char *repo_arg, int d1, int d2) {
    Options o = *opts;
    o.since_days = -1;
    if (opts->output[0])
        snprintf(cfg.output, sizeof(cfg.output), "%s", opts->output);
    if (opts->format[0])
        snprintf(cfg.format, sizeof(cfg.format), "%s", opts->format);

    Config c1 = cfg;
    c1.default_days = d1;
    Config c2 = cfg;
    c2.default_days = d2;

    Report r1, r2;
    memset(&r1, 0, sizeof(r1));
    memset(&r2, 0, sizeof(r2));

    if (analyze_repo(&o, c1, repo_arg, 0, &r1) != 0) return 1;
    if (analyze_repo(&o, c2, repo_arg, 0, &r2) != 0) return 1;

    FILE *fp = stdout;
    int close_fp = 0;
    if (cfg.output[0]) {
        fp = fopen(cfg.output, "w");
        if (!fp) {
            fprintf(stderr, "Error: Cannot open %s\n", cfg.output);
            return 1;
        }
        close_fp = 1;
    }

    int use_json = strcmp(cfg.format, "json") == 0;

    if (use_json) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "owner", r1.owner);
        cJSON_AddStringToObject(root, "repo", r1.repo);
        cJSON_AddNumberToObject(root, "period_days_a", d1);
        cJSON_AddNumberToObject(root, "period_days_b", d2);

        cJSON *ma = cJSON_AddObjectToObject(root, "period_a");
        cJSON_AddNumberToObject(ma, "pr_total", r1.pr_total);
        cJSON_AddNumberToObject(ma, "pr_merged", r1.pr_merged);
        cJSON_AddNumberToObject(ma, "commit_total", r1.commit_total);
        cJSON_AddNumberToObject(ma, "author_total", r1.author_total);
        cJSON_AddNumberToObject(ma, "issue_total", r1.issue_total);
        cJSON_AddNumberToObject(ma, "release_total", r1.release_total);
        cJSON_AddNumberToObject(ma, "contributor_total", r1.contributor_total);
        cJSON_AddNumberToObject(ma, "pr_avg_merge_hours", r1.pr_avg_merge_hours);
        cJSON_AddNumberToObject(ma, "issue_avg_close_hours", r1.issue_avg_close_hours);
        cJSON_AddNumberToObject(ma, "activity_count", r1.activity_count);

        cJSON *mb = cJSON_AddObjectToObject(root, "period_b");
        cJSON_AddNumberToObject(mb, "pr_total", r2.pr_total);
        cJSON_AddNumberToObject(mb, "pr_merged", r2.pr_merged);
        cJSON_AddNumberToObject(mb, "commit_total", r2.commit_total);
        cJSON_AddNumberToObject(mb, "author_total", r2.author_total);
        cJSON_AddNumberToObject(mb, "issue_total", r2.issue_total);
        cJSON_AddNumberToObject(mb, "release_total", r2.release_total);
        cJSON_AddNumberToObject(mb, "contributor_total", r2.contributor_total);
        cJSON_AddNumberToObject(mb, "pr_avg_merge_hours", r2.pr_avg_merge_hours);
        cJSON_AddNumberToObject(mb, "issue_avg_close_hours", r2.issue_avg_close_hours);
        cJSON_AddNumberToObject(mb, "activity_count", r2.activity_count);

        char *str = cJSON_Print(root);
        fprintf(fp, "%s\n", str);
        free(str);
        cJSON_Delete(root);
    } else {
        fprintf(fp, "# Activity comparison: %s/%s\n\n", r1.owner, r1.repo);
        fprintf(fp, "Period A: last %d days | Period B: last %d days\n\n", d1, d2);
        fprintf(fp, "| Metric | A (%dd) | B (%dd) | Delta |\n", d1, d2);
        fprintf(fp, "|---|---|---|---|\n");
        print_compare_delta(fp, "Pull requests", r1.pr_total, r2.pr_total);
        print_compare_delta(fp, "PRs merged", r1.pr_merged, r2.pr_merged);
        print_compare_delta(fp, "Commits", r1.commit_total, r2.commit_total);
        print_compare_delta(fp, "Authors", r1.author_total, r2.author_total);
        print_compare_delta(fp, "Issues", r1.issue_total, r2.issue_total);
        print_compare_delta(fp, "Releases", r1.release_total, r2.release_total);
        print_compare_delta(fp, "Contributors", r1.contributor_total, r2.contributor_total);
        fprintf(fp, "\nTop author: %s  ->  %s\n", r1.top_author, r2.top_author);
        fprintf(fp, "Top contributor: %s  ->  %s\n", r1.top_contributor, r2.top_contributor);
        fprintf(fp, "Busiest weekday: %s  ->  %s\n", r1.activity_labels[r1.busiest_weekday],
                r2.activity_labels[r2.busiest_weekday]);
        if (r1.pr_merged_count > 0 || r2.pr_merged_count > 0)
            fprintf(fp, "Avg merge time: %.1fh  ->  %.1fh\n", r1.pr_avg_merge_hours, r2.pr_avg_merge_hours);
        if (r1.issue_closed_count > 0 || r2.issue_closed_count > 0)
            fprintf(fp, "Avg issue close time: %.1fh  ->  %.1fh\n", r1.issue_avg_close_hours, r2.issue_avg_close_hours);
        fprintf(fp, "\n");
    }

    if (close_fp) {
        fclose(fp);
        if (!opts->quiet)
            fprintf(stderr, "  -> Written to %s\n", cfg.output);
    }
    return 0;
}
