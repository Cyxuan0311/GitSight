#ifndef GITSIGHT_ANIM_H
#define GITSIGHT_ANIM_H

#include <pthread.h>

/* Built-in animation styles. All glyph frames are single-width characters;
 * pick the style that fits the terminal and the mood of the operation. */
typedef enum {
    ANIM_LINE,     /* classic ASCII: | / - \            */
    ANIM_DOTS,     /* braille dots:  ⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏  */
    ANIM_BRAILLE,  /* braille fill:  ⣾⣽⣻⢿⡿⣟⣯⣷   */
    ANIM_BOUNCE,   /* bouncing dot:  ⠁⠂⠄⠂          */
    ANIM_UNICODE,  /* geometric:     ◐◓◑◒             */
    ANIM_TRIANGLE, /* triangles:     ◢◣◤◥             */
    ANIM_POINTS,   /* quadrants:     ▖▘▝▗             */
    ANIM_BAR,      /* progress bar:  [███░░░] 45% (1/6) */
} AnimStyle;

typedef struct {
    pthread_t thread;
    volatile int running;
    int thread_started;
    char message[256];
    volatile int current;
    volatile int total;
    AnimStyle style;
    int frame;
} Anim;

/* Start an animation on a background thread writing to stderr.
 * total > 0 is determinate (shows current/total and a bar for ANIM_BAR);
 * total == 0 is indeterminate (glyphs spin forever). */
void anim_start(Anim *a, AnimStyle style, int current, int total, const char *message);

/* Update progress while running (picked up on the next frame). */
void anim_update(Anim *a, int current, int total);
void anim_update_message(Anim *a, const char *message);

/* Stop the animation and print OK/FAIL. Safe to call after anim_start even
 * if the thread never spawned (non-tty or quiet). */
void anim_stop(Anim *a, int success);

/* Global control. anim_set_quiet(1) suppresses all output; anim_set_tty()
 * overrides auto stderr tty detection (use -1 to restore auto). */
void anim_set_quiet(int quiet);
void anim_set_tty(int enabled);

/* Progress listener. Every stage transition and tick already passes through
 * anim_start/anim_update/anim_stop, so installing a listener here turns the
 * animation into a structured progress stream with no extra instrumentation
 * on the analysis path. The CLI leaves the listener unset.
 *
 *   state: ANIM_PROG_START | ANIM_PROG_UPDATE | ANIM_PROG_OK | ANIM_PROG_FAIL
 *   total <= 0 means an indeterminate stage (no fraction available).
 *
 * Called from the caller's thread, so a listener may take its own lock. */
enum {
    ANIM_PROG_START = 0,
    ANIM_PROG_UPDATE = 1,
    ANIM_PROG_OK = 2,
    ANIM_PROG_FAIL = 3
};
typedef void (*AnimListener)(const char *stage, int current, int total,
                             int state, void *user);
void anim_set_listener(AnimListener fn, void *user);
void anim_emit(const char *stage, int current, int total, int state);

#endif
