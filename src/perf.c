#include "gitsight/perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * The phase timer.
 *
 * The marks are collected in a fixed table rather than allocated, so a run
 * never pays for the instrumentation and the reporting cannot fail.
 */
struct PerfPhase {
    const char *name;
    double      ms;
};

/* Thread-local, so a daemon that runs several analyses at once reports each
 * one on its own thread instead of interleaving them into one table. */
static _Thread_local struct PerfPhase g_phases[PERF_MAX_PHASES];
static _Thread_local int              g_phase_count;
static _Thread_local struct timespec  g_start;
static _Thread_local struct timespec  g_last;
static int                           g_enabled;

static double since_ms(const struct timespec *from, const struct timespec *to) {
    return (double)(to->tv_sec - from->tv_sec) * 1000.0 +
           (double)(to->tv_nsec - from->tv_nsec) / 1e6;
}

int perf_enabled(void) {
    return g_enabled;
}

void perf_start(void) {
    const char *flag = getenv("GS_PERF");
    g_enabled = flag && flag[0] && strcmp(flag, "0") != 0;
    g_phase_count = 0;
    if (!g_enabled) return;
    clock_gettime(CLOCK_MONOTONIC, &g_start);
    g_last = g_start;
}

void perf_mark(const char *name) {
    if (!g_enabled || !name) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (g_phase_count >= PERF_MAX_PHASES) return;
    g_phases[g_phase_count].name = name;
    g_phases[g_phase_count].ms = since_ms(&g_last, &now);
    g_phase_count++;
    g_last = now;
}

void perf_report(void) {
    if (!g_enabled) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    fprintf(stderr, "\n--- timings ---\n");
    for (int i = 0; i < g_phase_count; i++)
        fprintf(stderr, "  %-24s %8.2f ms\n", g_phases[i].name, g_phases[i].ms);
    fprintf(stderr, "  %-24s %8.2f ms\n", "total", since_ms(&g_start, &now));
    fprintf(stderr, "----------------\n");
}
