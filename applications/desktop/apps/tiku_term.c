/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_term.c - a terminal, as one application descriptor.
 *
 * A shell on a pty writes into a grid of cells, each carrying a
 * character and how it is drawn.  The parser is a VT100 subset: enough
 * for a line editor, a pager and a full-screen editor to behave.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tiku_desk_app.h"
#include "tiku_desk_client.h"
#include "tiku_desk_font.h"
#include "tiku_desk_gfx.h"

#define COLS      80
#define ROWS      24
#define SCROLLBACK 500
#define STRIP_H   20
#define MARGIN    4
#define PARAMS_MAX 8

#define ATTR_BOLD    0x01u
#define ATTR_REVERSE 0x02u

#define CMD_CLEAR 1
#define CMD_RESET 2
#define CMD_QUIT  3

typedef struct {
    char          ch;
    unsigned char fg;           /* 0-7 ANSI, 8 = the default ink   */
    unsigned char bg;           /* 0-7 ANSI, 8 = the default paper */
    unsigned char attr;
} cell_t;

typedef enum {
    SCAN_TEXT = 0,
    SCAN_ESC,
    SCAN_CSI,
    SCAN_OSC,
    SCAN_SKIP                   /* one byte of a two-byte sequence */
} scan_t;

typedef struct {
    const tiku_desk_app_services_t *services;
    tiku_desk_surface_t            *surface;
    uint32_t                        id;
    int                             master;
    pid_t                           child;

    cell_t                          cell[ROWS][COLS];
    cell_t                          back[SCROLLBACK][COLS];
    int                             back_count;
    int                             back_head;  /* oldest row       */
    int                             view;       /* rows scrolled up */

    int                             row, col;
    int                             save_row, save_col;
    int                             top, bottom;    /* scroll region */
    unsigned char                   fg, bg, attr;
    int                             wrap_next;
    int                             cursor_on;

    scan_t                          scan;
    int                             param[PARAMS_MAX];
    int                             nparam;
    int                             question;       /* CSI ? ...    */

    int                             alive;
    int                             dirty;
    int                             cell_w, cell_h;
} term_state_t;

/* The ANSI eight, then the eight bright ones the bold attribute picks. */
static const tiku_desk_rgb_t kAnsi[8] = {
    TIKU_DESK_RGB(0, 0, 0),       TIKU_DESK_RGB(178, 24, 24),
    TIKU_DESK_RGB(24, 154, 24),   TIKU_DESK_RGB(160, 132, 0),
    TIKU_DESK_RGB(40, 90, 190),   TIKU_DESK_RGB(160, 60, 170),
    TIKU_DESK_RGB(0, 150, 160),   TIKU_DESK_RGB(190, 190, 190)
};
static const tiku_desk_rgb_t kBright[8] = {
    TIKU_DESK_RGB(110, 110, 110), TIKU_DESK_RGB(230, 60, 60),
    TIKU_DESK_RGB(50, 200, 50),   TIKU_DESK_RGB(210, 180, 20),
    TIKU_DESK_RGB(80, 140, 240),  TIKU_DESK_RGB(210, 100, 220),
    TIKU_DESK_RGB(40, 200, 210),  TIKU_DESK_RGB(255, 255, 255)
};

static int
width_px(const term_state_t *st)
{
    return COLS * st->cell_w + 2 * MARGIN;
}

static int
height_px(const term_state_t *st)
{
    return ROWS * st->cell_h + 2 * MARGIN + STRIP_H;
}

static cell_t
blank_of(const term_state_t *st)
{
    cell_t c;

    c.ch = ' ';
    c.fg = st->fg;
    c.bg = st->bg;
    c.attr = 0u;
    return c;
}

static void
row_blank(term_state_t *st, int r)
{
    int c;

    for (c = 0; c < COLS; c++) {
        st->cell[r][c] = blank_of(st);
    }
}

static void
grid_clear(term_state_t *st)
{
    int r;

    for (r = 0; r < ROWS; r++) {
        row_blank(st, r);
    }
    st->row = 0;
    st->col = 0;
    st->wrap_next = 0;
    st->dirty = 1;
}

