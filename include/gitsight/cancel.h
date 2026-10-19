#ifndef GITSIGHT_CANCEL_H
#define GITSIGHT_CANCEL_H

#include <stdatomic.h>

/* Cooperative cancellation for long-running work.
 *
 * A request must be cancellable per unit of work, not per process: the server
 * runs several repositories in parallel threads, so aborting a single job must
 * not abort its neighbours. Each worker thread binds the flag it owns with
 * gs_cancel_bind() and every cancel call then resolves against that binding.
 *
 * curl invokes the progress callback on the thread that called
 * curl_easy_perform, so a thread-local binding is visible to the in-flight
 * transfer and no extra plumbing reaches the transfer layer. Code that never
 * binds (the CLI) falls back to one process-global flag.
 *
 * gs_cancel_bind(NULL) restores the global flag. */

extern atomic_int gs_cancel_global;

void gs_cancel_bind(const atomic_int *flag);
const atomic_int *gs_cancel_current(void);

void gs_cancel_reset(void);      /* clears the bound (or global) flag */
void gs_cancel_request(void);    /* sets the bound (or global) flag */
int  gs_cancel_requested(void);  /* reads the bound (or global) flag */

/* Install an XFERINFOFUNCTION on `easy` (a CURL *) that aborts the transfer
 * with CURLE_ABORTED_BY_CALLBACK once cancellation is requested. Call right
 * after curl_easy_init. No-op when easy is NULL. */
struct CURL;
void gs_cancel_attach(void *easy);

#endif