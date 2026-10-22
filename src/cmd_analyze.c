/* open_memstream() backs the in-memory export path in analysis_render(). */
#define _GNU_SOURCE

#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/error.h"
#include "gitsight/i18n.h"
#include "gitsight/color.h"
#include "gitsight/anim.h"
#include "gitsight/cancel.h"
#include "gitsight/usage.h"
#include "gitsight/models.h"
#include "gitsight/perf.h"
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
#include <time.h>
#include <ctype.h>

/* ---- Date range (--from/--to) and item filters (--filter) ---- */

static int iso_valid(const char *s) {
    int y = 0, m = 0, d = 0;
    char extra = 0;
    if (sscanf(s, "%d-%d-%d%c", &y, &m, &d, &extra) != 3) return 0;
    return y >= 1970 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

static long long iso_to_epoch(const char *s) {
    int y = 0, m = 0, d = 0;
    sscanf(s, "%d-%d-%d", &y, &m, &d);
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
#ifdef _WIN32
    return (long long)_mkgmtime(&tm);
#else
    return (long long)timegm(&tm);
#endif
}

static void epoch_to_iso(long long e, char *out, size_t cap) {
    time_t t = (time_t)e;
    struct tm *g = gmtime(&t);
    if (g) strftime(out, cap, "%Y-%m-%d", g);
    else snprintf(out, cap, "?");
}

#define FILTER_MAX 8
#define FILTER_VAL_LEN 64

typedef struct {
    char authors[FILTER_MAX][FILTER_VAL_LEN];
    char labels[FILTER_MAX][FILTER_VAL_LEN];
    char words[FILTER_MAX][FILTER_VAL_LEN];
    int  n_author, n_label, n_word;
} FilterSet;

static void filter_add(char dst[FILTER_MAX][FILTER_VAL_LEN], int *n, const char *val) {
    if (!val[0] || *n >= FILTER_MAX) return;
    snprintf(dst[*n], FILTER_VAL_LEN, "%s", val);
    (*n)++;
}

static void parse_filters(const char *joined, FilterSet *fs) {
    memset(fs, 0, sizeof(*fs));
    const char *p = joined;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char tok[128];
        if (len >= sizeof(tok)) len = sizeof(tok) - 1;
        memcpy(tok, p, len);
        tok[len] = '\0';
        if (strncmp(tok, "author:", 7) == 0)
            filter_add(fs->authors, &fs->n_author, tok + 7);
        else if (strncmp(tok, "label:", 6) == 0)
            filter_add(fs->labels, &fs->n_label, tok + 6);
        else if (tok[0])
            filter_add(fs->words, &fs->n_word, tok);
        p = nl ? nl + 1 : NULL;
    }
}