/** @brief Keep the row leaving the top, so it can be scrolled back to. */
static void
remember(term_state_t *st, const cell_t *row)
{
    int slot = (st->back_head + st->back_count) % SCROLLBACK;

    memcpy(st->back[slot], row, sizeof st->back[0]);
    if (st->back_count < SCROLLBACK) {
        st->back_count++;
    } else {
        st->back_head = (st->back_head + 1) % SCROLLBACK;
    }
}

static void
scroll_region_up(term_state_t *st)
{
    int r;

    /* Only a scroll of the WHOLE screen is history; one inside a region
     * belongs to whatever drew the region and is not the user's past. */
    if (st->top == 0 && st->bottom == ROWS - 1) {
        remember(st, st->cell[0]);
    }
    for (r = st->top; r < st->bottom; r++) {
        memcpy(st->cell[r], st->cell[r + 1], sizeof st->cell[0]);
    }
    row_blank(st, st->bottom);
}

static void
scroll_region_down(term_state_t *st)
{
    int r;

    for (r = st->bottom; r > st->top; r--) {
        memcpy(st->cell[r], st->cell[r - 1], sizeof st->cell[0]);
    }
    row_blank(st, st->top);
}

static void
newline(term_state_t *st)
{
    st->wrap_next = 0;
    if (st->row == st->bottom) {
        scroll_region_up(st);
    } else if (st->row + 1 < ROWS) {
        st->row++;
    }
}

static void
put_printable(term_state_t *st, char ch)
{
    cell_t *c;

    if (st->wrap_next) {
        st->col = 0;
        newline(st);
    }
    c = &st->cell[st->row][st->col];
    c->ch = ch;
    c->fg = st->fg;
    c->bg = st->bg;
    c->attr = st->attr;
    if (st->col + 1 >= COLS) {
        /* Held at the last column: a character there does not wrap until
         * the NEXT one arrives, which is what stops a line that exactly
         * fills the width from leaving a blank row behind it. */
        st->wrap_next = 1;
    } else {
        st->col++;
    }
}

static int
param_or(const term_state_t *st, int i, int fallback)
{
    if (i >= st->nparam || st->param[i] < 0) {
        return fallback;
    }
    return st->param[i];
}

static void
erase_cells(term_state_t *st, int r, int from, int to)
{
    int c;

    for (c = from; c <= to && c < COLS; c++) {
        st->cell[r][c] = blank_of(st);
    }
}

static void
select_graphics(term_state_t *st)
{
    int i;

    if (st->nparam == 0) {
        st->fg = 8u;
        st->bg = 8u;
        st->attr = 0u;
        return;
    }
    for (i = 0; i < st->nparam; i++) {
        int p = param_or(st, i, 0);

        if (p == 0) {
            st->fg = 8u;
            st->bg = 8u;
            st->attr = 0u;
        } else if (p == 1) {
            st->attr |= ATTR_BOLD;
        } else if (p == 7) {
            st->attr |= ATTR_REVERSE;
        } else if (p == 22) {
            st->attr &= ~ATTR_BOLD;
        } else if (p == 27) {
            st->attr &= ~ATTR_REVERSE;
        } else if (p >= 30 && p <= 37) {
            st->fg = (unsigned char)(p - 30);
        } else if (p == 39) {
            st->fg = 8u;
        } else if (p >= 40 && p <= 47) {
            st->bg = (unsigned char)(p - 40);
        } else if (p == 49) {
            st->bg = 8u;
        } else if (p >= 90 && p <= 97) {
            st->fg = (unsigned char)(p - 90);
            st->attr |= ATTR_BOLD;
        } else if (p >= 100 && p <= 107) {
            st->bg = (unsigned char)(p - 100);
        }
    }
}

