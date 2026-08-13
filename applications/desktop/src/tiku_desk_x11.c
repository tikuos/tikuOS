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
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_desk_host.h"
#include "tiku_desk_gfx.h"

struct tiku_desk_host {
    Display *display;
    Window window;
    GC gc;
    XImage *image;
    Atom delete_window;
    Cursor resize_cursor;
    int width;
    int height;
};

/** @brief Translate native modifiers once, at the host boundary. */
static unsigned
event_modifiers(unsigned state)
{
    unsigned modifiers = 0u;

    if ((state & ShiftMask) != 0u) { modifiers |= TIKU_DESK_MOD_SHIFT; }
    if ((state & ControlMask) != 0u) { modifiers |= TIKU_DESK_MOD_CMD; }
    if ((state & Mod1Mask) != 0u) { modifiers |= TIKU_DESK_MOD_OPTION; }
    if ((state & LockMask) != 0u) { modifiers |= TIKU_DESK_MOD_CAPS; }
    return modifiers;
}

/** @brief Translate navigation keys; printable values remain characters. */
static unsigned
event_key(KeySym keysym)
{
    switch (keysym) {
    case XK_Up:        return TIKU_DESK_KEY_UP;
    case XK_Down:      return TIKU_DESK_KEY_DOWN;
    case XK_Left:      return TIKU_DESK_KEY_LEFT;
    case XK_Right:     return TIKU_DESK_KEY_RIGHT;
    case XK_Home:      return TIKU_DESK_KEY_HOME;
    case XK_End:       return TIKU_DESK_KEY_END;
    case XK_Prior:     return TIKU_DESK_KEY_PAGE_UP;
    case XK_Next:      return TIKU_DESK_KEY_PAGE_DOWN;
    case XK_BackSpace: return TIKU_DESK_KEY_BACKSPACE;
    case XK_Delete:    return TIKU_DESK_KEY_DELETE;
    case XK_Escape:    return TIKU_DESK_KEY_ESCAPE;
    case XK_Return:
    case XK_KP_Enter:  return TIKU_DESK_KEY_RETURN;
    case XK_Tab:       return TIKU_DESK_KEY_TAB;
    case XK_Menu:      return TIKU_DESK_KEY_MENU;
    case XK_F2:        return TIKU_DESK_KEY_F2;
    case XK_Shift_L:
    case XK_Shift_R:   return TIKU_DESK_KEY_SHIFT;
    case XK_Control_L:
    case XK_Control_R: return TIKU_DESK_KEY_CMD;
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R:    return TIKU_DESK_KEY_OPTION;
    default:
        if (keysym >= XK_A && keysym <= XK_Z) {
            return (unsigned)(keysym - XK_A + 'a');
        }
        return (keysym >= 0x20 && keysym < 0x7fu)
                   ? (unsigned)keysym : 0u;
    }
}

tiku_desk_host_t *
tiku_desk_host_open(int width, int height, const char *title)
{
    tiku_desk_host_t *host;
    char *pixels;
    int screen;

    if (width <= 0 || height <= 0) {
        return NULL;
    }
    host = calloc(1u, sizeof *host);
    if (host == NULL) {
        return NULL;
    }
    host->display = XOpenDisplay(NULL);
    if (host->display == NULL) {
        free(host);
        return NULL;
    }
    screen = DefaultScreen(host->display);
    host->window = XCreateSimpleWindow(host->display,
        RootWindow(host->display, screen), 0, 0, (unsigned)width,
        (unsigned)height, 0, BlackPixel(host->display, screen),
        WhitePixel(host->display, screen));
    XSelectInput(host->display, host->window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask | FocusChangeMask);
    host->delete_window = XInternAtom(host->display, "WM_DELETE_WINDOW",
                                      False);
    XSetWMProtocols(host->display, host->window, &host->delete_window, 1);
    XStoreName(host->display, host->window,
               (title != NULL) ? title : "TikuOS");
    XMapWindow(host->display, host->window);
    host->gc = XCreateGC(host->display, host->window, 0, NULL);
    pixels = malloc((size_t)width * (size_t)height * 4u);
    if (pixels == NULL) {
        tiku_desk_host_close(host);
        return NULL;
    }
    host->image = XCreateImage(host->display,
        DefaultVisual(host->display, screen),
        (unsigned)DefaultDepth(host->display, screen), ZPixmap, 0, pixels,
        (unsigned)width, (unsigned)height, 32, 0);
    if (host->image == NULL) {
        free(pixels);
        tiku_desk_host_close(host);
        return NULL;
    }
    host->resize_cursor = XCreateFontCursor(host->display,
                                            XC_sb_h_double_arrow);
    host->width = width;
    host->height = height;
    return host;
}

