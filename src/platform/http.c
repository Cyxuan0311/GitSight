#include "gitsight/platform/http.h"
#include "gitsight/cancel.h"
#include <curl/curl.h>
#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HTTP_MAX_RETRIES 3
#define GS_CACHE_TTL_DEFAULT 300

static void http_default_cache_dir(char *out, size_t len);

static int gs_cache_enabled = 1;

/* Last API quota seen on any response (-1 = not yet known). Advisory only:
 * authenticated and anonymous callers share this view. */
static atomic_long g_quota_remaining = -1;
static atomic_long g_quota_reset = 0;

/* What the process has spent on the network. Counted rather than timed, so
 * the numbers can be read without a profiler attached. */
static atomic_long g_stat_requests = 0;
static atomic_long g_stat_pages = 0;
static atomic_long g_stat_cache_hits = 0;
static atomic_long g_stat_cache_misses = 0;
static atomic_long g_stat_retries = 0;
static atomic_long g_stat_bytes = 0;

void http_stats(HttpStats *out) {
    if (!out) return;
    out->requests     = atomic_load(&g_stat_requests);
    out->pages        = atomic_load(&g_stat_pages);
    out->cache_hits   = atomic_load(&g_stat_cache_hits);
    out->cache_misses = atomic_load(&g_stat_cache_misses);
    out->retries      = atomic_load(&g_stat_retries);
    out->bytes        = atomic_load(&g_stat_bytes);
}

void http_quota(long *remaining, long *reset_at) {
    if (remaining) *remaining = atomic_load(&g_quota_remaining);
    if (reset_at)  *reset_at  = atomic_load(&g_quota_reset);
}

void http_set_cache_enabled(int enabled) {
    gs_cache_enabled = enabled;
}

void http_cache_info(char *dir, size_t len, long *ttl, int *enabled) {
    if (dir && len) http_default_cache_dir(dir, len);
    if (ttl) *ttl = GS_CACHE_TTL_DEFAULT;
    if (enabled) *enabled = gs_cache_enabled;
}

int url_encode(const char *src, char *dst, size_t dst_len) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            if (j + 1 >= dst_len) return -1;
            dst[j++] = c;
        } else {
            if (j + 3 >= dst_len) return -1;
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0xF];
            dst[j++] = hex[c & 0xF];
        }
    }
    if (j >= dst_len) return -1;
    dst[j] = '\0';
    return (int)j;
}

static void http_default_cache_dir(char *out, size_t len) {
    /* An explicit override, so a benchmark or a test can point the cache at a
     * throwaway directory instead of the user's real one. */
    const char *override = getenv("GS_CACHE_DIR");
    if (override && override[0]) {
        snprintf(out, len, "%s", override);
        return;
    }
    const char *base = getenv("XDG_CACHE_HOME");
    if (base && base[0]) {
        snprintf(out, len, "%s/gitsight", base);
        return;
    }
    base = getenv("HOME");
    if (base && base[0]) {
        snprintf(out, len, "%s/.cache/gitsight", base);
        return;
    }
    snprintf(out, len, "/tmp/gitsight-cache");
}

static unsigned long long fnv1a64(const char *s) {
    unsigned long long h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static void cache_path_for(const HttpClient *hc, const char *url, char *out, size_t len) {
    snprintf(out, len, "%s/%016llx.json", hc->cache_dir,
             (unsigned long long)fnv1a64(url));
}

static void mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t n = strlen(tmp);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0700);
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0700);
}

/* The cache directory, created once.
 *
 * Creating it on every write cost four or five syscalls per cached page, and
 * a run caches dozens of them. */
static int cache_dir_ready(const char *dir) {
    static _Thread_local char known[512] = "";
    if (known[0] && strcmp(known, dir) == 0) return 1;

    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        mkdir_p(dir);
        if (stat(dir, &st) != 0) return 0;
    }
    snprintf(known, sizeof(known), "%s", dir);
    return 1;
}

/* How many cached pages the directory may hold.
 *
 * The cache only ever holds what is still usable -- an entry past its
 * lifetime is dead weight -- but nothing removed the dead ones, so the
 * directory grew for as long as the daemon ran. */
