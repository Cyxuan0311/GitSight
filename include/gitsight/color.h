#ifndef GITSIGHT_COLOR_H
#define GITSIGHT_COLOR_H

#include <stdio.h>
#include <stdint.h>

typedef enum {
    COLOR_AUTO = 0,
    COLOR_ALWAYS,
    COLOR_NEVER
} ColorMode;

#define CLR_BOLD    "\x1b[1m"
#define CLR_RED     "\x1b[31m"
#define CLR_GREEN   "\x1b[32m"
#define CLR_YELLOW  "\x1b[33m"
#define CLR_BLUE    "\x1b[34m"
#define CLR_CYAN    "\x1b[36m"
#define CLR_GRAY    "\x1b[90m"
#define CLR_RESET   "\x1b[0m"

void color_init(ColorMode mode);
int  color_active(void);

int color_printf(const char *fmt, ...);
int color_printf_color(const char *color, const char *fmt, ...);

#endif
