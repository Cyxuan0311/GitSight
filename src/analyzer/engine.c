#include "gitsight/analyzer/engine.h"
#include "gitsight/error.h"
#include "gitsight/i18n.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>

#define MAX_LABEL_ENTRIES 128
#define MAX_PROMPT_ITEMS 50
#define MAX_ACTIVITY_BUCKETS 60
#define TOP_CONTRIB_LIST 5
#define LLM_PROMPT_MAX_CHARS 16000

typedef struct {
    char name[64];
    int count;
} LabelCounter;

/* ---- Date helpers (UTC, portable) ---- */
static long days_from_civil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static long parse_iso_ts(const char *s) {
    if (!s || s[0] == '\0') return 0;
    int y, mo, d, h = 0, mi = 0, sec = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 3) {
        if (sscanf(s, "%d-%d-%d", &y, &mo, &d) < 3) return 0;
    }
    if (y < 1970) return 0;
    long days = days_from_civil(y, (unsigned)mo, (unsigned)d);
    return days * 86400L + (long)h * 3600L + (long)mi * 60L + (long)sec;
}

static void find_top_labels(const Vector *prs, const Vector *issues,
                            char *out, size_t out_len)
{
    LabelCounter labels[MAX_LABEL_ENTRIES];
    int label_count = 0;

    const Vector *vectors[] = {prs, issues};
    int label_offset[] = {
        (int)offsetof(PR, labels),
        (int)offsetof(Issue, labels)
    };

    for (int v = 0; v < 2; v++) {
        for (size_t i = 0; i < vector_len(vectors[v]); i++) {
            void *item = vector_get(vectors[v], i);
            char *labels_str = (char *)((char *)item + label_offset[v]);
            if (labels_str[0] == '\0') continue;

            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s", labels_str);
            /* strtok_r, not strtok: the analyzer runs on one thread per
             * repository, and strtok keeps its state in a buffer that is
             * shared by every thread. */
            char *save = NULL;
            char *token = strtok_r(tmp, ",", &save);
            while (token) {
                while (*token == ' ') token++;
                if (*token == '\0') { token = strtok_r(NULL, ",", &save); continue; }

                int found = 0;
                for (int j = 0; j < label_count; j++) {
                    if (strcmp(labels[j].name, token) == 0) {
                        labels[j].count++;
                        found = 1;
                        break;
                    }
                }
                if (!found && label_count < MAX_LABEL_ENTRIES) {
                    snprintf(labels[label_count].name, sizeof(labels[label_count].name),
                             "%s", token);
                    labels[label_count].count = 1;
                    label_count++;
                }
                token = strtok_r(NULL, ",", &save);
            }
        }
    }

    out[0] = '\0';
    for (int sort = 0; sort < 5 && sort < label_count; sort++) {
        int best = -1;
        for (int j = 0; j < label_count; j++) {
            if (labels[j].count < 0) continue;
            if (best < 0 || labels[j].count > labels[best].count) {
                best = j;
            }
        }
        if (best >= 0) {
            if (out[0] != '\0') strncat(out, ", ", out_len - strlen(out) - 1);
            strncat(out, labels[best].name, out_len - strlen(out) - 1);
            labels[best].count = -1;
        }
    }
}

static void compute_merge_close_times(Report *report, const Vector *prs,
                                      const Vector *issues)
{
    long total_merge = 0;
    int merge_count = 0;
    for (size_t i = 0; i < vector_len(prs); i++) {
        PR *pr = (PR *)vector_get(prs, i);
        if (pr->merged_at[0] == '\0') continue;
        long created = parse_iso_ts(pr->created_at);
        long merged = parse_iso_ts(pr->merged_at);
        if (created > 0 && merged > created) {
            total_merge += (merged - created);
            merge_count++;
        }
    }
    report->pr_merged_count = merge_count;
    if (merge_count > 0)
        report->pr_avg_merge_hours = (double)total_merge / merge_count / 3600.0;

    long total_close = 0;
    int close_count = 0;
    for (size_t i = 0; i < vector_len(issues); i++) {
        Issue *iss = (Issue *)vector_get(issues, i);
        if (iss->closed_at[0] == '\0') continue;
        long created = parse_iso_ts(iss->created_at);
        long closed = parse_iso_ts(iss->closed_at);
        if (created > 0 && closed > created) {
            total_close += (closed - created);
            close_count++;
        }
    }
    report->issue_closed_count = close_count;
    if (close_count > 0)
        report->issue_avg_close_hours = (double)total_close / close_count / 3600.0;
}

