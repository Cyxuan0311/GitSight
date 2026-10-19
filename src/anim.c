#define _DEFAULT_SOURCE

#include "gitsight/anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BAR_WIDTH 24

static const char *const frames_line[]     = { "|", "/", "-", "\\" };
static const char *const frames_dots[]     = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" };
static const char *const frames_braille[]  = { "⣾", "⣽", "⣻", "⢿", "⡿", "⣟", "⣯", "⣷" };
static const char *const frames_bounce[]   = { "⠁", "⠂", "⠄", "⠂" };
static const char *const frames_unicode[]  = { "◐", "◓", "◑", "◒" };
static const char *const frames_triangle[] = { "◢", "◣", "◤", "◥" };
static const char *const frames_points[]   = { "▖", "▘", "▝", "▗" };

static int g_quiet = 0;
static int g_tty_override = -1;
static AnimListener g_listener = NULL;
static void *g_listener_user = NULL;

static int stderr_is_tty(void) {
    if (g_tty_override >= 0) return g_tty_override;
    return isatty(STDERR_FILENO);
}

static int no_color_env(void) {
    const char *nc = getenv("NO_COLOR");
    return nc != NULL && nc[0] != '\0';
}

void anim_set_quiet(int quiet) {
    g_quiet = quiet;
}

void anim_set_tty(int enabled) {
    g_tty_override = enabled;
}

void anim_set_listener(AnimListener fn, void *user) {
    g_listener = fn;
    g_listener_user = user;
}

void anim_emit(const char *stage, int current, int total, int state) {
    if (g_listener) g_listener(stage ? stage : "", current, total, state, g_listener_user);
}

static const char *current_glyph(Anim *a) {
    switch (a->style) {
        case ANIM_LINE:
            return frames_line[a->frame % (int)(sizeof frames_line / sizeof frames_line[0])];
        case ANIM_DOTS:
            return frames_dots[a->frame % (int)(sizeof frames_dots / sizeof frames_dots[0])];
        case ANIM_BRAILLE:
            return frames_braille[a->frame % (int)(sizeof frames_braille / sizeof frames_braille[0])];
        case ANIM_BOUNCE:
            return frames_bounce[a->frame % (int)(sizeof frames_bounce / sizeof frames_bounce[0])];
        case ANIM_UNICODE:
            return frames_unicode[a->frame % (int)(sizeof frames_unicode / sizeof frames_unicode[0])];
        case ANIM_TRIANGLE:
            return frames_triangle[a->frame % (int)(sizeof frames_triangle / sizeof frames_triangle[0])];
        case ANIM_POINTS:
            return frames_points[a->frame % (int)(sizeof frames_points / sizeof frames_points[0])];
        case ANIM_BAR:
        default:
            return "";
    }
}

static void anim_render(Anim *a) {
    if (a->style == ANIM_BAR) {
        if (a->total > 0) {
            long long pct = (long long)a->current * 100 / a->total;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            int filled = (int)(pct * BAR_WIDTH / 100);
            fprintf(stderr, "\r[*] %s [", a->message);
            for (int i = 0; i < BAR_WIDTH; i++)
                fputs(i < filled ? "█" : "░", stderr);
            fprintf(stderr, "] %3lld%% (%d/%d)", pct, a->current, a->total);
        } else {
            int pos = a->frame % BAR_WIDTH;
            fprintf(stderr, "\r[*] %s [", a->message);
            for (int i = 0; i < BAR_WIDTH; i++)
                fputs(i == pos ? "█" : "░", stderr);
            fprintf(stderr, "]");
        }
        return;
    }

    const char *glyph = current_glyph(a);
    if (a->total > 0)
        fprintf(stderr, "\r[*] (%d/%d) %s [%s]", a->current, a->total, a->message, glyph);
    else
        fprintf(stderr, "\r[*] %s [%s]", a->message, glyph);
}

static void *anim_run(void *arg) {
    Anim *a = (Anim *)arg;
    while (a->running) {
        anim_render(a);
        fflush(stderr);
        usleep(100000);
        a->frame++;
    }
    return NULL;
}

void anim_start(Anim *a, AnimStyle style, int current, int total, const char *message) {
    a->style = style;
    a->current = current;
    a->total = total;
    a->frame = 0;
    a->thread_started = 0;
    a->running = 1;
    snprintf(a->message, sizeof(a->message), "%s", message);

    anim_emit(a->message, current, total, ANIM_PROG_START);

    if (!g_quiet && stderr_is_tty()) {
        if (pthread_create(&a->thread, NULL, anim_run, a) == 0) {
            a->thread_started = 1;
        } else {
            a->running = 0;
            if (total > 0)
                fprintf(stderr, "[*] (%d/%d) %s... ", current, total, message);
            else
                fprintf(stderr, "[*] %s... ", message);
            fflush(stderr);
        }
    } else if (!g_quiet) {
        if (total > 0)
            fprintf(stderr, "[*] (%d/%d) %s... ", current, total, message);
        else
            fprintf(stderr, "[*] %s... ", message);
        fflush(stderr);
    }
}

void anim_update(Anim *a, int current, int total) {
    a->current = current;
    if (total >= 0) a->total = total;
    anim_emit(a->message, a->current, a->total, ANIM_PROG_UPDATE);
}

void anim_update_message(Anim *a, const char *message) {
    snprintf(a->message, sizeof(a->message), "%s", message);
    anim_emit(a->message, a->current, a->total, ANIM_PROG_UPDATE);
}

void anim_stop(Anim *a, int success) {
    anim_emit(a->message, a->current, a->total,
              success ? ANIM_PROG_OK : ANIM_PROG_FAIL);
    if (a->running) {
        a->running = 0;
        if (a->thread_started) {
            pthread_join(a->thread, NULL);
            a->thread_started = 0;
        }
    }
    if (!g_quiet) {
        if (stderr_is_tty()) {
            const char *status = success ? "OK" : "FAIL";
            const char *on = "", *off = "";
            if (!no_color_env()) {
                on = success ? "\033[32m" : "\033[31m";
                off = "\033[0m";
            }
            fprintf(stderr, "\r[*] ");
            if (a->total > 0)
                fprintf(stderr, "(%d/%d) ", a->current, a->total);
            fprintf(stderr, "%s [%s%s%s]\n", a->message, on, status, off);
        } else {
            fprintf(stderr, "%s\n", success ? "OK" : "FAIL");
        }
    }
    fflush(stderr);
}