/** @brief Act on a complete control sequence. */
static void
csi_dispatch(term_state_t *st, char final)
{
    int n = param_or(st, 0, 1);
    int r, c;

    switch (final) {
    case 'A':
        st->row -= (n < 1) ? 1 : n;
        break;
    case 'B':
        st->row += (n < 1) ? 1 : n;
        break;
    case 'C':
        st->col += (n < 1) ? 1 : n;
        break;
    case 'D':
        st->col -= (n < 1) ? 1 : n;
        break;
    case 'E':
        st->row += (n < 1) ? 1 : n;
        st->col = 0;
        break;
    case 'F':
        st->row -= (n < 1) ? 1 : n;
        st->col = 0;
        break;
    case 'G':
        st->col = n - 1;
        break;
    case 'd':
        st->row = n - 1;
        break;
    case 'H':
    case 'f':
        st->row = param_or(st, 0, 1) - 1;
        st->col = param_or(st, 1, 1) - 1;
        break;
    case 'J':
        n = param_or(st, 0, 0);
        if (n == 0) {
            erase_cells(st, st->row, st->col, COLS - 1);
            for (r = st->row + 1; r < ROWS; r++) {
                row_blank(st, r);
            }
        } else if (n == 1) {
            erase_cells(st, st->row, 0, st->col);
            for (r = 0; r < st->row; r++) {
                row_blank(st, r);
            }
        } else {
            for (r = 0; r < ROWS; r++) {
                row_blank(st, r);
            }
        }
        break;
    case 'K':
        n = param_or(st, 0, 0);
        if (n == 0) {
            erase_cells(st, st->row, st->col, COLS - 1);
        } else if (n == 1) {
            erase_cells(st, st->row, 0, st->col);
        } else {
            row_blank(st, st->row);
        }
        break;
    case 'L':
        for (r = 0; r < n; r++) {
            int keep = st->top;

            st->top = st->row;
            scroll_region_down(st);
            st->top = keep;
        }
        break;
    case 'M':
        for (r = 0; r < n; r++) {
            int keep = st->top;

            st->top = st->row;
            scroll_region_up(st);
            st->top = keep;
        }
        break;
    case 'P':
        for (c = st->col; c < COLS; c++) {
            st->cell[st->row][c] = (c + n < COLS)
                                       ? st->cell[st->row][c + n]
                                       : blank_of(st);
        }
        break;
    case '@':
        for (c = COLS - 1; c >= st->col; c--) {
            st->cell[st->row][c] = (c - n >= st->col)
                                       ? st->cell[st->row][c - n]
                                       : blank_of(st);
        }
        break;
    case 'S':
        for (r = 0; r < n; r++) {
            scroll_region_up(st);
        }
        break;
    case 'T':
        for (r = 0; r < n; r++) {
            scroll_region_down(st);
        }
        break;
    case 'm':
        select_graphics(st);
        break;
    case 'r':
        st->top = param_or(st, 0, 1) - 1;
        st->bottom = param_or(st, 1, ROWS) - 1;
        if (st->top < 0) {
            st->top = 0;
        }
        if (st->bottom >= ROWS) {
            st->bottom = ROWS - 1;
        }
        if (st->top >= st->bottom) {
            st->top = 0;
            st->bottom = ROWS - 1;
        }
        st->row = st->top;
        st->col = 0;
        break;
    case 'h':
    case 'l':
        if (st->question && param_or(st, 0, 0) == 25) {
            st->cursor_on = (final == 'h');
        }
        break;
    case 's':
        st->save_row = st->row;
        st->save_col = st->col;
        break;
    case 'u':
        st->row = st->save_row;
        st->col = st->save_col;
        break;
    default:
        break;                  /* a sequence this build does not know */
    }
    if (st->row < 0) {
        st->row = 0;
    }
    if (st->row >= ROWS) {
        st->row = ROWS - 1;
    }
    if (st->col < 0) {
        st->col = 0;
    }
    if (st->col >= COLS) {
        st->col = COLS - 1;
    }
    st->wrap_next = 0;
}