static void compute_activity(Report *report, const Vector *commits, int since_days)
{
    memset(report->activity_values, 0, sizeof(report->activity_values));
    memset(report->weekday_values, 0, sizeof(report->weekday_values));
    report->activity_count = 0;
    report->activity_max = 0;
    report->busiest_weekday = 0;
    report->activity_labels[0][0] = '\0';

    size_t n = vector_len(commits);
    if (n == 0) return;

    int buckets = since_days;
    if (buckets < 1) buckets = 1;
    if (buckets > MAX_ACTIVITY_BUCKETS) buckets = MAX_ACTIVITY_BUCKETS;
    int span = (since_days + buckets - 1) / buckets;
    if (span < 1) span = 1;

    long now = time(NULL);
    for (size_t i = 0; i < n; i++) {
        Commit *c = (Commit *)vector_get(commits, i);
        long ts = parse_iso_ts(c->date);
        if (ts <= 0) continue;

        long diff = now - ts;
        if (diff < 0) diff = 0;
        int idx = (int)(diff / (span * 86400L));
        if (idx >= buckets) idx = buckets - 1;
        report->activity_values[idx]++;

        struct tm tmv;
        time_t t = (time_t)ts;
        gmtime_r(&t, &tmv);
        int wd = tmv.tm_wday; /* 0=Sun .. 6=Sat */
        report->weekday_values[wd]++;
    }

    report->activity_count = buckets;
    for (int i = 0; i < buckets; i++) {
        time_t day_start = now - (long)(i * span * 86400L);
        struct tm tmv;
        gmtime_r(&day_start, &tmv);
        snprintf(report->activity_labels[i], 16, "%02d-%02d", tmv.tm_mon + 1, tmv.tm_mday);
        if (report->activity_values[i] > report->activity_max)
            report->activity_max = report->activity_values[i];
    }

    report->busiest_weekday = 0;
    for (int i = 1; i < 7; i++) {
        if (report->weekday_values[i] > report->weekday_values[report->busiest_weekday])
            report->busiest_weekday = i;
    }
}

static void build_contributor_stats(Report *report, const Vector *contributors)
{
    report->top_contributor_list[0] = '\0';
    size_t n = vector_len(contributors);
    if (n == 0) return;

    report->contributor_total = (int)n;

    int best_idx = -1;
    int best_count = -1;
    for (size_t i = 0; i < n; i++) {
        Contributor *ct = (Contributor *)vector_get(contributors, i);
        if (ct->contributions > best_count) {
            best_count = ct->contributions;
            best_idx = (int)i;
        }
    }
    if (best_idx >= 0) {
        Contributor *ct = (Contributor *)vector_get(contributors, best_idx);
        snprintf(report->top_contributor, sizeof(report->top_contributor),
                 "%s (%d contributions)", ct->login, ct->contributions);
    }

    /* Top-N list, best first */
    int shown = 0;
    int used[512];
    int n_used = 0;
    while (shown < TOP_CONTRIB_LIST) {
        int b = -1;
        int bc = -1;
        for (size_t i = 0; i < n && n_used < 512; i++) {
            Contributor *ct = (Contributor *)vector_get(contributors, i);
            int skip = 0;
            for (int j = 0; j < n_used; j++) if (used[j] == (int)i) { skip = 1; break; }
            if (skip) continue;
            if (ct->contributions > bc) { bc = ct->contributions; b = (int)i; }
        }
        if (b < 0) break;
        used[n_used++] = b;
        Contributor *ct = (Contributor *)vector_get(contributors, b);
        char part[384];
        snprintf(part, sizeof(part), "%s (%d)", ct->login, ct->contributions);
        if (report->top_contributor_list[0])
            strncat(report->top_contributor_list, ", ",
                    sizeof(report->top_contributor_list) - strlen(report->top_contributor_list) - 1);
        strncat(report->top_contributor_list, part,
                sizeof(report->top_contributor_list) - strlen(report->top_contributor_list) - 1);
        shown++;
    }
}