#define CACHE_MAX_ENTRIES 2048

/* How often the directory is swept, in writes. Scanning it on every write
 * would cost more than the write does. */
#define CACHE_PRUNE_EVERY 64

/* Removes the entries that can no longer be used: the ones past their
 * lifetime, and -- if there are still too many -- the oldest ones. */
static void cache_prune(const HttpClient *hc) {
    DIR *dir = opendir(hc->cache_dir);
    if (!dir) return;

    /* The entries, oldest first. A run caches a few dozen pages, so the list
     * is small enough to sort in place. */
    struct CacheEntry { char name[256]; time_t mtime; } entries[CACHE_MAX_ENTRIES];
    int count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < CACHE_MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        size_t len = strlen(ent->d_name);
        if (len < 5 || len >= sizeof(entries[0].name)) continue;
        if (strcmp(ent->d_name + len - 5, ".json") != 0) continue;

        char path[768];
        snprintf(path, sizeof(path), "%s/%s", hc->cache_dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        snprintf(entries[count].name, sizeof(entries[0].name), "%.*s",
                 (int)(sizeof(entries[0].name) - 1), ent->d_name);
        entries[count].mtime = st.st_mtime;
        count++;
    }
    closedir(dir);

    if (count == 0) return;

    /* Oldest first. */
    for (int i = 1; i < count; i++) {
        struct CacheEntry key = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].mtime > key.mtime) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }

    time_t now = time(NULL);
    int keep = count;
    for (int i = 0; i < count; i++) {
        int expired = hc->cache_ttl > 0 && (now - entries[i].mtime) > hc->cache_ttl;
        int over_cap = keep > CACHE_MAX_ENTRIES;
        if (!expired && !over_cap) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%.400s/%.256s", hc->cache_dir, entries[i].name);
        unlink(path);
        keep--;
    }
}

/* The header every cached page carries.
 *
 * The pagination lives in the response headers, not in the body, so a cache
 * that kept only the body lost it -- and a run that hit the cache stopped
 * after the first page and reported a fraction of the repository. The header
 * keeps the body verbatim and stores the rest beside it.
 *
 * "GSC1" marks the format; a file without it was written by an older version
 * and is treated as a miss, so the cache heals itself. */
#define CACHE_MAGIC "GSC1"

/* Returns 1 on cache hit (fills result and the pagination it came with). */
static int cache_read(const HttpClient *hc, const char *url, HttpBuffer *result,
                      HttpResult *out) {
    if (!gs_cache_enabled || !hc->use_cache) return 0;

    char path[768];
    cache_path_for(hc, url, path, sizeof(path));

    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (hc->cache_ttl > 0 && (time(NULL) - st.st_mtime) > hc->cache_ttl) return 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    /* The header is a single line: the magic, the status, the next page
     * number and the next page URL, separated by pipes. */
    char header[2048];
    if (!fgets(header, sizeof(header), fp)) { fclose(fp); return 0; }
    if (strncmp(header, CACHE_MAGIC "|", strlen(CACHE_MAGIC) + 1) != 0) {
        fclose(fp);
        return 0;
    }

    long code = 0, page_num = 0;
    char next_page[1024] = "";
    int fields = sscanf(header + strlen(CACHE_MAGIC) + 1, "%ld|%ld|%1023[^|\n]",
                        &code, &page_num, next_page);
    if (fields < 2) { fclose(fp); return 0; }

    size_t body_len = (size_t)st.st_size - (size_t)(ftell(fp));
    if (ftell(fp) < 0 || body_len > (size_t)st.st_size) { fclose(fp); return 0; }

    char *data = malloc(body_len + 1);
    if (!data) { fclose(fp); return 0; }
    size_t rd = fread(data, 1, body_len, fp);
    fclose(fp);
    if (rd != body_len) { free(data); return 0; }

    data[rd] = '\0';
    result->data = data;
    result->size = rd;

    if (out) {
        out->http_code = code ? code : 200;
        out->next_page_num = page_num;
        snprintf(out->next_page, sizeof(out->next_page), "%s", next_page);
    }
    return 1;
}

