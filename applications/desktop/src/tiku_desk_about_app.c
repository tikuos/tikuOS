/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_about_app.c - the About box as a SEPARATE PROCESS.
 *
 * The thesis in one artifact: the same R5-mannered About panel that lives
 * linked into the Tracker desktop also runs here as its own binary,
 * speaking the window session over the desk socket -- a surface out,
 * events and menu picks back.  Its faults are its own; the desktop it
 * appears in never linked a line of it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "tiku_desk_gfx.h"
#include "tiku_desk_font.h"
#include "tiku_desk_remote.h"

#define ABOUT_W 400
#define ABOUT_H 210

#define CMD_QUIT    1
#define CMD_REFRESH 2

static long
read_uptime(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    double up = 0.0;

    if (f != NULL) {
        (void)fscanf(f, "%lf", &up);
        (void)fclose(f);
    }
    return (long)up;
}

static void
paint(tiku_desk_surface_t *s, const char *kernel)
{
    const tiku_desk_font_t *plain = tiku_desk_font_plain();
    const tiku_desk_font_t *big = tiku_desk_font_at(28);
    const tiku_desk_font_t *bold = tiku_desk_font_bold();
    int facts_x = 150;
    int y = 20;
    char line[160];

    tiku_desk_fill(s, (tiku_desk_rect_t){ 0, 0, ABOUT_W, ABOUT_H },
                   TIKU_DESK_C_PANEL);
    {
        int base = 22 + big->ascent;

        tiku_desk_text(s, big, 18, base, "Tiku", TIKU_DESK_C_TEXT);
        tiku_desk_fill(s, (tiku_desk_rect_t){ 18, base + 6, 96, 3 },
                       TIKU_DESK_C_TAB);
        tiku_desk_text(s, bold, 18, base + 14 + plain->ascent,
                       "out of process", TIKU_DESK_C_TEXT);
    }
    tiku_desk_text(s, bold, facts_x, y + plain->ascent,
                   "About, from its own process", TIKU_DESK_C_TEXT);
    y += plain->height + 10;
    tiku_desk_text(s, plain, facts_x, y + plain->ascent, kernel,
                   TIKU_DESK_C_TEXT);
    y += plain->height + 10;
    {
        long up = read_uptime();

        snprintf(line, sizeof line, "up %ld day%s, %ld:%02ld",
                 up / 86400, (up / 86400) == 1 ? "" : "s",
                 (up % 86400) / 3600, (up % 3600) / 60);
        tiku_desk_text(s, plain, facts_x, y + plain->ascent, line,
                       TIKU_DESK_C_TEXT);
    }
    {
        snprintf(line, sizeof line,
                 "pid %ld: whose faults are whose", (long)getpid());
        tiku_desk_text(s, plain, 18, ABOUT_H - 14, line,
                       TIKU_DESK_C_TEXT);
    }
}

int
main(void)
{
    tiku_desk_remote_client_t client;
    tiku_desk_surface_t *surface;
    char kernel[96];
    struct utsname un;
    uint32_t id;
    long shown_minute = -1;

    if (uname(&un) == 0) {
        snprintf(kernel, sizeof kernel, "%s %s (%s)", un.sysname,
                 un.release, un.machine);
    } else {
        snprintf(kernel, sizeof kernel, "unknown kernel");
    }
    if (tiku_desk_remote_connect(&client, "about", 8000) != 0) {
        fprintf(stderr, "about: no desktop answered\n");
        return 1;
    }
    surface = tiku_desk_surface_new(ABOUT_W, ABOUT_H, TIKU_DESK_C_PANEL);
    if (surface == NULL) {
        return 1;
    }
    id = tiku_desk_remote_open(&client, "About (remote)", ABOUT_W,
                               ABOUT_H);
    paint(surface, kernel);
    (void)tiku_desk_remote_frame(&client, id, surface->px, ABOUT_W,
                                 ABOUT_H);
    {
        /* The menus travel as the same plain data a linked-in window
         * publishes -- the global bar cannot tell the difference. */
        tiku_desk_menuset_t menus;

        memset(&menus, 0, sizeof menus);
        menus.nmenu = 1;
        snprintf(menus.menu[0].title, sizeof menus.menu[0].title,
                 "Remote");
        menus.menu[0].nitem = 2;
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label, "Refresh");
        menus.menu[0].item[0].command = CMD_REFRESH;
        menus.menu[0].item[0].enabled = 1;
        snprintf(menus.menu[0].item[1].label,
                 sizeof menus.menu[0].item[1].label, "Quit");
        menus.menu[0].item[1].command = CMD_QUIT;
        menus.menu[0].item[1].enabled = 1;
        (void)tiku_desk_remote_menus(&client, id, &menus);
    }
    for (;;) {
        tiku_desk_event_t event;
        uint32_t evid = 0;
        int command = 0;
        int type = tiku_desk_remote_read(&client, &evid, &event, &command);
        long minute;

        if (type < 0 || type == TIKU_DESK_RMSG_CLOSED) {
            break;              /* the desktop went away, or said done */
        }
        if (type == TIKU_DESK_RMSG_PICK) {
            if (command == CMD_QUIT) {
                break;
            }
            if (command == CMD_REFRESH) {
                paint(surface, kernel);
                (void)tiku_desk_remote_frame(&client, id, surface->px,
                                             ABOUT_W, ABOUT_H);
            }
        }
        if (type == TIKU_DESK_RMSG_EVENT &&
            event.type == TIKU_DESK_EVENT_KEY_DOWN &&
            event.key == TIKU_DESK_KEY_ESCAPE) {
            break;
        }
        minute = read_uptime() / 60;
        if (minute != shown_minute) {
            /* The uptime ticks from here, not from the compositor:
             * liveness is the CLIENT's to prove. */
            shown_minute = minute;
            paint(surface, kernel);
            (void)tiku_desk_remote_frame(&client, id, surface->px,
                                         ABOUT_W, ABOUT_H);
        }
        usleep(30000);
    }
    tiku_desk_remote_disconnect(&client);
    tiku_desk_surface_free(surface);
    return 0;
}
