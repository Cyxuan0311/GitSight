#include "gitsight/spinner.h"
#include "gitsight/color.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static void *spinner_run(void *arg) {
    Spinner *sp = (Spinner *)arg;
    const char chars[] = {'|', '/', '-', '\\'};
    int i = 0;
    while (sp->running) {
        fprintf(stderr, "\r[*] (%d/%d) %s [%c]", sp->current, sp->total, sp->message, chars[i % 4]);
        fflush(stderr);
        usleep(120000);
        i++;
    }
    return NULL;
}

static int g_quiet = 0;

void spinner_set_quiet(int quiet) {
    g_quiet = quiet;
}

void spinner_start(Spinner *sp, int current, int total, const char *message) {
    sp->running = 1;
    sp->current = current;
    sp->total = total;
    snprintf(sp->message, sizeof(sp->message), "%s", message);

    if (spinner_active() && !g_quiet) {
        pthread_create(&sp->thread, NULL, spinner_run, sp);
    } else if (!g_quiet) {
        fprintf(stderr, "[*] (%d/%d) %s... ", current, total, message);
        fflush(stderr);
    }
}

void spinner_stop(Spinner *sp, int success) {
    if (spinner_active() && !g_quiet) {
        sp->running = 0;
        pthread_join(sp->thread, NULL);
        const char *status = success ? CLR_GREEN "OK" CLR_RESET : CLR_RED "FAIL" CLR_RESET;
        fprintf(stderr, "\r[*] (%d/%d) %s [%s]\n", sp->current, sp->total, sp->message, status);
    } else {
        if (!g_quiet) {
            fprintf(stderr, "%s\n", success ? "OK" : "FAIL");
        }
    }
    fflush(stderr);
}