static void cache_write(const HttpClient *hc, const char *url, const HttpBuffer *buf,
                        const HttpResult *res) {
    if (!gs_cache_enabled || !hc->use_cache) return;

    char path[768];
    cache_path_for(hc, url, path, sizeof(path));
    if (!cache_dir_ready(hc->cache_dir)) return;

    /* Sweep the directory every so often, so the entries that can no longer
     * be used do not pile up for as long as the daemon runs. */
    static atomic_long writes = 0;
    if (atomic_fetch_add(&writes, 1) % CACHE_PRUNE_EVERY == 0)
        cache_prune(hc);

    char tmp[832];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) return;
    fprintf(fp, "%s|%ld|%ld|%s\n", CACHE_MAGIC,
            res && res->http_code ? res->http_code : 200,
            res ? res->next_page_num : 0,
            (res && res->next_page[0]) ? res->next_page : "");
    fwrite(buf->data, 1, buf->size, fp);
    fclose(fp);
    rename(tmp, path);
}

/* ---- Offline fixture mode (GS_FIXTURE_DIR) ----
   When the GS_FIXTURE_DIR environment variable is set, http_get serves
   canned responses from that directory instead of the network. This lets
   tests exercise the full parsing/analysis/export pipeline offline.
   Fixture files: prs.json, commits.json, issues.json, releases.json,
   contributors.json, languages.json and meta.json. */
static const char *fixture_name_for_url(const char *url) {
    if (strstr(url, "/pulls") || strstr(url, "/merge_requests"))
        return "prs.json";
    if (strstr(url, "/commits"))       return "commits.json";
    if (strstr(url, "/issues"))        return "issues.json";
    if (strstr(url, "/releases"))      return "releases.json";
    if (strstr(url, "/contributors"))  return "contributors.json";
    if (strstr(url, "/languages"))     return "languages.json";
    if (strstr(url, "/stats/"))        return "code_frequency.json";
    return "meta.json";
}

/* Returns 1 if a fixture was served (result + out filled). */
static int fixture_read_file(const char *path, HttpBuffer *result) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);
    if (len < 0) len = 0;

    char *data = malloc((size_t)len + 1);
    if (!data) { fclose(fp); return 0; }
    size_t rd = fread(data, 1, (size_t)len, fp);
    fclose(fp);
    data[rd] = '\0';
    result->data = data;
    result->size = rd;
    return 1;
}

static int fixture_empty(HttpBuffer *result) {
    result->data = malloc(3);
    if (!result->data) return 1;
    memcpy(result->data, "[]", 3);
    result->size = 2;
    return 1;
}

static int http_get_fixture(const char *url, HttpBuffer *result, HttpResult *out) {
    const char *dir = getenv("GS_FIXTURE_DIR");
    if (!dir || dir[0] == '\0') return 0;

    const char *page = strstr(url, "?page=");
    if (!page) page = strstr(url, "&page=");
    int pagenum = page ? atoi(page + 6) : 1;
    out->http_code = 200;

    if (pagenum == 1) {
        /* Optional "next_page" marker: a plain integer simulates an
           X-Next-Page header, a URL simulates a Link rel="next" header. */
        char nxtpath[1024];
        snprintf(nxtpath, sizeof(nxtpath), "%s/next_page", dir);
        char nb[512] = {0};
        FILE *npf = fopen(nxtpath, "rb");
        if (npf) {
            size_t nrd = fread(nb, 1, sizeof(nb) - 1, npf);
            fclose(npf);
            nb[nrd] = '\0';
        }
        size_t n = strlen(nb);
        while (n > 0 && (nb[n-1] == '\n' || nb[n-1] == '\r' || nb[n-1] == ' ')) nb[--n] = '\0';
        if (nb[0] >= '0' && nb[0] <= '9') {
            long np = atol(nb);
            if (np > 0) out->next_page_num = np;
        } else if (nb[0]) {
            snprintf(out->next_page, sizeof(out->next_page), "%s", nb);
        }
    }

    char path[1024];
    if (pagenum > 1) {
        /* Dedicated per-page fixture file (e.g. prs.2.json); fall back to an
           empty array so pagination stops. */
        const char *fname = fixture_name_for_url(url);
        char base[64];
        size_t flen = strlen(fname);
        if (flen > 5 && strcmp(fname + flen - 5, ".json") == 0) {
            size_t n = flen - 5;
            if (n >= sizeof(base)) n = sizeof(base) - 1;
            memcpy(base, fname, n);
            base[n] = '\0';
        } else {
            snprintf(base, sizeof(base), "%s", fname);
        }
        snprintf(path, sizeof(path), "%s/%s.%d.json", dir, base, pagenum);
        if (fixture_read_file(path, result)) return 1;
        return fixture_empty(result);
    }
    snprintf(path, sizeof(path), "%s/%s", dir, fixture_name_for_url(url));
    if (fixture_read_file(path, result)) return 1;
    return fixture_empty(result);
}