static void build_language_stats(Report *report, const Vector *langs)
{
    report->lang_count = 0;
    if (!langs) return;
    size_t n = vector_len(langs);
    if (n == 0) return;

    /* sort a local index array desc by bytes (insertion sort, n is small) */
    int order[REPORT_LANGS_MAX * 4];
    if (n > (size_t)(REPORT_LANGS_MAX * 4)) n = (size_t)(REPORT_LANGS_MAX * 4);
    for (size_t i = 0; i < n; i++) order[i] = (int)i;
    for (size_t i = 1; i < n; i++) {
        int key = order[i];
        long long kb = ((const LangEntry *)vector_get(langs, key))->bytes;
        size_t j = i;
        while (j > 0 && ((const LangEntry *)vector_get(langs, order[j - 1]))->bytes < kb) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }

    int shown = 0;
    for (size_t i = 0; i < n && shown < REPORT_LANGS_MAX; i++) {
        const LangEntry *le = (const LangEntry *)vector_get(langs, order[i]);
        if (le->bytes <= 0) continue;
        snprintf(report->lang_names[shown], sizeof(report->lang_names[0]),
                 "%s", le->name);
        report->lang_bytes[shown] = le->bytes;
        shown++;
    }
    report->lang_count = shown;
}

void analyzer_run(Report *report, const char *owner, const char *repo,
                  int since_days, const Vector *prs,
                  const Vector *commits, const Vector *issues,
                  const Vector *releases, const Vector *contributors,
                  const Vector *langs)
{
    memset(report, 0, sizeof(Report));

    snprintf(report->owner, sizeof(report->owner), "%s", owner);
    snprintf(report->repo, sizeof(report->repo), "%s", repo);
    report->period_days = since_days;
    snprintf(report->period, sizeof(report->period), gs_t("last %d days"), since_days);

    report->pr_total = (int)vector_len(prs);
    report->pr_open = 0;
    report->pr_closed = 0;
    report->pr_merged = 0;

    for (size_t i = 0; i < vector_len(prs); i++) {
        PR *pr = (PR *)vector_get(prs, i);
        if (strcmp(pr->state, "open") == 0) {
            report->pr_open++;
        } else {
            report->pr_closed++;
            if (pr->merged_at[0] != '\0') {
                report->pr_merged++;
            }
        }
    }

    report->commit_total = (int)vector_len(commits);

    char top_author_tmp[MAX_AUTHOR_LEN + 1] = "";
    int top_author_count = 0;
    report->author_total = 0;

    {
        typedef struct {
            char name[MAX_AUTHOR_LEN];
            int count;
        } AuthorCounter;

        AuthorCounter authors[256];
        int author_count = 0;

        for (size_t i = 0; i < vector_len(commits); i++) {
            Commit *c = (Commit *)vector_get(commits, i);
            if (c->author[0] == '\0') continue;

            int found = 0;
            for (int j = 0; j < author_count; j++) {
                if (strcmp(authors[j].name, c->author) == 0) {
                    authors[j].count++;
                    found = 1;
                    break;
                }
            }
            if (!found && author_count < 256) {
                snprintf(authors[author_count].name, sizeof(authors[author_count].name),
                         "%s", c->author);
                authors[author_count].count = 1;
                author_count++;
            }
        }

        report->author_total = author_count;

        for (int j = 0; j < author_count; j++) {
            if (authors[j].count > top_author_count) {
                top_author_count = authors[j].count;
                strncpy(top_author_tmp, authors[j].name, sizeof(top_author_tmp) - 1);
                top_author_tmp[sizeof(top_author_tmp) - 1] = '\0';
            }
        }
    }

    if (top_author_tmp[0]) {
        snprintf(report->top_author, sizeof(report->top_author), "%s (%d commits)",
                 top_author_tmp, top_author_count);
    }

    report->issue_total = (int)vector_len(issues);
    report->issue_open = 0;
    report->issue_closed = 0;

    for (size_t i = 0; i < vector_len(issues); i++) {
        Issue *iss = (Issue *)vector_get(issues, i);
        if (strcmp(iss->state, "open") == 0)
            report->issue_open++;
        else
            report->issue_closed++;
    }

    find_top_labels(prs, issues, report->top_labels, sizeof(report->top_labels));

    /* Releases */
    report->release_total = (int)vector_len(releases);
    report->latest_release[0] = '\0';
    if (report->release_total > 0) {
        Release *first = (Release *)vector_get(releases, 0);
        if (first) snprintf(report->latest_release, sizeof(report->latest_release),
                            "%s", first->tag_name);
    }

    /* Contributors: prefer real API data, fall back to commit authors */
    report->contributor_total = report->author_total;
    snprintf(report->top_contributor, sizeof(report->top_contributor), "%s", report->top_author);
    build_contributor_stats(report, contributors);

    compute_merge_close_times(report, prs, issues);
    compute_activity(report, commits, since_days);
    build_language_stats(report, langs);

    char summary[MAX_SUMMARY_LEN];
    int pos = 0;

    pos += snprintf(summary + pos, sizeof(summary) - pos,
        gs_t("The repository %s/%s has been active over the %s. "), owner, repo, report->period);

    if (report->pr_total > 0) {
        pos += snprintf(summary + pos, sizeof(summary) - pos,
            gs_t("There were %d pull requests total (%d merged, %d open, %d closed). "),
            report->pr_total, report->pr_merged, report->pr_open,
            report->pr_closed - report->pr_merged);
        if (report->pr_merged_count > 0) {
            pos += snprintf(summary + pos, sizeof(summary) - pos,
                gs_t("Average merge time was %.1f hours. "),
                report->pr_avg_merge_hours);
        }
    } else {
        pos += snprintf(summary + pos, sizeof(summary) - pos,
            gs_t("No pull requests found in this period. "));
    }

    if (report->commit_total > 0) {
        pos += snprintf(summary + pos, sizeof(summary) - pos,
            gs_t("%d commits from %d contributors were made, with %s being the most active. "),
            report->commit_total, report->author_total, report->top_author);
    }

    if (report->issue_total > 0) {
        pos += snprintf(summary + pos, sizeof(summary) - pos,
            gs_t("%d issues were active (%d open, %d closed). "),
            report->issue_total, report->issue_open, report->issue_closed);
        if (report->issue_closed_count > 0) {
            pos += snprintf(summary + pos, sizeof(summary) - pos,
                gs_t("Average close time was %.1f hours. "),
                report->issue_avg_close_hours);
        }
    }

    if (report->top_labels[0]) {
        pos += snprintf(summary + pos, sizeof(summary) - pos,
            gs_t("Top labels: %s."), report->top_labels);
    }

    if (report->release_total > 0) {
        pos += snprintf(summary + pos, sizeof(summary) - pos,
            gs_t(" %d releases published, latest: %s."),
            report->release_total, report->latest_release);
    }

    snprintf(report->summary, sizeof(report->summary), "%s", summary);
}