void
tiku_desk_host_close(tiku_desk_host_t *host)
{
    if (host == NULL) {
        return;
    }
    if (host->image != NULL) {
        XDestroyImage(host->image);
    }
    if (host->display != NULL && host->resize_cursor != None) {
        XFreeCursor(host->display, host->resize_cursor);
    }
    if (host->display != NULL && host->gc != NULL) {
        XFreeGC(host->display, host->gc);
    }
    if (host->display != NULL && host->window != None) {
        XDestroyWindow(host->display, host->window);
    }
    if (host->display != NULL) {
        XCloseDisplay(host->display);
    }
    free(host);
}

int
tiku_desk_host_present(tiku_desk_host_t *host,
                       const tiku_desk_surface_t *surface)
{
    if (host == NULL || surface == NULL || surface->w != host->width ||
        surface->h != host->height || host->image == NULL) {
        return -1;
    }
    memcpy(host->image->data, surface->px,
           (size_t)surface->w * (size_t)surface->h * 4u);
    XPutImage(host->display, host->window, host->gc, host->image,
              0, 0, 0, 0, (unsigned)surface->w, (unsigned)surface->h);
    XFlush(host->display);
    return 0;
}

void
tiku_desk_host_set_title(tiku_desk_host_t *host, const char *title)
{
    if (host != NULL && title != NULL) {
        XStoreName(host->display, host->window, title);
    }
}

int
tiku_desk_host_poll(tiku_desk_host_t *host, tiku_desk_event_t *event)
{
    XEvent native;

    if (host == NULL || event == NULL || XPending(host->display) == 0) {
        return 0;
    }
    XNextEvent(host->display, &native);
    memset(event, 0, sizeof *event);
    switch (native.type) {
    case Expose:
        event->type = TIKU_DESK_EVENT_EXPOSE;
        break;
    case KeyPress:
    case KeyRelease: {
        KeySym keysym = NoSymbol;
        int n = XLookupString(&native.xkey, event->text,
                              (int)sizeof event->text - 1, &keysym, NULL);

        event->type = (native.type == KeyPress)
                          ? TIKU_DESK_EVENT_KEY_DOWN
                          : TIKU_DESK_EVENT_KEY_UP;
        event->modifiers = event_modifiers(native.xkey.state);
        event->key = event_key(keysym);
        event->time_us = (int64_t)native.xkey.time * 1000;
        if (n < 0) { n = 0; }
        event->text[n] = '\0';
        break;
    }
    case ButtonPress:
    case ButtonRelease:
        event->type = (native.type == ButtonPress)
                          ? TIKU_DESK_EVENT_POINTER_DOWN
                          : TIKU_DESK_EVENT_POINTER_UP;
        event->x = native.xbutton.x;
        event->y = native.xbutton.y;
        event->button = native.xbutton.button;
        event->modifiers = event_modifiers(native.xbutton.state);
        event->time_us = (int64_t)native.xbutton.time * 1000;
        break;
    case MotionNotify:
        event->type = TIKU_DESK_EVENT_POINTER_MOVE;
        event->x = native.xmotion.x;
        event->y = native.xmotion.y;
        event->modifiers = event_modifiers(native.xmotion.state);
        event->time_us = (int64_t)native.xmotion.time * 1000;
        break;
    case FocusIn:
        event->type = TIKU_DESK_EVENT_ACTIVATED;
        break;
    case FocusOut:
        event->type = TIKU_DESK_EVENT_DEACTIVATED;
        break;
    case ConfigureNotify:
        event->type = TIKU_DESK_EVENT_RESIZE;
        event->width = native.xconfigure.width;
        event->height = native.xconfigure.height;
        break;
    case ClientMessage:
        if ((Atom)native.xclient.data.l[0] == host->delete_window) {
            event->type = TIKU_DESK_EVENT_CLOSE;
        }
        break;
    default:
        event->type = TIKU_DESK_EVENT_NONE;
        break;
    }
    return 1;
}

void
tiku_desk_host_set_resize_cursor(tiku_desk_host_t *host, int enabled)
{
    if (host == NULL) {
        return;
    }
    if (enabled) {
        XDefineCursor(host->display, host->window, host->resize_cursor);
    } else {
        XUndefineCursor(host->display, host->window);
    }
}

int
tiku_desk_host_pointer(tiku_desk_host_t *host, int *x, int *y,
                       unsigned *modifiers)
{
    Window root, child;
    int root_x, root_y, local_x, local_y;
    unsigned state;

    if (host == NULL || !XQueryPointer(host->display, host->window,
                                       &root, &child, &root_x, &root_y,
                                       &local_x, &local_y, &state)) {
        return 0;
    }
    if (x != NULL) { *x = local_x; }
    if (y != NULL) { *y = local_y; }
    if (modifiers != NULL) { *modifiers = event_modifiers(state); }
    return 1;
}

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