/** @brief Feed the parser one byte of the shell's output. */
static void
feed(term_state_t *st, unsigned char ch)
{
    switch (st->scan) {
    case SCAN_ESC:
        if (ch == '[') {
            st->scan = SCAN_CSI;
            st->nparam = 0;
            st->question = 0;
            memset(st->param, 0, sizeof st->param);
            st->param[0] = -1;
            return;
        }
        if (ch == ']') {
            st->scan = SCAN_OSC;
            return;
        }
        if (ch == '(' || ch == ')' || ch == '#' || ch == '%') {
            st->scan = SCAN_SKIP;   /* a charset select: not ours */
            return;
        }
        if (ch == 'M') {
            /* Reverse index: the one cursor motion that arrives without
             * a bracket, and a pager scrolling backwards uses it. */
            if (st->row == st->top) {
                scroll_region_down(st);
            } else if (st->row > 0) {
                st->row--;
            }
        } else if (ch == 'c') {
            grid_clear(st);
            st->fg = 8u;
            st->bg = 8u;
            st->attr = 0u;
        }
        st->scan = SCAN_TEXT;
        return;
    case SCAN_SKIP:
        st->scan = SCAN_TEXT;
        return;
    case SCAN_OSC:
        /* A window title, usually.  Ends at BEL, or at ESC and one more. */
        if (ch == 0x07) {
            st->scan = SCAN_TEXT;
        } else if (ch == 0x1b) {
            st->scan = SCAN_SKIP;
        }
        return;
    case SCAN_CSI:
        if (ch == '?') {
            st->question = 1;
            return;
        }
        if (ch >= '0' && ch <= '9') {
            if (st->nparam == 0) {
                st->nparam = 1;
                st->param[0] = 0;
            }
            if (st->param[st->nparam - 1] < 0) {
                st->param[st->nparam - 1] = 0;
            }
            st->param[st->nparam - 1] =
                st->param[st->nparam - 1] * 10 + (ch - '0');
            return;
        }
        if (ch == ';') {
            if (st->nparam == 0) {
                st->nparam = 1;
                st->param[0] = -1;
            }
            if (st->nparam < PARAMS_MAX) {
                st->param[st->nparam++] = -1;
            }
            return;
        }
        if (ch >= '@' && ch <= '~') {
            csi_dispatch(st, (char)ch);
            st->scan = SCAN_TEXT;
            return;
        }
        return;                 /* an intermediate byte: ignored */
    case SCAN_TEXT:
    default:
        break;
    }

    switch (ch) {
    case 0x1b:
        st->scan = SCAN_ESC;
        return;
    case '\n':
        newline(st);
        return;
    case '\r':
        st->col = 0;
        st->wrap_next = 0;
        return;
    case '\b':
        if (st->col > 0) {
            st->col--;
        }
        st->wrap_next = 0;
        return;
    case '\t':
        st->col = (st->col + 8) & ~7;
        if (st->col >= COLS) {
            st->col = COLS - 1;
        }
        return;
    case 0x07:
        return;                 /* the bell has nowhere to ring */
    default:
        break;
    }
    if (ch >= 32u && ch < 127u) {
        put_printable(st, (char)ch);
    }
}

static tiku_desk_rgb_t
ink_of(unsigned char index, unsigned char attr, int is_bg)
{
    if (index > 7u) {
        return is_bg ? TIKU_DESK_C_DOC : TIKU_DESK_C_TEXT;
    }
    return ((attr & ATTR_BOLD) != 0u && !is_bg) ? kBright[index]
                                                : kAnsi[index];
}

/** @brief The row shown at screen line @p r, live or scrolled back. */
static const cell_t *
row_at(const term_state_t *st, int r)
{
    int behind = st->view - r;  /* how far into history this line is */

    if (behind > 0) {
        int slot = (st->back_head + st->back_count - behind) % SCROLLBACK;

        return st->back[slot];
    }
    return st->cell[r - st->view];
}