static int ci_has(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (!nl) return 1;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

/* OR within a kind, AND across kinds. */
static int item_kept(const FilterSet *fs, const char *author,
                     const char *labels, const char *text) {
    if (fs->n_author) {
        int ok = 0;
        for (int i = 0; i < fs->n_author; i++)
            if (ci_has(author, fs->authors[i])) { ok = 1; break; }
        if (!ok) return 0;
    }
    if (fs->n_label) {
        int ok = 0;
        for (int i = 0; i < fs->n_label; i++)
            if (labels && ci_has(labels, fs->labels[i])) { ok = 1; break; }
        if (!ok) return 0;
    }
    if (fs->n_word) {
        int ok = 0;
        for (int i = 0; i < fs->n_word; i++)
            if (ci_has(text, fs->words[i]) || ci_has(author, fs->words[i])) { ok = 1; break; }
        if (!ok) return 0;
    }
    return 1;
}

/* ISO-8601 strings compare correctly as text on the leading YYYY-MM-DD. */
static int date_le(const char *iso, const char *to_iso) {
    if (!to_iso[0] || !iso[0]) return 1;
    return strncmp(iso, to_iso, 10) <= 0;
}

static int filters_active(const FilterSet *fs, const char *to_iso) {
    return to_iso[0] || fs->n_author || fs->n_label || fs->n_word;
}

/* The text an item is matched against, for one kind of item. */
typedef struct {
    const char *to_iso;
    const FilterSet *fs;
    char text[1024];
} FilterCtx;

static int keep_pr(const void *item, void *user) {
    FilterCtx *c = (FilterCtx *)user;
    const PR *it = (const PR *)item;
    snprintf(c->text, sizeof(c->text), "%s", it->title);
    return date_le(it->created_at, c->to_iso) &&
           item_kept(c->fs, it->author, it->labels, c->text);
}

static int keep_commit(const void *item, void *user) {
    FilterCtx *c = (FilterCtx *)user;
    const Commit *it = (const Commit *)item;
    snprintf(c->text, sizeof(c->text), "%s", it->message);
    return date_le(it->date, c->to_iso) &&
           item_kept(c->fs, it->author, NULL, c->text);
}

static int keep_issue(const void *item, void *user) {
    FilterCtx *c = (FilterCtx *)user;
    const Issue *it = (const Issue *)item;
    snprintf(c->text, sizeof(c->text), "%s", it->title);
    return date_le(it->created_at, c->to_iso) &&
           item_kept(c->fs, it->author, it->labels, c->text);
}

static int keep_release(const void *item, void *user) {
    FilterCtx *c = (FilterCtx *)user;
    const Release *it = (const Release *)item;
    snprintf(c->text, sizeof(c->text), "%s %s", it->tag_name, it->name);
    return date_le(it->published_at, c->to_iso) &&
           item_kept(c->fs, it->author, NULL, c->text);
}

/* Drops the items the filters exclude.
 *
 * The items are swept out in one pass: removing them one at a time moved the
 * whole tail of the list for every removal, which is quadratic in the number
 * of removals -- and a run bounded by --to can remove most of a repository. */
static void apply_filters(Vector *prs, Vector *commits, Vector *issues,
                          Vector *releases, const char *to_iso, const FilterSet *fs) {
    if (!filters_active(fs, to_iso)) return;

    FilterCtx ctx;
    ctx.to_iso = to_iso;
    ctx.fs = fs;
    ctx.text[0] = '\0';

    vector_retain(prs, keep_pr, &ctx, pr_free);
    vector_retain(commits, keep_commit, &ctx, commit_free);
    vector_retain(issues, keep_issue, &ctx, issue_free);
    vector_retain(releases, keep_release, &ctx, release_free);
}

typedef struct {
    const Config *cfg;
    char owner[128];
    char repo[128];
    int  since_days;
    int  use_cache;         /* whether this run may read the disk cache */
    int  kind;              /* 0=prs 1=commits 2=issues 3=releases 4=meta 5=contributors 6=languages */
    Vector *vec;
    RepoMeta *meta;
    PlatformClient pclient;
    int  client_ready;
    ErrorCode err;
} FetchJob;

const char *const analysis_fetch_labels[ANALYSIS_FETCH_KINDS] = {
    "pull requests", "commits", "issues", "releases",
    "repository metadata", "contributors", "languages"
};

void analysis_result_init(AnalysisResult *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    vector_init(&r->prs);
    vector_init(&r->commits);
    vector_init(&r->issues);
    vector_init(&r->releases);
    vector_init(&r->contributors);
    vector_init(&r->langs);
    for (int i = 0; i < ANALYSIS_FETCH_KINDS; i++)
        r->fetch_err[i] = GS_OK;
}

void analysis_result_free(AnalysisResult *r) {
    if (!r) return;
    vector_free(&r->prs, pr_free);
    vector_free(&r->commits, commit_free);
    vector_free(&r->issues, issue_free);
    vector_free(&r->releases, release_free);
    vector_free(&r->contributors, contributor_free);
    vector_free(&r->langs, lang_free);
    memset(r, 0, sizeof(*r));
}

static void *fetch_worker(void *arg) {
    FetchJob *j = (FetchJob *)arg;
    ErrorCode e = platform_client_init(&j->pclient, j->cfg, j->owner, j->repo,
                                       j->use_cache);
    if (e != GS_OK) { j->err = e; return NULL; }
    j->client_ready = 1;
    switch (j->kind) {
        case 0: j->err = j->pclient.fetch_prs(j->pclient.ctx, j->vec, j->since_days); break;
        case 1: j->err = j->pclient.fetch_commits(j->pclient.ctx, j->vec, j->since_days); break;
        case 2: j->err = j->pclient.fetch_issues(j->pclient.ctx, j->vec, j->since_days); break;
        case 3: j->err = j->pclient.fetch_releases(j->pclient.ctx, j->vec, j->since_days); break;
        case 4: j->err = j->pclient.fetch_meta(j->pclient.ctx, j->meta); break;
        case 5: j->err = j->pclient.fetch_contributors(j->pclient.ctx, j->vec); break;
        case 6: j->err = j->pclient.fetch_languages(j->pclient.ctx, j->vec); break;
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
        if (!opts->quiet) {
            long rem = -1, rst = 0;
            http_quota(&rem, &rst);
            if (rem >= 0) {
                long left = rst - (long)time(NULL);
                fprintf(stderr, "  -> API quota remaining: %ld (resets in %lds)\n",
                        rem, left > 0 ? left : 0L);
            }
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

int repo_spec_resolve(const char *arg,
                      char *owner, size_t owner_len,
                      char *repo, size_t repo_len,
                      char *platform, size_t platform_len) {
    if (!arg || !owner || !owner_len || !repo || !repo_len) return -1;
    owner[0] = '\0';
    repo[0] = '\0';
    if (platform && platform_len) platform[0] = '\0';

    char input[512];
    snprintf(input, sizeof(input), "%s", arg);

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
                const char *detected = NULL;
                if (strstr(host, "gitlab"))            detected = "gitlab";
                else if (strstr(host, "codeberg"))     detected = "codeberg";
                else if (strstr(host, "gitea"))        detected = "gitea";
                else if (strstr(host, "forgejo"))      detected = "forgejo";
                if (detected && platform && platform_len)
                    snprintf(platform, platform_len, "%s", detected);
            }
            path = host_slash + 1;
        } else {
            path = "";
        }
    }

    /* "user:token@host" is addressing the forge, not part of the repo path. */
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
    if (nseg < 2) return -1;

    char *r = segments[nseg - 1];
    size_t rlen = strlen(r);
    if (rlen > 4 && strcmp(r + rlen - 4, ".git") == 0) r[rlen - 4] = '\0';
    if (!r[0]) return -1;
    snprintf(repo, repo_len, "%s", r);
    snprintf(owner, owner_len, "%s", segments[nseg - 2]);
    return 0;
}

int analyze_repo(const Options *opts, Config cfg, const char *repo_arg, int batch, AnalysisResult *out) {
    ErrorCode err;
    char owner[128] = {0}, repo[128] = {0};
    char detected_platform[16] = {0};
    if (repo_spec_resolve(repo_arg, owner, sizeof(owner), repo, sizeof(repo),
                         detected_platform, sizeof(detected_platform)) != 0) {
        fprintf(stderr, "Error: Invalid format '%s'. Use owner/repo or a URL.\n", repo_arg);
        return 1;
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

    int has_from = opts->from[0] != 0;
    int has_to = opts->to[0] != 0;
    long long from_ts = 0, to_ts = 0;
    char to_iso[16] = "";
    if (has_from) {
        if (!iso_valid(opts->from)) {
            fprintf(stderr, "Error: --from expects YYYY-MM-DD.\n");
            return 1;
        }
        from_ts = iso_to_epoch(opts->from);
    }
    if (has_to) {
        if (!iso_valid(opts->to)) {
            fprintf(stderr, "Error: --to expects YYYY-MM-DD.\n");
            return 1;
        }
        to_ts = iso_to_epoch(opts->to) + 86399;
        epoch_to_iso(to_ts, to_iso, sizeof(to_iso));
    }
    long long now = (long long)time(NULL);
    if (has_from && from_ts > now) {
        fprintf(stderr, "Error: --from is in the future.\n");
        return 1;
    }
    if (has_from && has_to && from_ts > to_ts) {
        fprintf(stderr, "Error: --from must not be after --to.\n");
        return 1;
    }
    if (has_from) {
        long long span = (now - from_ts) / 86400 + 1;
        cfg.default_days = (int)(span > 3660 ? 3660 : span);
    } else if (has_to) {
        long long shift = (now - to_ts) / 86400 + 1;
        long long span = (long long)cfg.default_days + shift;
        cfg.default_days = (int)(span > 3660 ? 3660 : span);
    }

    FilterSet fs;
    parse_filters(opts->filters, &fs);

    Vector prs, commits, issues, releases, contributors, langs;
    vector_init(&prs);
    vector_init(&commits);
    vector_init(&issues);
    vector_init(&releases);
    vector_init(&contributors);
    vector_init(&langs);

    const LLMConfig *active_llm = config_get_active_llm(&cfg);
    int use_llm = active_llm && active_llm->api_key[0] && active_llm->api_base[0] &&
                  !opts->suppress_llm;

    RepoMeta meta;
    memset(&meta, 0, sizeof(meta));

    FetchJob jobs[7];
    pthread_t threads[7];
    memset(jobs, 0, sizeof(jobs));
    int jidx = 0;
    jobs[jidx++].kind = 0;
    jobs[jidx++].kind = 1;
    jobs[jidx++].kind = 2;
    jobs[jidx++].kind = 3;
    jobs[jidx].kind = 4; jobs[jidx].meta = &meta; jidx++;
    jobs[jidx++].kind = 5;
    jobs[jidx++].kind = 6;
    jobs[0].vec = &prs;
    jobs[1].vec = &commits;
    jobs[2].vec = &issues;
    jobs[3].vec = &releases;
    jobs[5].vec = &contributors;
    jobs[6].vec = &langs;

    for (int i = 0; i < 7; i++) {
        jobs[i].cfg = &cfg;
        snprintf(jobs[i].owner, sizeof(jobs[i].owner), "%s", owner);
        snprintf(jobs[i].repo, sizeof(jobs[i].repo), "%s", repo);
        jobs[i].since_days = cfg.default_days;
        jobs[i].use_cache = opts->no_cache ? 0 : 1;
    }

    Anim sp;
    anim_start(&sp, ANIM_BAR, 0, 7, "Fetching repository data");
    perf_mark("fetch");
    if (opts->serial_fetch) {
        /* Bounded-thread mode: the caller already runs repos in parallel, so
         * fetch one resource at a time on this thread. */
        for (int i = 0; i < 7; i++) {
            fetch_worker(&jobs[i]);
            anim_update(&sp, i + 1, 7);
        }
    } else {
        int created[7];
        int ncreated = 0;
        for (int i = 0; i < 7; i++) {
            if (pthread_create(&threads[i], NULL, fetch_worker, &jobs[i]) == 0)
                created[ncreated++] = i;
            else
                jobs[i].err = ERR_THREAD;
        }
        int done = 0;
        for (int t = 0; t < ncreated; t++) {
            pthread_join(threads[created[t]], NULL);
            anim_update(&sp, ++done, 7);
        }
    }
    perf_mark("fetched");
    anim_stop(&sp, 1);

    if (gs_cancel_requested()) {
        vector_free(&prs, pr_free);
        vector_free(&commits, commit_free);
        vector_free(&issues, issue_free);
        vector_free(&releases, release_free);
        vector_free(&contributors, contributor_free);
        vector_free(&langs, lang_free);
        return 2;   /* caller maps this to ERR_CANCELLED */
    }

    for (int i = 0; i < ANALYSIS_FETCH_KINDS; i++) {
        if (out) {
            out->fetch_err[i] = jobs[i].err;
            if (jobs[i].err != GS_OK && i != 6) {
                snprintf(out->fetch_err_text[i], sizeof(out->fetch_err_text[i]), "%s",
                         (jobs[i].client_ready && jobs[i].pclient.last_error[0])
                             ? jobs[i].pclient.last_error : gs_error_string(jobs[i].err));
            }
        }
    }

    for (int i = 0; i < 7; i++) {
        if (jobs[i].err != GS_OK && i != 6) {
            const char *detail = (jobs[i].client_ready && jobs[i].pclient.last_error[0])
                                 ? jobs[i].pclient.last_error : gs_error_string(jobs[i].err);
            color_printf_color(CLR_RED, "  -> Fetching %s failed: %s\n",
                               analysis_fetch_labels[i], detail);
        }
        if (jobs[i].client_ready)
            jobs[i].pclient.destroy(jobs[i].pclient.ctx);
    }

    apply_filters(&prs, &commits, &issues, &releases, to_iso, &fs);

    Report report;

    if (use_llm) {
        anim_start(&sp, ANIM_DOTS, 0, 0, "Analyzing with LLM");
        analyzer_run(&report, owner, repo, cfg.default_days, &prs, &commits, &issues, &releases, &contributors, &langs);
        perf_mark("analyze");
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
            if (out) {
                snprintf(out->llm_err, sizeof(out->llm_err), "%s", gs_error_string(err));
                out->used_llm = 1;
            }
            color_printf_color(CLR_YELLOW, "\nLLM analysis failed (%s), using local summary.\n",
                               gs_error_string(err));
        }
        else {
            /* The provider that wrote this summary, so a client credits the
             * one that ran rather than the one that happens to be active. */
            snprintf(report.llm_provider, sizeof(report.llm_provider), "%s", active_llm->name);
            if (out) out->used_llm = 1;
        }
        perf_mark("llm");
        anim_stop(&sp, 1);
    } else {
        anim_start(&sp, ANIM_BRAILLE, 0, 0, "Analyzing repository");
        analyzer_run(&report, owner, repo, cfg.default_days, &prs, &commits, &issues, &releases, &contributors, &langs);
        perf_mark("analyze");
        report.meta = meta;
        if (cfg.branch[0])
            snprintf(report.branch, sizeof(report.branch), "%s", cfg.branch);
        else
            snprintf(report.branch, sizeof(report.branch), "%s", meta.default_branch);
        anim_stop(&sp, 1);
    }

    if (has_from || has_to) {
        char s[16] = "", e[16] = "";
        if (has_from)
            epoch_to_iso(from_ts, s, sizeof(s));
        else
            epoch_to_iso(now - (long long)cfg.default_days * 86400, s, sizeof(s));
        if (has_to)
            epoch_to_iso(to_ts, e, sizeof(e));
        else
            epoch_to_iso(now, e, sizeof(e));
        snprintf(report.period, sizeof(report.period), "%s..%s", s, e);
    }

    if (out) {
        out->report = report;
        /* Hand the item vectors to the caller instead of freeing them, so a
         * caller can re-render this result into any export format later
         * without refetching. */
        out->prs = prs;
        out->commits = commits;
        out->issues = issues;
        out->releases = releases;
        out->contributors = contributors;
        out->langs = langs;
        return 0;
    }

    anim_start(&sp, ANIM_UNICODE, 0, 0, "Writing report");
    {
        if (strchr(cfg.format, ',')) {
            char fmt_copy[256];
            snprintf(fmt_copy, sizeof(fmt_copy), "%s", cfg.format);
            /* strtok_r, not strtok: a batch writes several formats on one
             * thread per repository, and strtok's state is shared. */
            char *fmt_save = NULL;
            char *token = strtok_r(fmt_copy, ",", &fmt_save);
            while (token) {
                while (*token == ' ') token++;
                if (*token == '\0') { token = strtok_r(NULL, ",", &fmt_save); continue; }

                Exporter e;
                if (exporter_init(&e, token) != GS_OK) {
                    color_printf_color(CLR_RED, "  -> Unsupported format: %s\n", token);
                    token = strtok_r(NULL, ",", &fmt_save);
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
                token = strtok_r(NULL, ",", &fmt_save);
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
                vector_free(&langs, lang_free);
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
                    vector_free(&langs, lang_free);
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
                    vector_free(&langs, lang_free);
                    return 1;
                }
                fp = stdout;
            }

            FormatOptions fopts;
            build_fopts(&fopts, &cfg, close_fp, cfg.format);
            perf_mark("prepare");
            exporter.write_report(fp, &report, &prs, &commits, &issues, &releases, &fopts);
            free_fopts(&fopts);
            perf_mark("export");

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
    vector_free(&langs, lang_free);

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

/* Split the comparison into gather and render so a long-running caller can run
 * the two periods as a cancellable job and then materialize either output
 * format from the same data, without refetching anything. */
int compare_analyze(const Options *opts, Config cfg, const char *repo_arg,
                    int d1, int d2, CompareResult *out) {
    if (!out) return 1;
    memset(out, 0, sizeof(*out));
    analysis_result_init(&out->period_a);
    analysis_result_init(&out->period_b);

    Options o = *opts;
    o.since_days = -1;
    o.suppress_llm = 1;   /* per-period reports feed one shared trend summary */

    Config c1 = cfg;
    c1.default_days = d1;
    Config c2 = cfg;
    c2.default_days = d2;

    out->days_a = d1;
    out->days_b = d2;

    if (analyze_repo(&o, c1, repo_arg, 0, &out->period_a) != 0) {
        analysis_result_free(&out->period_a);
        return 1;
    }
    if (analyze_repo(&o, c2, repo_arg, 0, &out->period_b) != 0) {
        analysis_result_free(&out->period_a);
        analysis_result_free(&out->period_b);
        return 1;
    }

    const LLMConfig *llm = config_get_active_llm(&cfg);
    /* A per-run --llm names the provider to use; the config's active one is
     * only the fallback. Without this the choice made on the console (or on
     * the command line) would silently not reach the trend summary. */
    if (opts->llm_name[0]) {
        for (int i = 0; i < cfg.llm_count; i++) {
            if (strcmp(cfg.llms[i].name, opts->llm_name) == 0) {
                llm = &cfg.llms[i];
                break;
            }
        }
    }
    if (llm && llm->api_key[0] && llm->api_base[0]) {
        Anim sp;
        anim_start(&sp, ANIM_DOTS, 0, 0, "Summarizing trend with LLM");
        if (analyzer_llm_compare(out->trend_summary, sizeof(out->trend_summary), llm,
                                 out->period_a.report.owner, out->period_a.report.repo,
                                 d1, d2, &out->period_a.report,
                                 &out->period_b.report) != GS_OK) {
            out->trend_summary[0] = '\0';
            anim_stop(&sp, 0);
            color_printf_color(CLR_YELLOW, "\nLLM trend summary failed, skipping.\n");
        } else {
            snprintf(out->llm_provider, sizeof(out->llm_provider), "%s", llm->name);
            anim_stop(&sp, 1);
        }
    }
    out->has_trend = out->trend_summary[0] != '\0';
    return 0;
}

void compare_result_free(CompareResult *cr) {
    if (!cr) return;
    analysis_result_free(&cr->period_a);
    analysis_result_free(&cr->period_b);
    cr->has_trend = 0;
    cr->trend_summary[0] = '\0';
}

static void compare_write_json(FILE *fp, const CompareResult *cr) {
    const Report *r1 = &cr->period_a.report;
    const Report *r2 = &cr->period_b.report;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "owner", r1->owner);
    cJSON_AddStringToObject(root, "repo", r1->repo);
    cJSON_AddNumberToObject(root, "period_days_a", cr->days_a);
    cJSON_AddNumberToObject(root, "period_days_b", cr->days_b);

    cJSON *ma = cJSON_AddObjectToObject(root, "period_a");
    cJSON_AddNumberToObject(ma, "pr_total", r1->pr_total);
    cJSON_AddNumberToObject(ma, "pr_merged", r1->pr_merged);
    cJSON_AddNumberToObject(ma, "commit_total", r1->commit_total);
    cJSON_AddNumberToObject(ma, "author_total", r1->author_total);
    cJSON_AddNumberToObject(ma, "issue_total", r1->issue_total);
    cJSON_AddNumberToObject(ma, "release_total", r1->release_total);
    cJSON_AddNumberToObject(ma, "contributor_total", r1->contributor_total);
    cJSON_AddNumberToObject(ma, "pr_avg_merge_hours", r1->pr_avg_merge_hours);
    cJSON_AddNumberToObject(ma, "issue_avg_close_hours", r1->issue_avg_close_hours);
    cJSON_AddNumberToObject(ma, "activity_count", r1->activity_count);
    /* Narrative fields the markdown view shows but the original JSON lacked;
     * additive only, so existing consumers keep working. */
    cJSON_AddStringToObject(ma, "top_author", r1->top_author);
    cJSON_AddStringToObject(ma, "top_contributor", r1->top_contributor);

    cJSON *mb = cJSON_AddObjectToObject(root, "period_b");
    cJSON_AddNumberToObject(mb, "pr_total", r2->pr_total);
    cJSON_AddNumberToObject(mb, "pr_merged", r2->pr_merged);
    cJSON_AddNumberToObject(mb, "commit_total", r2->commit_total);
    cJSON_AddNumberToObject(mb, "author_total", r2->author_total);
    cJSON_AddNumberToObject(mb, "issue_total", r2->issue_total);
    cJSON_AddNumberToObject(mb, "release_total", r2->release_total);
    cJSON_AddNumberToObject(mb, "contributor_total", r2->contributor_total);
    cJSON_AddNumberToObject(mb, "pr_avg_merge_hours", r2->pr_avg_merge_hours);
    cJSON_AddNumberToObject(mb, "issue_avg_close_hours", r2->issue_avg_close_hours);
    cJSON_AddNumberToObject(mb, "activity_count", r2->activity_count);
    cJSON_AddStringToObject(mb, "top_author", r2->top_author);
    cJSON_AddStringToObject(mb, "top_contributor", r2->top_contributor);

    if (cr->has_trend)
        cJSON_AddStringToObject(root, "trend_summary", cr->trend_summary);
    cJSON_AddStringToObject(root, "llm_provider", cr->llm_provider);

    char *str = cJSON_Print(root);
    fprintf(fp, "%s\n", str);
    free(str);
    cJSON_Delete(root);
}

static void compare_write_md(FILE *fp, const CompareResult *cr) {
    const Report *r1 = &cr->period_a.report;
    const Report *r2 = &cr->period_b.report;
    int d1 = cr->days_a, d2 = cr->days_b;

    fprintf(fp, "# Activity comparison: %s/%s\n\n", r1->owner, r1->repo);
    fprintf(fp, "Period A: last %d days | Period B: last %d days\n\n", d1, d2);
    fprintf(fp, "| Metric | A (%dd) | B (%dd) | Delta |\n", d1, d2);
    fprintf(fp, "|---|---|---|---|\n");
    print_compare_delta(fp, "Pull requests", r1->pr_total, r2->pr_total);
    print_compare_delta(fp, "PRs merged", r1->pr_merged, r2->pr_merged);
    print_compare_delta(fp, "Commits", r1->commit_total, r2->commit_total);
    print_compare_delta(fp, "Authors", r1->author_total, r2->author_total);
    print_compare_delta(fp, "Issues", r1->issue_total, r2->issue_total);
    print_compare_delta(fp, "Releases", r1->release_total, r2->release_total);
    print_compare_delta(fp, "Contributors", r1->contributor_total, r2->contributor_total);
    fprintf(fp, "\nTop author: %s  ->  %s\n", r1->top_author, r2->top_author);
    fprintf(fp, "Top contributor: %s  ->  %s\n", r1->top_contributor, r2->top_contributor);
    fprintf(fp, "Busiest weekday: %s  ->  %s\n", r1->activity_labels[r1->busiest_weekday],
            r2->activity_labels[r2->busiest_weekday]);
    if (r1->pr_merged_count > 0 || r2->pr_merged_count > 0)
        fprintf(fp, "Avg merge time: %.1fh  ->  %.1fh\n", r1->pr_avg_merge_hours, r2->pr_avg_merge_hours);
    if (r1->issue_closed_count > 0 || r2->issue_closed_count > 0)
        fprintf(fp, "Avg issue close time: %.1fh  ->  %.1fh\n", r1->issue_avg_close_hours, r2->issue_avg_close_hours);
    fprintf(fp, "\n");
    if (cr->has_trend)
        fprintf(fp, "## Trend summary\n\n%s\n\n", cr->trend_summary);
}

ErrorCode compare_render(const CompareResult *cr, const char *format,
                         char **buf, size_t *len) {
    if (!cr || !buf || !len) return ERR_INVALID_ARGS;
    *buf = NULL;
    *len = 0;
    FILE *fp = open_memstream(buf, len);
    if (!fp) return ERR_MEMORY;
    if (format && strcmp(format, "json") == 0)
        compare_write_json(fp, cr);
    else
        compare_write_md(fp, cr);
    fclose(fp);
    return GS_OK;
}

int run_compare(const Options *opts, Config cfg, const char *repo_arg, int d1, int d2) {
    if (opts->output[0])
        snprintf(cfg.output, sizeof(cfg.output), "%s", opts->output);
    if (opts->format[0])
        snprintf(cfg.format, sizeof(cfg.format), "%s", opts->format);

    CompareResult cr;
    if (compare_analyze(opts, cfg, repo_arg, d1, d2, &cr) != 0) return 1;

    char *rendered = NULL;
    size_t rlen = 0;
    if (compare_render(&cr, cfg.format, &rendered, &rlen) != GS_OK) {
        compare_result_free(&cr);
        return 1;
    }

    FILE *fp = stdout;
    int close_fp = 0;
    if (cfg.output[0]) {
        fp = fopen(cfg.output, "w");
        if (!fp) {
            fprintf(stderr, "Error: Cannot open %s\n", cfg.output);
            free(rendered);
            compare_result_free(&cr);
            return 1;
        }
        close_fp = 1;
    }

    if (rlen) fwrite(rendered, 1, rlen, fp);
    free(rendered);
    compare_result_free(&cr);

    if (close_fp) {
        fclose(fp);
        if (!opts->quiet)
            fprintf(stderr, "  -> Written to %s\n", cfg.output);
    }
    return 0;
}

/* Render a stored analysis into memory. open_memstream() backs the exporter's
 * FILE* with a heap buffer, so all seven formats -- including the binary xlsx
 * and pdf -- take exactly one path and never touch the filesystem. A caller
 * can therefore offer every format as a download without re-running the
 * analysis, and without caring about stdout-unsupported formats. */
ErrorCode analysis_render(const AnalysisResult *r, const Config *cfg,
                          const char *format, char **buf, size_t *len) {
    if (!r || !cfg || !format || !buf || !len) return ERR_INVALID_ARGS;
    *buf = NULL;
    *len = 0;

    Exporter e;
    if (exporter_init(&e, format) != GS_OK) return ERR_UNSUPPORTED_FORMAT;

    /* close_fp=1 means "output is going to a sink, not a terminal", which is
     * exactly right here: md/txt must not carry ANSI escapes. pdf/xlsx/html
     * ignore it and embed their own colors from the config instead. */
    FormatOptions fopts;
    if (!build_fopts(&fopts, cfg, 1, format)) return ERR_EXPORT;

    FILE *fp = open_memstream(buf, len);
    if (!fp) {
        free_fopts(&fopts);
        return ERR_MEMORY;
    }
    e.write_report(fp, &r->report, &r->prs, &r->commits, &r->issues,
                   &r->releases, &fopts);
    free_fopts(&fopts);
    if (fclose(fp) != 0) {
        free(*buf);
        *buf = NULL;
        *len = 0;
        return ERR_FILE_IO;
    }
    return GS_OK;
}
