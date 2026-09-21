#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/error.h"
#include "gitsight/i18n.h"
#include "gitsight/color.h"
#include "gitsight/anim.h"
#include "gitsight/usage.h"
#include "gitsight/models.h"
#include "gitsight/container.h"
#include "gitsight/platform/client.h"
#include "gitsight/platform/http.h"
#include "gitsight/platform/profile.h"
#include "gitsight/analyzer/engine.h"
#include "gitsight/export/exporter.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <curl/curl.h>

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

int cmd_analyze(Options *opts) {
    char config_path[1024];
    get_config_path(config_path, sizeof(config_path), opts);

    char usage_path[1024];
    usage_path_from_config(config_path, usage_path, sizeof(usage_path));
    usage_set_file(usage_path);

    Config cfg;
    config_init(&cfg);
    config_load(&cfg, config_path);

    config_resolve_prompts(&cfg, config_path);

    color_init(opts->color_mode >= 0 ? opts->color_mode : cfg.color_mode);
    anim_set_quiet(opts->quiet);
    http_set_cache_enabled(!opts->no_cache);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int batch = opts->repo_count > 1;
    if (batch && opts->output[0]) {
        fprintf(stderr, "Error: -o/--output cannot be used with multiple repositories.\n");
        curl_global_cleanup();
        return 1;
    }

    if (opts->compare[0]) {
        if (opts->repo_count != 1) {
            fprintf(stderr, "Error: --compare requires exactly one repository.\n");
            curl_global_cleanup();
            return 1;
        }
        int d1 = 0, d2 = 0;
        if (sscanf(opts->compare, "%d:%d", &d1, &d2) != 2 || d1 <= 0 || d2 <= 0) {
            fprintf(stderr, "Error: --compare expects two periods like 7:30.\n");
            curl_global_cleanup();
            return 1;
        }
        int rc = run_compare(opts, cfg, opts->repos[0], d1, d2);
        curl_global_cleanup();
        return rc;
    }

    int rc = 0;
    int statuses[16];
    const char *repo_ptrs[16];

    if (batch) {
        BatchTask tasks[16];
        char labels[16][300];
        int par = batch_parallelism(opts, &cfg);
        for (int i = 0; i < opts->repo_count; i++) {
            tasks[i].opts = *opts;
            tasks[i].cfg = &cfg;
            tasks[i].repo = opts->repos[i];
            snprintf(labels[i], sizeof(labels[i]), "Analyzing %s", opts->repos[i]);
            tasks[i].label = labels[i];
            tasks[i].batch = 1;
            tasks[i].result = 0;
        }
        rc = run_batch_tasks(tasks, opts->repo_count, par, opts->quiet);
        for (int i = 0; i < opts->repo_count; i++) {
            statuses[i] = tasks[i].result;
            repo_ptrs[i] = opts->repos[i];
        }
    } else {
        rc = analyze_repo(opts, cfg, opts->repos[0], 0, NULL);
        statuses[0] = rc;
        repo_ptrs[0] = opts->repos[0];
    }

    if (batch && !opts->quiet)
        print_batch_summary(repo_ptrs, statuses, opts->repo_count);

    curl_global_cleanup();
    return rc;
}

int analyze_repo(const Options *opts, Config cfg, const char *repo_arg, int batch, Report *report_out) {
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
                    else if (strstr(host, "codeberg"))
                        snprintf(detected_platform, sizeof(detected_platform), "codeberg");
                    else if (strstr(host, "gitea"))
                        snprintf(detected_platform, sizeof(detected_platform), "gitea");
                    else if (strstr(host, "forgejo"))
                        snprintf(detected_platform, sizeof(detected_platform), "forgejo");
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
        const PlatformProfile *pf = platform_profile(detected_platform);
        snprintf(cfg.platform, sizeof(cfg.platform), "%s", detected_platform);
        if (pf && !opts->api_base[0])
            snprintf(cfg.api_base, sizeof(cfg.api_base), "%s", pf->default_api_base);
    }

    if (opts->export_extra[0])
        snprintf(cfg.export_extra, sizeof(cfg.export_extra), "%s", opts->export_extra);

    config_merge(&cfg, opts->since_days, opts->max_pages,
                 opts->format, opts->output, opts->proxy,
                 opts->api_base, opts->color_mode, opts->token,
                 opts->llm_name, opts->platform, opts->branch);

    if (opts->lang[0])
        snprintf(cfg.lang, sizeof(cfg.lang), "%s", opts->lang);
    gs_set_lang_str(cfg.lang);

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
        if (cfg.branch[0])
            snprintf(report.branch, sizeof(report.branch), "%s", cfg.branch);
        else
            snprintf(report.branch, sizeof(report.branch), "%s", meta.default_branch);
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
        if (cfg.branch[0])
            snprintf(report.branch, sizeof(report.branch), "%s", cfg.branch);
        else
            snprintf(report.branch, sizeof(report.branch), "%s", meta.default_branch);
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

int run_compare(const Options *opts, Config cfg, const char *repo_arg, int d1, int d2) {
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