static void
paint(term_state_t *st)
{
    const tiku_desk_font_t *plain = tiku_desk_font_mono(0);
    const tiku_desk_font_t *bold = tiku_desk_font_mono(1);
    const tiku_desk_font_t *small = tiku_desk_font_at(11);
    int w = width_px(st);
    int h = height_px(st);
    tiku_desk_rect_t page = { 0, 0, w, h - STRIP_H };
    tiku_desk_rect_t strip = { 0, h - STRIP_H, w, STRIP_H };
    char note[96];
    int r, c;

    tiku_desk_fill(st->surface, page, TIKU_DESK_C_DOC);
    for (r = 0; r < ROWS; r++) {
        const cell_t *line = row_at(st, r);
        int y = MARGIN + r * st->cell_h;

        for (c = 0; c < COLS; c++) {
            const cell_t *cell = &line[c];
            tiku_desk_rgb_t fg = ink_of(cell->fg, cell->attr, 0);
            tiku_desk_rgb_t bg = ink_of(cell->bg, cell->attr, 1);
            char one[2];

            if ((cell->attr & ATTR_REVERSE) != 0u) {
                tiku_desk_rgb_t swap = fg;

                fg = bg;
                bg = swap;
            }
            if (cell->bg <= 7u || (cell->attr & ATTR_REVERSE) != 0u) {
                tiku_desk_fill(st->surface,
                               (tiku_desk_rect_t){ MARGIN + c * st->cell_w,
                                                   y, st->cell_w,
                                                   st->cell_h }, bg);
            }
            if (cell->ch == ' ' || cell->ch == '\0') {
                continue;
            }
            one[0] = cell->ch;
            one[1] = '\0';
            tiku_desk_text(st->surface,
                           ((cell->attr & ATTR_BOLD) != 0u) ? bold : plain,
                           MARGIN + c * st->cell_w, y + plain->ascent, one,
                           fg);
        }
    }
    if (st->alive && st->cursor_on && st->view == 0) {
        /* A block where the next character will land, with whatever is
         * already there drawn back over it. */
        tiku_desk_rect_t at = { MARGIN + st->col * st->cell_w,
                                MARGIN + st->row * st->cell_h,
                                st->cell_w, st->cell_h };

        tiku_desk_fill(st->surface, at, TIKU_DESK_C_SELECT);
        if (st->cell[st->row][st->col].ch > ' ') {
            char one[2];

            one[0] = st->cell[st->row][st->col].ch;
            one[1] = '\0';
            tiku_desk_text(st->surface, plain, at.x, at.y + plain->ascent,
                           one, TIKU_DESK_C_SELTEXT);
        }
    }
    tiku_desk_fill(st->surface, strip, TIKU_DESK_C_PANEL);
    tiku_desk_hline(st->surface, 0, strip.y, w,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    if (!st->alive) {
        snprintf(note, sizeof note, "the shell has exited");
    } else if (st->view > 0) {
        snprintf(note, sizeof note, "%d line%s back  --  shift+page down "
                 "to return", st->view, st->view == 1 ? "" : "s");
    } else {
        snprintf(note, sizeof note, "%dx%d  vt100", COLS, ROWS);
    }
    tiku_desk_text(st->surface, small, MARGIN,
                   strip.y + (STRIP_H - small->height) / 2 + small->ascent,
                   note, TIKU_DESK_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->id, st->surface->px,
                              w, h);
    st->dirty = 0;
}

static void
publish(term_state_t *st)
{
    tiku_desk_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Terminal");
    menus.menu[0].nitem = 3;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Clear");
    menus.menu[0].item[0].command = CMD_CLEAR;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Reset");
    menus.menu[0].item[1].command = CMD_RESET;
    menus.menu[0].item[1].enabled = 1;
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "Quit");
    menus.menu[0].item[2].command = CMD_QUIT;
    menus.menu[0].item[2].enabled = 1;
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

/**
 * @brief Start a shell on a pty of its own.
 *
 * @note The child gets the pty as its controlling terminal, or nothing
 *       started from it would have job control.
 */
static int
start_shell(term_state_t *st)
{
    char slave_name[128];
    struct winsize ws;
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    pid_t kid;

    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
        ptsname_r(master, slave_name, sizeof slave_name) != 0) {
        return -1;
    }
    memset(&ws, 0, sizeof ws);
    ws.ws_col = COLS;
    ws.ws_row = ROWS;
    (void)ioctl(master, TIOCSWINSZ, &ws);

    kid = fork();
    if (kid < 0) {
        (void)close(master);
        return -1;
    }
    if (kid == 0) {
        const char *shell = getenv("SHELL");
        int slave;

        (void)close(master);
        (void)setsid();
        slave = open(slave_name, O_RDWR);
        if (slave < 0) {
            _exit(127);
        }
        (void)ioctl(slave, TIOCSCTTY, 0);
        (void)dup2(slave, 0);
        (void)dup2(slave, 1);
        (void)dup2(slave, 2);
        if (slave > 2) {
            (void)close(slave);
        }
        /* Claiming vt100 is a promise about the sequences handled above:
         * cursor addressing, erase, colour and a scroll region.  A
         * larger claim would invite sequences that arrive as rubbish. */
        (void)setenv("TERM", "vt100", 1);
        if (shell == NULL || shell[0] == '\0') {
            shell = "/bin/sh";
        }
        (void)execl(shell, shell, "-i", (char *)NULL);
        (void)execl("/bin/sh", "sh", "-i", (char *)NULL);
        _exit(127);
    }
    (void)fcntl(master, F_SETFL, O_NONBLOCK);
    st->master = master;
    st->child = kid;
    st->alive = 1;
    return 0;
}