ErrorCode analyzer_llm_summary(char *summary, size_t summary_len,
                               const LLMConfig *llm,
                               const char *owner, const char *repo,
                               int since_days,
                               const Vector *prs,
                               const Vector *commits,
                               const Vector *issues,
                               const Vector *releases,
                               const char *mode)
{
    if (!llm->api_key[0] || !llm->api_base[0]) {
        return ERR_LLM_CONFIG;
    }

    if (!mode || mode[0] == '\0') mode = "health";

    char prompt[65536];
    int pos = 0;
    int truncated = 0;

#define CHECK_PT() do { \
    if (pos >= LLM_PROMPT_MAX_CHARS || pos >= (int)sizeof(prompt) - 2000) { \
        truncated = 1; \
        snprintf(prompt + pos, sizeof(prompt) - pos, "\n[data truncated]\n"); \
        goto prompt_done; \
    } } while(0)

    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
        "Analyze the following GitHub repository activity.\n\n"
        "Repository: %s/%s\n"
        "Period: last %d days\n"
        "Mode: %s\n\n",
        owner, repo, since_days, mode);

    CHECK_PT();
    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
        "=== Pull Requests (%zu total) ===\n", vector_len(prs));

    size_t count = vector_len(prs);
    if (count > MAX_PROMPT_ITEMS) count = MAX_PROMPT_ITEMS;
    for (size_t i = 0; i < count; i++) {
        CHECK_PT();
        PR *pr = (PR *)vector_get(prs, i);
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "#%d \"%s\" [%s] by %s\n",
            pr->number, pr->title, pr->state, pr->author);
    }
    if (vector_len(prs) > MAX_PROMPT_ITEMS) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "... and %zu more PRs\n", vector_len(prs) - MAX_PROMPT_ITEMS);
    }

    CHECK_PT();
    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
        "\n=== Commits (%zu total) ===\n", vector_len(commits));

    count = vector_len(commits);
    if (count > MAX_PROMPT_ITEMS) count = MAX_PROMPT_ITEMS;
    for (size_t i = 0; i < count; i++) {
        CHECK_PT();
        Commit *c = (Commit *)vector_get(commits, i);
        char first_line[MAX_MSG_LEN];
        snprintf(first_line, sizeof(first_line), "%s", c->message);
        char *nl = strchr(first_line, '\n');
        if (nl) *nl = '\0';
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "%s \"%s\" by %s\n",
            c->sha, first_line, c->author);
    }
    if (vector_len(commits) > MAX_PROMPT_ITEMS) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "... and %zu more commits\n", vector_len(commits) - MAX_PROMPT_ITEMS);
    }

    CHECK_PT();
    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
        "\n=== Issues (%zu total) ===\n", vector_len(issues));

    count = vector_len(issues);
    if (count > MAX_PROMPT_ITEMS) count = MAX_PROMPT_ITEMS;
    for (size_t i = 0; i < count; i++) {
        CHECK_PT();
        Issue *iss = (Issue *)vector_get(issues, i);
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "#%d \"%s\" [%s] by %s\n",
            iss->number, iss->title, iss->state, iss->author);
    }
    if (vector_len(issues) > MAX_PROMPT_ITEMS) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "... and %zu more issues\n", vector_len(issues) - MAX_PROMPT_ITEMS);
    }

    CHECK_PT();
    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
        "\n=== Releases (%zu total) ===\n", vector_len(releases));
    count = vector_len(releases);
    if (count > MAX_PROMPT_ITEMS) count = MAX_PROMPT_ITEMS;
    for (size_t i = 0; i < count; i++) {
        CHECK_PT();
        Release *rel = (Release *)vector_get(releases, i);
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "%s \"%s\" by %s\n", rel->tag_name, rel->name, rel->author);
        if (rel->body[0]) {
            char body_trunc[256];
            snprintf(body_trunc, sizeof(body_trunc), "%.200s", rel->body);
            if (strlen(rel->body) > 200)
                strncat(body_trunc, "...", sizeof(body_trunc) - strlen(body_trunc) - 1);
            pos += snprintf(prompt + pos, sizeof(prompt) - pos,
                "  Body: %s\n", body_trunc);
        }
    }
    if (vector_len(releases) > MAX_PROMPT_ITEMS) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "... and %zu more releases\n", vector_len(releases) - MAX_PROMPT_ITEMS);
    }

    CHECK_PT();
    if (strcmp(mode, "trends") == 0) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\nProvide an analysis focusing on trends:\n"
            "1. What themes or areas are gaining or losing attention\n"
            "2. Changes in contributor and activity patterns over the period\n"
            "3. Shifts in issue/PR mix\n"
            "4. Brief summary (2-3 sentences)\n");
    } else if (strcmp(mode, "release") == 0) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\nProvide an analysis focused on release readiness:\n"
            "1. Notable merged PRs and changes that belong in release notes\n"
            "2. Outstanding work or risks before a release\n"
            "3. Suggested highlights for a changelog\n"
            "4. Brief summary (2-3 sentences)\n");
    } else {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\nProvide a concise analysis covering:\n"
            "1. Overall activity level and project health\n"
            "2. Main themes and areas of focus\n"
            "3. Notable changes or patterns\n"
            "4. Brief summary (2-3 sentences)\n");
    }

    if (gs_get_lang() == GS_LANG_ZH) {
        CHECK_PT();
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\nWrite the entire analysis in Simplified Chinese.\n");
    }

