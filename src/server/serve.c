#define _GNU_SOURCE

#include "server.h"
#include "jobs.h"

#include "gitsight/anim.h"
#include "gitsight/color.h"
#include "gitsight/i18n.h"
#include "gitsight/usage.h"
#include "gitsight/version.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

ServerState g_server;

/* Serializes anything that reads or writes the process-global state the
 * analysis path touches: the language global, the usage log, the HTTP response
 * cache, and the config file. The work is network bound and the daemon is
 * single-user, so one writer keeps all of those consistent without threading
 * new locks through the existing modules. */
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

void server_lock(void)   { pthread_mutex_lock(&g_state_lock); }
void server_unlock(void) { pthread_mutex_unlock(&g_state_lock); }

int server_load_config(Config *out) {
    if (!out) return -1;
    pthread_mutex_lock(&g_state_lock);
    config_init(out);
    config_load(out, g_server.config_path);
    config_resolve_prompts(out, g_server.config_path);
    pthread_mutex_unlock(&g_state_lock);
    return 0;
}

int server_config_path(char *out, size_t len) {
    if (!out || !len) return -1;
    snprintf(out, len, "%s", g_server.config_path);
    return 0;
}

/* There is no terminal behind the daemon, so nothing to colour and no spinner to
 * draw. Document colors for pdf/xlsx/html still come from the config, which is
 * why the mode is ALWAYS rather than NEVER. */
void server_init_presentation(void) {
    color_init(COLOR_ALWAYS);
    anim_set_quiet(1);
    anim_set_tty(0);
    anim_set_listener(NULL, NULL);
}

static int parse_port(const char *s, int fallback) {
    if (!s || !*s) return fallback;
    char *end = NULL;
    long n = strtol(s, &end, 10);
    if (end == s || n < 1 || n > 65535) return fallback;
    return (int)n;
}

static void print_serve_usage(void) {
    printf("Usage: gs serve [options]\n\n");
    printf("Start the GitSight web console.\n\n");
    printf("Options:\n");
    printf("  --host <addr>      Address to bind (default %s, loopback only)\n", GS_SERVER_DEFAULT_HOST);
    printf("  --port <n>         Port to listen on (default %d)\n", GS_SERVER_DEFAULT_PORT);
    printf("  --web-root <dir>   Directory holding the built console\n");
    printf("                     (default: <exe dir>/web, then ./web)\n");
    printf("  --config <path>    Config file to use (default: %s)\n",
           "~/.config/gitsight/config.json");
    printf("  --jobs <n>         Concurrent analysis jobs (default 2)\n");
    printf("  --quiet, -q        Suppress the startup banner\n");
    printf("  --help, -h         Show this help\n\n");
    printf("Environment:\n");
    printf("  GS_PORT, GS_HOST        Same as --port / --host\n");
    printf("  GITSIGHT_WEB_ROOT      Same as --web-root\n");
    printf("  GS_FIXTURE_DIR         Serve offline fixtures instead of the network\n\n");
    printf("The daemon binds to loopback by default and has no authentication.\n");
    printf("Only pass --host 0.0.0.0 on a network you trust.\n");
}

/* Picks the built console from: an explicit flag, the environment, next to the
 * binary, then ./web. Keeps the launch command free of paths. */
static void resolve_web_root(char *out, size_t len, const char *explicit_root) {
    /* Truncate rather than overflow: a long override should degrade to a
     * possibly-unusable path, not corrupt the stack. */
    if (explicit_root && *explicit_root) {
        snprintf(out, len, "%.*s", (int)len - 1, explicit_root);
        return;
    }
    const char *env = getenv("GITSIGHT_WEB_ROOT");
    if (env && *env) {
        snprintf(out, len, "%.*s", (int)len - 1, env);
        return;
    }
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            char cand[1100], probe[1200];
            snprintf(cand, sizeof(cand), "%s/web", exe);
            snprintf(probe, sizeof(probe), "%s/index.html", cand);
            if (access(probe, R_OK) == 0) {
                snprintf(out, len, "%.*s", (int)len - 1, cand);
                return;
            }
        }
    }
    snprintf(out, len, "web");
}

int cmd_serve(int argc, char **argv) {
    ServeOptions so;
    memset(&so, 0, sizeof(so));
    snprintf(so.host, sizeof(so.host), "%s", GS_SERVER_DEFAULT_HOST);
    so.port = GS_SERVER_DEFAULT_PORT;
    so.threads = 2;

    const char *env_host = getenv("GS_HOST");
    if (env_host && *env_host) snprintf(so.host, sizeof(so.host), "%s", env_host);
    const char *env_port = getenv("GS_PORT");
    if (env_port && *env_port) so.port = parse_port(env_port, so.port);

    const char *web_root = NULL;

    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--host") && i + 1 < argc) {
            snprintf(so.host, sizeof(so.host), "%s", argv[++i]);
        } else if (!strcmp(a, "--port") && i + 1 < argc) {
            so.port = parse_port(argv[++i], so.port);
        } else if (!strcmp(a, "--web-root") && i + 1 < argc) {
            web_root = argv[++i];
        } else if (!strcmp(a, "--config") && i + 1 < argc) {
            snprintf(so.config_path, sizeof(so.config_path), "%s", argv[++i]);
        } else if (!strcmp(a, "--jobs") && i + 1 < argc) {
            so.threads = parse_port(argv[++i], so.threads);
            if (so.threads > GS_JOBS_MAX) so.threads = GS_JOBS_MAX;
        } else if (!strcmp(a, "--quiet") || !strcmp(a, "-q")) {
            so.quiet = 1;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            print_serve_usage();
            return 0;
        } else {
            fprintf(stderr, "Error: unknown option for serve: %s\n", a);
            print_serve_usage();
            return 1;
        }
    }

    if (so.config_path[0]) {
        snprintf(g_server.config_path, sizeof(g_server.config_path), "%s", so.config_path);
    } else {
        config_get_default_path(g_server.config_path, sizeof(g_server.config_path));
    }
    usage_path_from_config(g_server.config_path, g_server.usage_path, sizeof(g_server.usage_path));
    usage_set_file(g_server.usage_path);

    resolve_web_root(g_server.web_root, sizeof(g_server.web_root), web_root);
    g_server.web_root_set = 1;
    g_server.started_at = (long)time(NULL);
    snprintf(g_server.opts.host, sizeof(g_server.opts.host), "%s", so.host);
    g_server.opts.port = so.port;

    server_init_presentation();

    Config cfg;
    server_load_config(&cfg);
    gs_set_lang_str(cfg.lang);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    jobs_start(so.threads);

    if (!so.quiet) printf("GitSight %s web console\n", GS_VERSION);
    int rc = server_run(&so);

    jobs_shutdown();
    curl_global_cleanup();
    return rc;
}