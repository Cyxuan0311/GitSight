#ifndef GITSIGHT_TUI_H
#define GITSIGHT_TUI_H

#include <stdio.h>

#define TUI_MAX_COLS 16
#define TUI_MAX_ROWS 256

typedef enum {
    TUI_ALIGN_LEFT = 0,
    TUI_ALIGN_RIGHT = 1,
    TUI_ALIGN_CENTER = 2
} TuiAlign;

typedef enum {
    TUI_BORDER_NONE = 0,
    TUI_BORDER_ASCII = 1,
    TUI_BORDER_ROUNDED = 2
} TuiBorder;

typedef struct {
    int          ncols;
    const char  *headers[TUI_MAX_COLS];
    const char  *header_colors[TUI_MAX_COLS];
    const char  *cell_colors[TUI_MAX_COLS];
    TuiAlign     aligns[TUI_MAX_COLS];
    int          widths[TUI_MAX_COLS];
    const char  *rows[TUI_MAX_ROWS][TUI_MAX_COLS];
    int          nrows;
    TuiBorder    border;
    int          use_color;
    int          zebra;
    int          truncate;
    int          max_width;
} TuiTable;

void tui_table_init(TuiTable *t, int ncols);
void tui_table_header(TuiTable *t, int col, const char *header);
void tui_table_align(TuiTable *t, int col, TuiAlign align);
void tui_table_header_color(TuiTable *t, int col, const char *ansi);
void tui_table_cell_color(TuiTable *t, int col, const char *ansi);
void tui_table_row(TuiTable *t, const char *const *cells);
void tui_table_render(const TuiTable *t, FILE *out);

#endif