prompt_done:
    {
        size_t est_tokens = (size_t)pos / 4;
        if (est_tokens > 500) {
            fprintf(stderr, "  -> LLM prompt ~%zu tokens (%s)\n",
                    est_tokens, truncated ? "truncated" : "OK");
        }
    }
    return llm_generate(llm, prompt, summary, summary_len);
}

ErrorCode analyzer_llm_compare(char *out, size_t out_len,
                               const LLMConfig *llm,
                               const char *owner, const char *repo,
                               int days_a, int days_b,
                               const Report *a, const Report *b)
{
    if (!llm->api_key[0] || !llm->api_base[0]) {
        return ERR_LLM_CONFIG;
    }

    char prompt[4096];
    snprintf(prompt, sizeof(prompt),
        "Compare two activity snapshots of the repository %s/%s.\n"
        "Window A: the last %d days (recent, short-term).\n"
        "Window B: the last %d days (longer-term baseline).\n\n"
        "Metric              A        B\n"
        "Pull requests       %-8d%-8d\n"
        "PRs merged          %-8d%-8d\n"
        "Commits             %-8d%-8d\n"
        "Commit authors      %-8d%-8d\n"
        "Top author          %-8s%-8s\n"
        "Issues              %-8d%-8d\n"
        "Issues open         %-8d%-8d\n"
        "Releases            %-8d%-8d\n"
        "Contributors        %-8d%-8d\n"
        "Top contributor     %-8s%-8s\n"
        "Avg merge time (h)  %-8.1f%-8.1f\n"
        "Avg close time (h)  %-8.1f%-8.1f\n"
        "Top labels A: %s\n"
        "Top labels B: %s\n\n"
        "Based only on these numbers, write 2-4 sentences describing how the\n"
        "project's activity changed in the recent short window relative to the\n"
        "longer baseline: accelerating or slowing, focus shifts, contributor\n"
        "changes, and what that implies for project health.\n",
        owner, repo, days_a, days_b,
        a->pr_total, b->pr_total,
        a->pr_merged, b->pr_merged,
        a->commit_total, b->commit_total,
        a->author_total, b->author_total,
        a->top_author[0] ? a->top_author : "-", b->top_author[0] ? b->top_author : "-",
        a->issue_total, b->issue_total,
        a->issue_open, b->issue_open,
        a->release_total, b->release_total,
        a->contributor_total, b->contributor_total,
        a->top_contributor[0] ? a->top_contributor : "-",
        b->top_contributor[0] ? b->top_contributor : "-",
        a->pr_avg_merge_hours, b->pr_avg_merge_hours,
        a->issue_avg_close_hours, b->issue_avg_close_hours,
        a->top_labels, b->top_labels);

    if (gs_get_lang() == GS_LANG_ZH)
        strncat(prompt, "\nWrite the entire analysis in Simplified Chinese.\n",
                sizeof(prompt) - strlen(prompt) - 1);

    return llm_generate(llm, prompt, out, out_len);
}