void http_buffer_init(HttpBuffer *b) {
    b->data = NULL;
    b->size = 0;
}

void http_buffer_free(HttpBuffer *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

void http_client_init(HttpClient *hc, const char *auth_header, const char *proxy,
                      const char *accept, long timeout_secs, int max_pages)
{
    memset(hc, 0, sizeof(*hc));
    if (auth_header && auth_header[0]) snprintf(hc->auth_header, sizeof(hc->auth_header), "%s", auth_header);
    if (proxy && proxy[0]) snprintf(hc->proxy, sizeof(hc->proxy), "%s", proxy);
    snprintf(hc->accept, sizeof(hc->accept), "%s",
             (accept && accept[0]) ? accept : "application/json");
    hc->timeout_secs = timeout_secs > 0 ? timeout_secs : 30;
    hc->max_pages = max_pages > 0 ? max_pages : 20;
    hc->use_cache = 1;
    hc->handle = NULL;
    hc->cache_ttl = GS_CACHE_TTL_DEFAULT;
    http_default_cache_dir(hc->cache_dir, sizeof(hc->cache_dir));
}

void http_client_free(HttpClient *hc) {
    if (!hc) return;
    if (hc->handle) {
        curl_easy_cleanup((CURL *)hc->handle);
        hc->handle = NULL;
    }
}

static void sleep_secs(long secs) {
    if (secs <= 0) return;
    struct timespec ts;
    ts.tv_sec = secs;
    ts.tv_nsec = 0;
    while (nanosleep(&ts, &ts) == -1) { /* retry on EINTR */ }
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuffer *b = (HttpBuffer *)userdata;
    size_t total = size * nmemb;
    char *new_data = realloc(b->data, b->size + total + 1);
    if (!new_data) return 0;
    b->data = new_data;
    memcpy(b->data + b->size, ptr, total);
    b->size += total;
    b->data[b->size] = '\0';
    return total;
}

static size_t header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpResult *r = (HttpResult *)userdata;
    size_t len = size * nmemb;
    if (len == 0) return 0;

    if (len >= 16 && strncasecmp(ptr, "Link:", 5) == 0) {
        char *lt = strchr(ptr, '<');
        char *gt = lt ? strchr(lt, ')') : NULL;
        if (lt && strstr(ptr, "rel=\"next\"")) {
            char *end = strstr(lt, ">");
            if (end) {
                size_t n = (size_t)(end - lt - 1);
                if (n < sizeof(r->next_page)) {
                    memcpy(r->next_page, lt + 1, n);
                    r->next_page[n] = '\0';
                }
            }
        }
        (void)gt;
    } else if (len >= 13 && strncasecmp(ptr, "X-Next-Page:", 12) == 0) {
        long p = atol(ptr + 12);
        if (p > 0) r->next_page_num = p;
    } else if (len >= 24 && strncasecmp(ptr, "X-RateLimit-Remaining:", 22) == 0) {
        r->rate_limit_remaining = atol(ptr + 22);
        r->has_rate_limit = 1;
        atomic_store(&g_quota_remaining, r->rate_limit_remaining);
    } else if (len >= 20 && strncasecmp(ptr, "X-RateLimit-Reset:", 18) == 0) {
        r->rate_limit_reset = atol(ptr + 18);
        atomic_store(&g_quota_reset, r->rate_limit_reset);
    } else if (len >= 14 && strncasecmp(ptr, "Retry-After:", 12) == 0) {
        r->retry_after = atol(ptr + 12);
        r->has_retry_after = 1;
    }
    return len;
}

