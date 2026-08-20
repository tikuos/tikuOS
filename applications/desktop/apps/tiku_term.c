/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_term.c - a terminal, as one application descriptor.
 *
 * A shell on a pty writes into a character grid, which is painted whole
 * whenever it changes.  Escape sequences are recognised only far enough
 * to keep them off the screen.
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

#define COLS     80
#define ROWS     24
#define STRIP_H  20
#define MARGIN   4

#define CMD_CLEAR 1
#define CMD_QUIT  2

typedef struct {
    const tiku_desk_app_services_t *services;
    tiku_desk_surface_t            *surface;
    uint32_t                        id;
    int                             master;
    pid_t                           child;
    char                            cell[ROWS][COLS];
    int                             row, col;
    int                             escape;     /* inside a sequence */
    int                             alive;
    int                             dirty;
    int                             cell_w, cell_h;
} term_state_t;

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

static void
grid_clear(term_state_t *st)
{
    memset(st->cell, ' ', sizeof st->cell);
    st->row = 0;
    st->col = 0;
    st->dirty = 1;
}

static void
scroll_up(term_state_t *st)
{
    memmove(&st->cell[0], &st->cell[1], sizeof st->cell[0] * (ROWS - 1));
    memset(&st->cell[ROWS - 1], ' ', sizeof st->cell[0]);
    st->row = ROWS - 1;
}

static void
newline(term_state_t *st)
{
    st->col = 0;
    if (++st->row >= ROWS) {
        scroll_up(st);
    }
}

/** @brief Place one byte of the shell's output into the grid. */
static void
put_byte(term_state_t *st, unsigned char c)
{
    if (st->escape) {
        /* Sequences are swallowed rather than obeyed: a final byte in the
         * @-to-~ range ends one, and nothing in between is printable. */
        if (c >= '@' && c <= '~') {
            st->escape = 0;
        }
        return;
    }
    switch (c) {
    case 0x1b:
        st->escape = 1;
        return;
    case '\n':
        newline(st);
        return;
    case '\r':
        st->col = 0;
        return;
    case '\b':
        if (st->col > 0) {
            st->col--;
        }
        return;
    case '\t':
        st->col = (st->col + 8) & ~7;
        if (st->col >= COLS) {
            newline(st);
        }
        return;
    case 0x07:
        return;                 /* the bell has nowhere to ring */
    default:
        break;
    }
    if (c < 32u || c > 126u) {
        return;
    }
    if (st->col >= COLS) {
        newline(st);
    }
    st->cell[st->row][st->col++] = (char)c;
}

static void
paint(term_state_t *st)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    const tiku_desk_font_t *small = tiku_desk_font_at(11);
    int w = width_px(st);
    int h = height_px(st);
    tiku_desk_rect_t page = { 0, 0, w, h - STRIP_H };
    tiku_desk_rect_t strip = { 0, h - STRIP_H, w, STRIP_H };
    int r, c;

    tiku_desk_fill(st->surface, page, TIKU_DESK_C_DOC);
    for (r = 0; r < ROWS; r++) {
        int y = MARGIN + r * st->cell_h + f->ascent;

        for (c = 0; c < COLS; c++) {
            char one[2];

            if (st->cell[r][c] == ' ') {
                continue;
            }
            one[0] = st->cell[r][c];
            one[1] = '\0';
            /* Drawn cell by cell: the face is proportional, and a
             * terminal's columns have to line up regardless. */
            tiku_desk_text(st->surface, f, MARGIN + c * st->cell_w, y,
                           one, TIKU_DESK_C_TEXT);
        }
    }
    if (st->alive) {
        tiku_desk_fill(st->surface,
                       (tiku_desk_rect_t){ MARGIN + st->col * st->cell_w,
                                           MARGIN + st->row * st->cell_h,
                                           st->cell_w, st->cell_h },
                       tiku_desk_tint(TIKU_DESK_C_DOC, 0.75f));
    }
    tiku_desk_fill(st->surface, strip, TIKU_DESK_C_PANEL);
    tiku_desk_hline(st->surface, 0, strip.y, w,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    tiku_desk_text(st->surface, small, MARGIN,
                   strip.y + (STRIP_H - small->height) / 2 + small->ascent,
                   st->alive ? "sh" : "the shell has exited",
                   TIKU_DESK_C_TEXT);
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
    menus.menu[0].nitem = 2;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Clear");
    menus.menu[0].item[0].command = CMD_CLEAR;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Quit");
    menus.menu[0].item[1].command = CMD_QUIT;
    menus.menu[0].item[1].enabled = 1;
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

/**
 * @brief Start a shell on a pty of its own.
 *
 * @note The child gets the pty as its controlling terminal, or a shell
 *       started from it would have no job control at all.
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
        (void)setenv("TERM", "dumb", 1);
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
    }
}

static int
term_start(void **state, const tiku_desk_app_services_t *services)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    term_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->master = -1;
    /* One cell is the widest character the face draws, so nothing
     * overruns its column. */
    st->cell_w = tiku_desk_text_width(f, "W");
    st->cell_h = f->height + 1;
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
            put_byte(st, (unsigned char)sorry[i]);
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

/** @brief Take whatever the shell has written, and repaint if any came. */
static void
term_tick(void *state, int64_t now_us)
{
    term_state_t *st = state;
    char buf[4096];
    ssize_t n;

    (void)now_us;
    if (st->master < 0) {
        return;
    }
    while ((n = read(st->master, buf, sizeof buf)) > 0) {
        ssize_t i;

        for (i = 0; i < n; i++) {
            put_byte(st, (unsigned char)buf[i]);
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

static int
term_event(void *state, const tiku_desk_event_t *event)
{
    term_state_t *st = state;
    char one;

    if (event->type != TIKU_DESK_EVENT_KEY_DOWN) {
        return 0;
    }
    if ((event->modifiers & TIKU_DESK_MOD_CMD) != 0u) {
        /* The control codes a shell needs: interrupt, and end of input. */
        if (event->key == 'c' || event->key == 'C') {
            one = 0x03;
            send_bytes(st, &one, 1u);
        } else if (event->key == 'd' || event->key == 'D') {
            one = 0x04;
            send_bytes(st, &one, 1u);
        }
        return 0;
    }
    switch (event->key) {
    case TIKU_DESK_KEY_RETURN:
        one = '\r';
        send_bytes(st, &one, 1u);
        break;
    case TIKU_DESK_KEY_BACKSPACE:
        one = 0x7f;
        send_bytes(st, &one, 1u);
        break;
    case TIKU_DESK_KEY_TAB:
        one = '\t';
        send_bytes(st, &one, 1u);
        break;
    default:
        if (event->key >= 32u && event->key < 127u) {
            one = (char)event->key;
            send_bytes(st, &one, 1u);
        }
        break;
    }
    return 0;
}

static int
term_pick(void *state, uint32_t window, int command)
{
    term_state_t *st = state;

    (void)window;
    if (command == CMD_QUIT) {
        return 1;
    }
    if (command == CMD_CLEAR) {
        grid_clear(st);
        paint(st);
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
