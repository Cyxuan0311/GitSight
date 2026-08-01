#ifndef GITSIGHT_SPINNER_H
#define GITSIGHT_SPINNER_H

#include <pthread.h>

typedef struct {
    pthread_t thread;
    volatile int running;
    char message[256];
    int current;
    int total;
} Spinner;

void spinner_start(Spinner *sp, int current, int total, const char *message);
void spinner_stop(Spinner *sp, int success);
void spinner_set_quiet(int quiet);

#endif
