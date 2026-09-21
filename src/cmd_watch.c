#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/color.h"
#include "gitsight/anim.h"
#include "gitsight/error.h"
#include "gitsight/tui.h"
#include "gitsight/usage.h"
#include "gitsight/platform/http.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_watch(Options *opts) {
    ErrorCode err;
    char path[1024];
    get_config_path(path, sizeof(path), opts);

    char usage_path[1024];
    usage_path_from_config(path, usage_path, sizeof(usage_path));
    usage_set_file(usage_path);

    if (opts->watch_cmd == 3) { /* list */
        Config cfg;
        config_init(&cfg);
        config_load(&cfg, path);
        if (opts->watch_json) {
            printf("{\n  \"subscriptions\": [\n");
            for (int i = 0; i < cfg.subscription_count; i++) {
                const Subscription *s = &cfg.subscriptions[i];
                const char *plat = s->platform[0] ? s->platform : cfg.platform;
                const char *fmt = s->format[0] ? s->format : cfg.format;
                int days = s->days > 0 ? s->days : cfg.default_days;
                const char *br = s->branch[0] ? s->branch : "-";
                printf("    {\"index\": %d, \"repo\": \"%s\", \"alias\": \"%s\", "
                       "\"platform\": \"%s\", \"days\": %d, \"format\": \"%s\", "
                       "\"branch\": \"%s\"}%s\n",
                       i, s->repo, s->alias, plat, days, fmt, br,
                       i + 1 < cfg.subscription_count ? "," : "");
            }
            printf("  ]\n}\n");
        } else {
            color_init(opts->color_mode >= 0 ? opts->color_mode : cfg.color_mode);
            TuiTable t;
            char idx[MAX_SUBSCRIPTIONS][8];
            char days_str[MAX_SUBSCRIPTIONS][12];
            const char *cells[7];
            tui_table_init(&t, 7);
            tui_table_header(&t, 0, "Idx");
            tui_table_header(&t, 1, "Repository");
            tui_table_header(&t, 2, "Alias");
            tui_table_header(&t, 3, "Platform");
            tui_table_header(&t, 4, "Days");
            tui_table_header(&t, 5, "Format");
            tui_table_header(&t, 6, "Branch");
            tui_table_align(&t, 0, TUI_ALIGN_RIGHT);
            tui_table_align(&t, 4, TUI_ALIGN_RIGHT);
            tui_table_cell_color(&t, 1, CLR_BLUE);
            tui_table_cell_color(&t, 2, CLR_CYAN);
            tui_table_cell_color(&t, 3, CLR_GRAY);
            tui_table_cell_color(&t, 4, CLR_YELLOW);
            tui_table_cell_color(&t, 5, CLR_GREEN);
            tui_table_cell_color(&t, 6, CLR_CYAN);
            for (int i = 0; i < cfg.subscription_count; i++) {
                const Subscription *s = &cfg.subscriptions[i];
                const char *plat = s->platform[0] ? s->platform : cfg.platform;
                const char *fmt = s->format[0] ? s->format : cfg.format;
                int days = s->days > 0 ? s->days : cfg.default_days;
                const char *br = s->branch[0] ? s->branch : "-";
                snprintf(idx[i], sizeof(idx[i]), "%d", i);
                snprintf(days_str[i], sizeof(days_str[i]), "%d", days);
                cells[0] = idx[i];
                cells[1] = s->repo;
                cells[2] = s->alias[0] ? s->alias : "-";
                cells[3] = plat;
                cells[4] = days_str[i];
                cells[5] = fmt;
                cells[6] = br;
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

    if (opts->watch_cmd == 0) { /* add */
        Subscription sub;
        memset(&sub, 0, sizeof(sub));
        snprintf(sub.repo, sizeof(sub.repo), "%s", opts->watch_ref);
        snprintf(sub.alias, sizeof(sub.alias), "%s", opts->watch_alias);
        snprintf(sub.platform, sizeof(sub.platform), "%s", opts->watch_platform);
        snprintf(sub.branch, sizeof(sub.branch), "%s", opts->branch);
        sub.days = opts->watch_days;
        snprintf(sub.format, sizeof(sub.format), "%s", opts->watch_format);

        err = config_add_subscription(path, &sub);
        if (err != GS_OK) {
            fprintf(stderr, "Error: cannot subscribe '%s' (%s). It may already exist.\n",
                    sub.repo, gs_error_string(err));
            return 1;
        }
        fprintf(stderr, "Subscribed to %s\n", sub.repo);
        return 0;
    }

    if (opts->watch_cmd == 1) { /* remove */
        err = config_remove_subscription(path, opts->watch_ref);
        if (err != GS_OK) {
            fprintf(stderr, "Error: subscription '%s' not found.\n", opts->watch_ref);
            return 1;
        }
        fprintf(stderr, "Removed subscription '%s'\n", opts->watch_ref);
        return 0;
    }

    if (opts->watch_cmd == 2) { /* update */
        Subscription changes;
        memset(&changes, 0, sizeof(changes));
        snprintf(changes.repo, sizeof(changes.repo), "%s", opts->watch_new_repo);
        snprintf(changes.alias, sizeof(changes.alias), "%s", opts->watch_alias);
        snprintf(changes.platform, sizeof(changes.platform), "%s", opts->watch_platform);
        if (opts->branch[0])
            snprintf(changes.branch, sizeof(changes.branch), "%s", opts->branch);
        if (opts->watch_days_set) changes.days = opts->watch_days;
        snprintf(changes.format, sizeof(changes.format), "%s", opts->watch_format);

        err = config_update_subscription(path, opts->watch_ref, &changes);
        if (err != GS_OK) {
            fprintf(stderr, "Error: cannot update subscription '%s' (%s).\n",
                    opts->watch_ref, gs_error_string(err));
            return 1;
        }
        fprintf(stderr, "Updated subscription '%s'\n", opts->watch_ref);
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

    color_init(opts->color_mode >= 0 ? opts->color_mode : cfg.color_mode);
    anim_set_quiet(opts->quiet);
    http_set_cache_enabled(!opts->no_cache);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int batch = cfg.subscription_count > 1;
    if (batch && opts->output[0]) {
        fprintf(stderr, "Error: -o/--output cannot be used with multiple subscriptions.\n");
        curl_global_cleanup();
        return 1;
    }

    int rc = 0;
    int statuses[TUI_MAX_ROWS];
    const char *repos[TUI_MAX_ROWS];
    int n = cfg.subscription_count;
    if (n > TUI_MAX_ROWS) n = TUI_MAX_ROWS;

    BatchTask tasks[MAX_SUBSCRIPTIONS];
    char labels[MAX_SUBSCRIPTIONS][340];
    for (int i = 0; i < n; i++) {
        const Subscription *s = &cfg.subscriptions[i];
        Options o2 = *opts;
        if (s->days > 0) o2.since_days = s->days;
        if (s->format[0]) snprintf(o2.format, sizeof(o2.format), "%s", s->format);
        if (s->platform[0]) snprintf(o2.platform, sizeof(o2.platform), "%s", s->platform);
        if (s->branch[0]) snprintf(o2.branch, sizeof(o2.branch), "%s", s->branch);

        tasks[i].opts = o2;
        tasks[i].cfg = &cfg;
        tasks[i].repo = s->repo;
        snprintf(labels[i], sizeof(labels[i]), "Analyzing subscription %s", s->repo);
        tasks[i].label = labels[i];
        tasks[i].batch = batch;
        tasks[i].result = 0;
    }

    int par = batch ? batch_parallelism(opts, &cfg) : 1;
    rc = run_batch_tasks(tasks, n, par, opts->quiet);

    for (int i = 0; i < n; i++) {
        statuses[i] = tasks[i].result;
        repos[i] = cfg.subscriptions[i].repo;
    }

    if (batch && !opts->quiet)
        print_batch_summary(repos, statuses, n);

    curl_global_cleanup();
    return rc;
}