static void
send_bytes(term_state_t *st, const char *bytes, size_t n)
{
    if (st->alive) {
        (void)write(st->master, bytes, n);
        /* Typing returns to the live screen: a keystroke aimed at a
         * shell the user cannot see would go somewhere invisible. */
        if (st->view != 0) {
            st->view = 0;
            st->dirty = 1;
        }
    }
}

static int
term_start(void **state, const tiku_desk_app_services_t *services)
{
    term_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->master = -1;
    st->fg = 8u;
    st->bg = 8u;
    st->top = 0;
    st->bottom = ROWS - 1;
    st->cursor_on = 1;
    /* Every glyph of this face advances the same width, so a column IS
     * that width and the grid is exact. */
    st->cell_w = tiku_desk_font_mono_cell(0);
    st->cell_h = tiku_desk_font_mono(0)->height + 1;
    st->surface = tiku_desk_surface_new(width_px(st), height_px(st),
                                        TIKU_DESK_C_DOC);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    grid_clear(st);
    if (start_shell(st) != 0) {
        const char *sorry = "no shell could be started";
        int i;

        for (i = 0; sorry[i] != '\0'; i++) {
            feed(st, (unsigned char)sorry[i]);
        }
    }
    st->id = services->open(services->ctx, "Terminal", width_px(st),
                            height_px(st));
    paint(st);
    publish(st);
    *state = st;
    return 0;
}

static void
term_stop(void *state)
{
    term_state_t *st = state;

    if (st == NULL) {
        return;
    }
    if (st->child > 0) {
        (void)kill(st->child, SIGHUP);
        (void)waitpid(st->child, NULL, WNOHANG);
    }
    if (st->master >= 0) {
        (void)close(st->master);
    }
    tiku_desk_surface_free(st->surface);
    free(st);
}

static void
term_tick(void *state, int64_t now_us)
{
    term_state_t *st = state;
    char buf[8192];
    ssize_t n;

    (void)now_us;
    if (st->master < 0) {
        return;
    }
    while ((n = read(st->master, buf, sizeof buf)) > 0) {
        ssize_t i;

        for (i = 0; i < n; i++) {
            feed(st, (unsigned char)buf[i]);
        }
        st->dirty = 1;
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                   errno != EINTR)) {
        if (st->alive) {
            st->alive = 0;
            st->dirty = 1;
        }
    }
    if (st->child > 0 && waitpid(st->child, NULL, WNOHANG) == st->child) {
        st->child = -1;
        st->alive = 0;
        st->dirty = 1;
    }
    if (st->dirty) {
        paint(st);
    }
}

/** @brief Move the view @p by rows into what has gone past. */
static void
scroll_view(term_state_t *st, int by)
{
    int limit = st->back_count;

    st->view += by;
    if (st->view > limit) {
        st->view = limit;
    }
    if (st->view < 0) {
        st->view = 0;
    }
    st->dirty = 1;
    paint(st);
}

