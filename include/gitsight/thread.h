#ifndef GITSIGHT_THREAD_H
#define GITSIGHT_THREAD_H

#include <pthread.h>

/* macOS hands a thread created with default attributes 512 KiB of stack, while
 * Linux gives 8 MiB. The analysis frames carry a few ~64 KiB Config copies and
 * the cache prune list is ~500 KiB, so the macOS default overflows the stack
 * and the process dies with SIGBUS. Ask for the Linux size on every platform so
 * the two behave the same. The size is only reserved (virtual) memory. */
#define GS_THREAD_STACK_BYTES (8u * 1024u * 1024u)

static inline int gs_thread_create(pthread_t *thread,
                                   void *(*start_routine)(void *),
                                   void *arg) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return -1;
    pthread_attr_setstacksize(&attr, GS_THREAD_STACK_BYTES);
    int rc = pthread_create(thread, &attr, start_routine, arg);
    pthread_attr_destroy(&attr);
    return rc;
}

#endif /* GITSIGHT_THREAD_H */
