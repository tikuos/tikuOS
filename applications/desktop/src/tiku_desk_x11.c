/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_x11.c - the native window backend.
 *
 * Plain Xlib: the interface layer owns every pixel, so a toolkit here would
 * only add a dependency and a second event model.  The wasm build replaces
 * this one file with a canvas.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_desk_gfx.h"

/**
 * @brief Open a window showing @p s and pump events until it is closed.
 *
 * @return 0 on a clean exit, -1 if no display was available.
 */
int
tiku_desk_x11_show(const tiku_desk_surface_t *s, const char *title)
{
    Display *d;
    Window w;
    XImage *img;
    GC gc;
    Atom del;
    char *pixels;
    int screen, running = 1;

    d = XOpenDisplay(NULL);
    if (d == NULL) {
        return -1;
    }
    screen = DefaultScreen(d);
    w = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0,
                            (unsigned)s->w, (unsigned)s->h, 0,
                            BlackPixel(d, screen), WhitePixel(d, screen));
    XStoreName(d, w, title);
    XSelectInput(d, w, ExposureMask | KeyPressMask | ButtonPressMask |
                       StructureNotifyMask);
    del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &del, 1);
    XMapWindow(d, w);
    gc = XCreateGC(d, w, 0, NULL);

    /* XImage frees this on XDestroyImage, so it must be its own copy. */
    pixels = malloc((size_t)s->w * (size_t)s->h * 4u);
    if (pixels == NULL) {
        XCloseDisplay(d);
        return -1;
    }
    memcpy(pixels, s->px, (size_t)s->w * (size_t)s->h * 4u);
    img = XCreateImage(d, DefaultVisual(d, screen),
                       (unsigned)DefaultDepth(d, screen), ZPixmap, 0, pixels,
                       (unsigned)s->w, (unsigned)s->h, 32, 0);
    if (img == NULL) {
        free(pixels);
        XCloseDisplay(d);
        return -1;
    }
    while (running) {
        XEvent e;
        XNextEvent(d, &e);
        switch (e.type) {
        case Expose:
            XPutImage(d, w, gc, img, 0, 0, 0, 0, (unsigned)s->w,
                      (unsigned)s->h);
            break;
        case KeyPress:
        case ButtonPress:
            running = 0;
            break;
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == del) {
                running = 0;
            }
            break;
        default:
            break;
        }
    }
    XDestroyImage(img);
    XFreeGC(d, gc);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 0;
}
