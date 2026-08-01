#include "gitsight/color.h"
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static ColorMode g_color_mode = COLOR_AUTO;
static int g_stdout_is_tty = 1;
static int g_no_color_env = 0;

void color_init(ColorMode mode) {
    g_color_mode = mode;
    g_stdout_is_tty = isatty(STDOUT_FILENO);
    const char *no_color = getenv("NO_COLOR");
    g_no_color_env = (no_color != NULL && no_color[0] != '\0');
}

static int should_color(void) {
    if (g_color_mode == COLOR_ALWAYS) return 1;
    if (g_color_mode == COLOR_NEVER)  return 0;
    return g_stdout_is_tty && !g_no_color_env;
}

int color_active(void) {
    return should_color();
}

int color_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vfprintf(stdout, fmt, args);
    va_end(args);
    return ret;
}

int color_printf_color(const char *color, const char *fmt, ...) {
    if (should_color()) {
        fputs(color, stdout);
    }
    va_list args;
    va_start(args, fmt);
    int ret = vfprintf(stdout, fmt, args);
    va_end(args);
    if (should_color()) {
        fputs(CLR_RESET, stdout);
    }
    return ret;
}
