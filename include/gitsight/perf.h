#ifndef GIT_SIGHT_PERF_H
#define GIT_SIGHT_PERF_H

#include <stddef.h>

/**
 * Phase timing for a run.
 *
 * A run has a few phases that fail in very different ways: the fetch waits on
 * the network, the analysis walks every item, and the export serialises the
 * result. Without a breakdown, "it is slow" has no useful answer, so the
 * phases are timed and printed when GS_PERF is set.
 *
 * The timer is deliberately cheap: one clock_gettime per mark, and the
 * printing happens once at the end.
 */
#define PERF_MAX_PHASES 24

/** Starts the clock. Called once at the start of a run. */
void perf_start(void);

/**
 * Records the time since the previous mark under this name. The first mark
 * after perf_start() measures the time since the run began.
 */
void perf_mark(const char *name);

/** Prints the breakdown to stderr, if the timer is enabled. */
void perf_report(void);

/** Whether the timer is on; the marks are skipped entirely when it is not. */
int perf_enabled(void);

#endif