static void reset_result(HttpResult *r) {
    r->http_code = 0;
    r->next_page[0] = '\0';
    r->next_page_num = 0;
    r->rate_limit_remaining = -1;
    r->rate_limit_reset = 0;
    r->retry_after = 0;
    r->has_rate_limit = 0;
    r->has_retry_after = 0;
}

/* Prepares the client's handle for one request.
 *
 * The handle is reused between the pages of a resource, so the connection --
 * and the TLS handshake behind it -- is established once instead of once per
 * page. curl_easy_reset clears the options but keeps the connection cache,
 * which is exactly what is wanted here. */
static CURL *setup_curl(const HttpClient *hc, const char *url,
                        HttpBuffer *buf, HttpResult *res,
                        struct curl_slist **headers)
{
    CURL *curl = (CURL *)hc->handle;
    if (curl) {
        curl_easy_reset(curl);
    } else {
        curl = curl_easy_init();
        if (!curl) return NULL;
        ((HttpClient *)hc)->handle = curl;
    }

    gs_cancel_attach(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, res);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, hc->timeout_secs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)(hc->timeout_secs / 2 + 1));

    if (hc->proxy[0]) {
        if (strncmp(hc->proxy, "socks5", 6) == 0)
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
        curl_easy_setopt(curl, CURLOPT_PROXY, hc->proxy);
    }

    *headers = NULL;
    *headers = curl_slist_append(*headers, hc->accept);
    *headers = curl_slist_append(*headers, "User-Agent: GitSight/1.0");
    if (hc->auth_header[0])
        *headers = curl_slist_append(*headers, hc->auth_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *headers);
    return curl;
}

static int is_transient_curl(CURLcode code) {
    switch (code) {
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_PARTIAL_FILE:
    case CURLE_GOT_NOTHING:
    case CURLE_HTTP2:
        return 1;
    default:
        return 0;
    }
}

static int is_transient_http(long code) {
    return code == 500 || code == 502 || code == 503 || code == 504;
}