static int
term_event(void *state, const tiku_desk_event_t *event)
{
    term_state_t *st = state;
    const char *seq = NULL;
    char one;

    if (event->type != TIKU_DESK_EVENT_KEY_DOWN) {
        return 0;
    }
    /* Shift with a paging key looks at history rather than reaching the
     * shell, which is what a terminal has always done with it. */
    if ((event->modifiers & TIKU_DESK_MOD_SHIFT) != 0u) {
        if (event->key == TIKU_DESK_KEY_PAGE_UP) {
            scroll_view(st, ROWS - 1);
            return 0;
        }
        if (event->key == TIKU_DESK_KEY_PAGE_DOWN) {
            scroll_view(st, -(ROWS - 1));
            return 0;
        }
    }
    if ((event->modifiers & TIKU_DESK_MOD_CMD) != 0u) {
        /* Every control code, not a chosen few: the letter's place in
         * the alphabet IS the code, which is why Ctrl+C is 3. */
        unsigned k = event->key;

        if (k >= 'a' && k <= 'z') {
            one = (char)(k - 'a' + 1);
            send_bytes(st, &one, 1u);
        } else if (k >= 'A' && k <= 'Z') {
            one = (char)(k - 'A' + 1);
            send_bytes(st, &one, 1u);
        }
        return 0;
    }
    switch (event->key) {
    case TIKU_DESK_KEY_RETURN:
        one = '\r';
        send_bytes(st, &one, 1u);
        return 0;
    case TIKU_DESK_KEY_BACKSPACE:
        one = 0x7f;
        send_bytes(st, &one, 1u);
        return 0;
    case TIKU_DESK_KEY_TAB:
        one = '\t';
        send_bytes(st, &one, 1u);
        return 0;
    case TIKU_DESK_KEY_ESCAPE:
        one = 0x1b;
        send_bytes(st, &one, 1u);
        return 0;
    /* The sequences a line editor listens for: without them there is no
     * history, and no cursor at all in a full-screen editor. */
    case TIKU_DESK_KEY_UP:
        seq = "\x1b[A";
        break;
    case TIKU_DESK_KEY_DOWN:
        seq = "\x1b[B";
        break;
    case TIKU_DESK_KEY_RIGHT:
        seq = "\x1b[C";
        break;
    case TIKU_DESK_KEY_LEFT:
        seq = "\x1b[D";
        break;
    case TIKU_DESK_KEY_HOME:
        seq = "\x1b[H";
        break;
    case TIKU_DESK_KEY_END:
        seq = "\x1b[F";
        break;
    case TIKU_DESK_KEY_DELETE:
        seq = "\x1b[3~";
        break;
    case TIKU_DESK_KEY_PAGE_UP:
        seq = "\x1b[5~";
        break;
    case TIKU_DESK_KEY_PAGE_DOWN:
        seq = "\x1b[6~";
        break;
    default:
        if (event->key >= 32u && event->key < 127u) {
            one = (char)event->key;
            send_bytes(st, &one, 1u);
        }
        return 0;
    }
    send_bytes(st, seq, strlen(seq));
    return 0;
}

static int
term_pick(void *state, uint32_t window, int command)
{
    term_state_t *st = state;

    (void)window;
    switch (command) {
    case CMD_QUIT:
        return 1;
    case CMD_CLEAR:
        grid_clear(st);
        paint(st);
        break;
    case CMD_RESET:
        /* What a wedged screen needs: attributes, region and parser back
         * where they started, without ending the shell. */
        st->fg = 8u;
        st->bg = 8u;
        st->attr = 0u;
        st->top = 0;
        st->bottom = ROWS - 1;
        st->scan = SCAN_TEXT;
        st->cursor_on = 1;
        st->view = 0;
        grid_clear(st);
        paint(st);
        break;
    default:
        break;
    }
    return 0;
}

const tiku_desk_app_descriptor_t tiku_term_app = {
    .id = "org.tikuos.terminal",
    .name = "Terminal",
    .start = term_start,
    .stop = term_stop,
    .event = term_event,
    .tick = term_tick,
    .pick = term_pick
};

#ifndef TIKU_APP_EMBED
int
main(void)
{
    return tiku_desk_client_run(&tiku_term_app);
}
#endif