ErrorCode http_get(const HttpClient *hc, const char *url,
                   HttpBuffer *result, HttpResult *out)
{
    HttpBuffer buf;
    http_buffer_init(&buf);
    reset_result(out);

    if (http_get_fixture(url, &buf, out)) {
        *result = buf;
        return GS_OK;
    }

    if (cache_read(hc, url, &buf, out)) {
        atomic_fetch_add(&g_stat_cache_hits, 1);
        *result = buf;
        return GS_OK;
    }
    atomic_fetch_add(&g_stat_cache_misses, 1);

    /* Proactive throttle: we already know the quota is spent — wait for the
     * window to reopen instead of firing requests that can only 403. */
    if (atomic_load(&g_quota_remaining) == 0) {
        long wait = atomic_load(&g_quota_reset) - (long)time(NULL) + 1;
        if (wait > 0 && wait <= 3660) {
            fprintf(stderr, "  -> API quota exhausted, waiting %lds for reset...\n", wait);
            sleep_secs(wait);
        }
        atomic_store(&g_quota_remaining, -1);
    }

    int attempt = 0;
    for (;;) {
        buf.size = 0;
        if (buf.data) buf.data[0] = '\0';

        struct curl_slist *headers = NULL;
        CURL *curl = setup_curl(hc, url, &buf, out, &headers);
        if (!curl) {
            http_buffer_free(&buf);
            return ERR_CURL_INIT;
        }

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        atomic_fetch_add(&g_stat_requests, 1);
        atomic_fetch_add(&g_stat_bytes, (long)buf.size);

        if (res != CURLE_OK) {
            if (res == CURLE_ABORTED_BY_CALLBACK || gs_cancel_requested()) {
                http_buffer_free(&buf);
                return ERR_CANCELLED;
            }
            if (is_transient_curl(res) && attempt < HTTP_MAX_RETRIES) {
                attempt++;
                atomic_fetch_add(&g_stat_retries, 1);
                sleep_secs(1L << (attempt - 1));
                continue;
            }
            http_buffer_free(&buf);
            return ERR_NETWORK;
        }

        out->http_code = http_code;

        if (http_code == 200) {
            *result = buf;
            cache_write(hc, url, &buf, out);
            return GS_OK;
        }

        /* Rate limit handling: 403/429 when quota exhausted */
        if ((http_code == 403 || http_code == 429) &&
            out->has_rate_limit && out->rate_limit_remaining == 0 &&
            attempt < HTTP_MAX_RETRIES)
        {
            long wait = out->rate_limit_reset - (long)time(NULL) + 1;
            if (out->has_retry_after && out->retry_after > 0)
                wait = out->retry_after;
            if (wait > 60) wait = 60;
            if (wait > 0) {
                fprintf(stderr, "  -> GitHub API rate limit reached, waiting %lds...\n", wait);
                sleep_secs(wait);
            }
            attempt++;
            atomic_fetch_add(&g_stat_retries, 1);
            continue;
        }
        if (http_code == 429 && out->has_retry_after &&
            out->retry_after > 0 && attempt < HTTP_MAX_RETRIES)
        {
            sleep_secs(out->retry_after);
            attempt++;
            atomic_fetch_add(&g_stat_retries, 1);
            continue;
        }

        /* Transient 5xx: exponential backoff */
        if (is_transient_http(http_code) && attempt < HTTP_MAX_RETRIES) {
            attempt++;
            sleep_secs(1L << (attempt - 1));
            continue;
        }

        http_buffer_free(&buf);
        return ERR_HTTP_STATUS;
    }
}

/* Set the "page" query parameter on url (replace existing, or append).
   The parameter is only matched when preceded by '?' or '&' so a substring
   of another parameter (e.g. per_page=) is never mistaken for it. */
static void set_page_param(char *url, size_t len, long page) {
    char num[32];
    snprintf(num, sizeof(num), "%ld", page);

    char *buf = malloc(4096);
    if (!buf) return;

    const char *q = strstr(url, "?page=");
    const char *a = strstr(url, "&page=");
    const char *page_eq = q ? q : a;
    if (page_eq) {
        const char *digits = page_eq + 6;
        const char *end = digits;
        while (*end >= '0' && *end <= '9') end++;
        snprintf(buf, 4096, "%.*s%s%s",
                 (int)(digits - url), url, num, end);
    } else {
        const char *sep = strchr(url, '?') ? "&" : "?";
        snprintf(buf, 4096, "%s%spage=%s", url, sep, num);
    }
    snprintf(url, len, "%s", buf);
    free(buf);
}

ErrorCode http_get_paginated(const HttpClient *hc, const char *url,
                             HttpPageFn cb, void *userdata, long *last_http)
{
    char current[2048];
    snprintf(current, sizeof(current), "%s", url);
    if (last_http) *last_http = 0;

    for (int page = 0; page < hc->max_pages; page++) {
        HttpBuffer buf;
        HttpResult res;
        ErrorCode err = http_get(hc, current, &buf, &res);
        if (last_http) *last_http = res.http_code;
        if (err != GS_OK) return err;

        int stop = cb(userdata, buf.data, &res);
        http_buffer_free(&buf);
        atomic_fetch_add(&g_stat_pages, 1);

        if (stop || (res.next_page[0] == '\0' && res.next_page_num == 0)) return GS_OK;
        if (res.next_page[0])
            snprintf(current, sizeof(current), "%s", res.next_page);
        else
            set_page_param(current, sizeof(current), res.next_page_num);
    }
    return GS_OK;
}
