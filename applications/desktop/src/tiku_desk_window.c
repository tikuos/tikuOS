/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_window.c - composited in-process windows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_window.h"
#include "tiku_desk_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The tab and its buttons, from the face rather than from memory.  The
 * arithmetic is chosen to answer 21 and 12 at 12 px -- the numbers that
 * were written here before -- so the default interface is pixel for
 * pixel what it was, and only the other sizes change.
 */
int
tiku_desk_window_tab_h(void)
{
    int want = tiku_desk_font_bold()->height + 6;   /* 21 at 12 px */
    int least = tiku_desk_window_tab_button() + 6;

    return (want > least) ? want : least;
}

int
tiku_desk_window_tab_button(void)
{
    int side = tiku_desk_font_plain()->height - 3;  /* 12 at 12 px */

    return (side > 8) ? side : 8;
}

#define MIN_WIDTH 180
#define MIN_HEIGHT 90

struct tiku_desk_workspace {
    tiku_desk_surface_t *surface;
    tiku_desk_window_t windows[TIKU_DESK_WINDOW_MAX];
    int order[TIKU_DESK_WINDOW_MAX];
    int count;
    int focused;
    tiku_desk_rect_t reserved;
    tiku_desk_hit_t track_what;
    int track_window;
    int track_x;
    int track_y;
    void (*backdrop)(tiku_desk_surface_t *surface, void *context);
    void *backdrop_context;
};

static int
inside(tiku_desk_rect_t rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w &&
           y >= rect.y && y < rect.y + rect.h;
}

static int
order_index(tiku_desk_workspace_t *workspace, int slot)
{
    int i;

    for (i = 0; i < workspace->count; i++) {
        if (workspace->order[i] == slot) {
            return i;
        }
    }
    return -1;
}

static tiku_desk_rect_t
tab_rect(const tiku_desk_window_t *window)
{
    tiku_desk_rect_t tab;
    /* MEASURED, not guessed: the tab is as wide as its title needs --
     * the close button, the title in the bold face, the zoom button when
     * there is one, and the breathing room the draw insets -- so the
     * text on it is never clipped short of the window's own edge. */
    int btn = tiku_desk_window_tab_button();
    int width = btn + 16 +
                tiku_desk_text_width(tiku_desk_font_bold(), window->title) +
                (window->zoomable ? btn + 8 : 0) + 4;

    if (width > window->frame.w) { width = window->frame.w; }
    if (width < 60) { width = 60; }
    /* Flush with the window's left edge, as the original's tab sits --
     * not floated a few pixels in. */
    tab.x = window->frame.x;
    tab.y = window->frame.y;
    tab.w = width;
    tab.h = tiku_desk_window_tab_h();
    return tab;
}

tiku_desk_workspace_t *
tiku_desk_workspace_new(tiku_desk_surface_t *surface)
{
    tiku_desk_workspace_t *workspace = calloc(1u, sizeof *workspace);

    if (workspace != NULL) {
        workspace->surface = surface;
        workspace->focused = -1;
        workspace->track_window = -1;
    }
    return workspace;
}

void
tiku_desk_workspace_free(tiku_desk_workspace_t *workspace)
{
    while (workspace != NULL && workspace->count > 0) {
        tiku_desk_workspace_close(workspace,
            &workspace->windows[workspace->order[workspace->count - 1]]);
    }
    free(workspace);
}

void
tiku_desk_workspace_set_reserved(tiku_desk_workspace_t *workspace,
                                 tiku_desk_rect_t reserved)
{
    if (workspace != NULL) { workspace->reserved = reserved; }
}

int
tiku_desk_workspace_count(const tiku_desk_workspace_t *workspace)
{
    return (workspace != NULL) ? workspace->count : 0;
}

tiku_desk_window_t *
tiku_desk_workspace_at(tiku_desk_workspace_t *workspace, int index)
{
    if (workspace == NULL || index < 0 || index >= workspace->count) {
        return NULL;
    }
    return &workspace->windows[workspace->order[index]];
}

tiku_desk_window_t *
tiku_desk_workspace_focused(tiku_desk_workspace_t *workspace)
{
    if (workspace == NULL || workspace->focused < 0) { return NULL; }
    return &workspace->windows[workspace->focused];
}

void
tiku_desk_workspace_activate(tiku_desk_workspace_t *workspace,
                             tiku_desk_window_t *window)
{
    int slot, at, i;

    if (workspace == NULL || window == NULL) { return; }
    slot = (int)(window - workspace->windows);
    at = order_index(workspace, slot);
    if (at < 0) { return; }
    for (i = at; i < workspace->count - 1; i++) {
        workspace->order[i] = workspace->order[i + 1];
    }
    workspace->order[workspace->count - 1] = slot;
    workspace->focused = slot;
}

tiku_desk_window_t *
tiku_desk_workspace_find(tiku_desk_workspace_t *workspace, void *tag)
{
    int i;

    for (i = 0; workspace != NULL && i < TIKU_DESK_WINDOW_MAX; i++) {
        if (workspace->windows[i].open && workspace->windows[i].tag == tag) {
            return &workspace->windows[i];
        }
    }
    return NULL;
}

tiku_desk_window_t *
tiku_desk_workspace_open(tiku_desk_workspace_t *workspace, const char *title,
                         tiku_desk_rect_t frame,
                         tiku_desk_window_draw_fn draw, void *context,
                         void *tag)
{
    tiku_desk_window_t *window;
    int slot;

    if (workspace == NULL) { return NULL; }
    window = (tag != NULL) ? tiku_desk_workspace_find(workspace, tag) : NULL;
    if (window != NULL) {
        tiku_desk_workspace_activate(workspace, window);
        return window;
    }
    for (slot = 0; slot < TIKU_DESK_WINDOW_MAX; slot++) {
        if (!workspace->windows[slot].open) { break; }
    }
    if (slot == TIKU_DESK_WINDOW_MAX) { return NULL; }
    window = &workspace->windows[slot];
    memset(window, 0, sizeof *window);
    snprintf(window->title, sizeof window->title, "%s",
             (title != NULL) ? title : "");
    window->frame = frame;
    window->draw = draw;
    window->context = context;
    window->tag = tag;
    window->open = 1;
    window->resizable = 1;
    window->zoomable = 1;
    workspace->order[workspace->count++] = slot;
    workspace->focused = slot;
    return window;
}

void
tiku_desk_workspace_close(tiku_desk_workspace_t *workspace,
                          tiku_desk_window_t *window)
{
    tiku_desk_window_destroy_fn destroy;
    void *context;
    int slot, at, i;

    if (workspace == NULL || window == NULL || !window->open) { return; }
    slot = (int)(window - workspace->windows);
    at = order_index(workspace, slot);
    window->open = 0;
    if (at >= 0) {
        for (i = at; i < workspace->count - 1; i++) {
            workspace->order[i] = workspace->order[i + 1];
        }
        workspace->count--;
    }
    workspace->focused = (workspace->count > 0)
                             ? workspace->order[workspace->count - 1] : -1;
    destroy = window->destroy;
    context = window->context;
    window->destroy = NULL;
    if (destroy != NULL) { destroy(window, context); }
}

void
tiku_desk_window_set_handlers(tiku_desk_window_t *window,
                              tiku_desk_window_event_fn event,
                              tiku_desk_window_close_fn close_requested,
                              tiku_desk_window_destroy_fn destroy)
{
    if (window != NULL) {
        window->event = event;
        window->close_requested = close_requested;
        window->destroy = destroy;
    }
}

int
tiku_desk_window_send(tiku_desk_window_t *window,
                      const tiku_desk_event_t *event)
{
    if (window == NULL || !window->open || event == NULL ||
        window->event == NULL) {
        return 0;
    }
    return window->event(window, event, window->context);
}

int
tiku_desk_workspace_request_close(tiku_desk_workspace_t *workspace,
                                  tiku_desk_window_t *window)
{
    if (workspace == NULL || window == NULL || !window->open) { return 0; }
    if (window->close_requested != NULL &&
        !window->close_requested(window, window->context)) {
        return 0;
    }
    tiku_desk_workspace_close(workspace, window);
    return 1;
}

tiku_desk_rect_t
tiku_desk_window_content(const tiku_desk_window_t *window)
{
    tiku_desk_rect_t content;

    content.x = window->frame.x + TIKU_DESK_WINDOW_BORDER;
    content.y = window->frame.y + tiku_desk_window_tab_h() +
                TIKU_DESK_WINDOW_BORDER;
    content.w = window->frame.w - 2 * TIKU_DESK_WINDOW_BORDER;
    content.h = window->frame.h - tiku_desk_window_tab_h() -
                2 * TIKU_DESK_WINDOW_BORDER;
    if (content.w < 0) { content.w = 0; }
    if (content.h < 0) { content.h = 0; }
    return content;
}

void
tiku_desk_window_set_size_controls(tiku_desk_window_t *window,
                                   int resizable, int zoomable)
{
    if (window != NULL) {
        window->resizable = resizable != 0;
        window->zoomable = zoomable != 0;
    }
}

tiku_desk_hit_t
tiku_desk_workspace_hit(tiku_desk_workspace_t *workspace, int x, int y,
                        tiku_desk_window_t **out)
{
    int i;

    if (out != NULL) { *out = NULL; }
    if (workspace == NULL) { return TIKU_DESK_HIT_NONE; }
    for (i = workspace->count - 1; i >= 0; i--) {
        tiku_desk_window_t *window =
            &workspace->windows[workspace->order[i]];
        tiku_desk_rect_t tab, content;

        if (!window->open || window->minimized) { continue; }
        tab = tab_rect(window);
        if (inside(tab, x, y)) {
            if (out != NULL) { *out = window; }
            int btn = tiku_desk_window_tab_button();

            if (x < tab.x + btn + 6) {
                return TIKU_DESK_HIT_CLOSE;
            }
            if (window->zoomable && x > tab.x + tab.w - btn - 6) {
                return TIKU_DESK_HIT_ZOOM;
            }
            return TIKU_DESK_HIT_TAB;
        }
        if (!inside(window->frame, x, y)) { continue; }
        if (out != NULL) { *out = window; }
        if (window->resizable &&
            x > window->frame.x + window->frame.w - 16 &&
            y > window->frame.y + window->frame.h - 16) {
            return TIKU_DESK_HIT_RESIZE;
        }
        content = tiku_desk_window_content(window);
        return inside(content, x, y) ? TIKU_DESK_HIT_CONTENT
                                     : TIKU_DESK_HIT_BORDER;
    }
    return TIKU_DESK_HIT_NONE;
}

void
tiku_desk_workspace_begin_track(tiku_desk_workspace_t *workspace,
                                tiku_desk_window_t *window,
                                tiku_desk_hit_t what, int x, int y)
{
    if (workspace == NULL || window == NULL) { return; }
    workspace->track_what = what;
    workspace->track_window = (int)(window - workspace->windows);
    workspace->track_x = x;
    workspace->track_y = y;
}

int
tiku_desk_workspace_tracking(const tiku_desk_workspace_t *workspace)
{
    return workspace != NULL && workspace->track_window >= 0;
}

int
tiku_desk_workspace_track(tiku_desk_workspace_t *workspace, int x, int y)
{
    tiku_desk_window_t *window;
    int dx, dy;

    if (workspace == NULL || workspace->track_window < 0) { return 0; }
    window = &workspace->windows[workspace->track_window];
    dx = x - workspace->track_x;
    dy = y - workspace->track_y;
    if (dx == 0 && dy == 0) { return 0; }
    workspace->track_x = x;
    workspace->track_y = y;
    if (workspace->track_what == TIKU_DESK_HIT_TAB) {
        window->frame.x += dx;
        window->frame.y += dy;
        if (window->frame.y < 0) { window->frame.y = 0; }
        if (window->frame.x + 40 > workspace->surface->w) {
            window->frame.x = workspace->surface->w - 40;
        }
        if (window->frame.x + window->frame.w < 40) {
            window->frame.x = 40 - window->frame.w;
        }
        if (window->frame.y + tiku_desk_window_tab_h() >
            workspace->surface->h) {
            window->frame.y = workspace->surface->h -
                              tiku_desk_window_tab_h();
        }
    } else if (workspace->track_what == TIKU_DESK_HIT_RESIZE) {
        window->frame.w += dx;
        window->frame.h += dy;
        if (window->frame.w < MIN_WIDTH) { window->frame.w = MIN_WIDTH; }
        if (window->frame.h < MIN_HEIGHT) { window->frame.h = MIN_HEIGHT; }
    } else {
        return 0;
    }
    return 1;
}

void
tiku_desk_workspace_end_track(tiku_desk_workspace_t *workspace)
{
    if (workspace != NULL) {
        workspace->track_window = -1;
        workspace->track_what = TIKU_DESK_HIT_NONE;
    }
}

int
tiku_desk_workspace_zoom(tiku_desk_workspace_t *workspace,
                         tiku_desk_window_t *window, int fit_width,
                         int fit_height)
{
    tiku_desk_rect_t was, wanted, content;
    int max_width, max_height;

    if (workspace == NULL || window == NULL) { return 0; }
    was = window->frame;
    wanted = was;
    content = tiku_desk_window_content(window);
    wanted.w = fit_width + (was.w - content.w);
    wanted.h = fit_height + (was.h - content.h);
    max_width = workspace->surface->w - 10 - wanted.x;
    max_height = workspace->surface->h - 10 - wanted.y -
                 tiku_desk_window_tab_h();
    if (workspace->reserved.w > 0 && wanted.x < workspace->reserved.x) {
        int room = workspace->reserved.x - wanted.x - 5;

        if (room < max_width) { max_width = room; }
    }
    if (wanted.w > max_width) { wanted.w = max_width; }
    if (wanted.h > max_height) { wanted.h = max_height; }
    if (wanted.w < 120) { wanted.w = 120; }
    if (wanted.h < 80) { wanted.h = 80; }
    if (wanted.w == was.w && wanted.h == was.h) {
        if (!window->zoom_valid) { return 0; }
        window->frame = window->saved_zoom;
        window->zoom_valid = 0;
        return 1;
    }
    window->saved_zoom = was;
    window->zoom_valid = 1;
    window->frame = wanted;
    return 1;
}

static void
draw_button(tiku_desk_surface_t *surface, int x, int y, int zoom, int active)
{
    int side = tiku_desk_window_tab_button();
    tiku_desk_rect_t button = { x, y, side, side };
    tiku_desk_rgb_t face = active ? TIKU_DESK_C_TAB : TIKU_DESK_C_TAB_IDLE;

    tiku_desk_ui_tab_widget(surface, button,
        zoom ? TIKU_DESK_TAB_ZOOM : TIKU_DESK_TAB_CLOSE, face, 0);
}

static void
draw_window(tiku_desk_workspace_t *workspace, tiku_desk_window_t *window,
            int active)
{
    tiku_desk_surface_t *surface = workspace->surface;
    const tiku_desk_font_t *font = tiku_desk_font_bold();
    tiku_desk_rect_t tab = tab_rect(window);
    int tab_h = tiku_desk_window_tab_h();
    int btn = tiku_desk_window_tab_button();
    tiku_desk_rect_t body = { window->frame.x,
        window->frame.y + tab_h - 1, window->frame.w,
        window->frame.h - tab_h + 1 };
    tiku_desk_rect_t content = tiku_desk_window_content(window);
    tiku_desk_rgb_t tab_color = active ? TIKU_DESK_C_TAB
                                       : TIKU_DESK_C_TAB_IDLE;
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);

    tiku_desk_fill(surface, body, TIKU_DESK_C_PANEL);
    tiku_desk_frame(surface, body, dark);
    tiku_desk_bevel(surface, tiku_desk_inset(body, 1),
        tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_MAX),
        tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    tiku_desk_fill(surface, tab, tab_color);
    tiku_desk_frame(surface, tab, dark);
    tiku_desk_bevel(surface, tiku_desk_inset(tab, 1),
        tiku_desk_tint(tab_color, TIKU_DESK_LIGHTEN_1),
        tiku_desk_tint(tab_color, TIKU_DESK_DARKEN_2));
    tiku_desk_hline(surface, tab.x + 1, tab.y + tab.h - 1, tab.w - 2,
                    tab_color);
    draw_button(surface, tab.x + 4, tab.y + (tab_h - btn) / 2, 0, active);
    if (window->zoomable) {
        draw_button(surface, tab.x + tab.w - btn - 4,
                    tab.y + (tab_h - btn) / 2, 1, active);
    }
    {
        tiku_desk_rect_t label = { tab.x + btn + 8, tab.y,
            tab.w - btn - 16 - (window->zoomable ? btn + 8 : 0), tab_h };

        tiku_desk_clip_set(surface, label);
        (void)tiku_desk_text_centered(surface, font, label, window->title,
                                      TIKU_DESK_C_TABTEXT);
        tiku_desk_clip_reset(surface);
    }
    if (window->draw != NULL && content.w > 0 && content.h > 0) {
        tiku_desk_clip_set(surface, content);
        window->draw(window, surface, content, window->context);
        tiku_desk_clip_reset(surface);
    }
    if (window->resizable) {
        int gx = window->frame.x + window->frame.w - 14;
        int gy = window->frame.y + window->frame.h - 14;
        int i;

        for (i = 0; i < 3; i++) {
            tiku_desk_hline(surface, gx + i * 3, gy + 10, 10 - i * 3,
                tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
            tiku_desk_hline(surface, gx + i * 3, gy + 11, 10 - i * 3,
                tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_MAX));
        }
    }
}

void
tiku_desk_workspace_set_backdrop(tiku_desk_workspace_t *workspace,
    void (*draw)(tiku_desk_surface_t *surface, void *context), void *context)
{
    if (workspace != NULL) {
        workspace->backdrop = draw;
        workspace->backdrop_context = context;
    }
}

void
tiku_desk_workspace_draw(tiku_desk_workspace_t *workspace)
{
    tiku_desk_rect_t all;
    int i;

    if (workspace == NULL || workspace->surface == NULL) { return; }
    all = (tiku_desk_rect_t){ 0, 0, workspace->surface->w,
                              workspace->surface->h };
    tiku_desk_fill(workspace->surface, all, TIKU_DESK_C_BACKDROP);
    if (workspace->backdrop != NULL) {
        workspace->backdrop(workspace->surface,
                            workspace->backdrop_context);
    }
    for (i = 0; i < workspace->count; i++) {
        tiku_desk_window_t *window =
            &workspace->windows[workspace->order[i]];

        if (window->open && !window->minimized) {
            draw_window(workspace, window,
                        workspace->order[i] == workspace->focused);
        }
    }
}

void
tiku_desk_window_publish_menus(struct tiku_desk_window *window,
                               const tiku_desk_menuset_t *menus,
                               tiku_desk_menu_pick_fn pick, void *context)
{
    if (window == NULL) {
        return;
    }
    if (menus == NULL) {
        window->has_menus = 0;
        window->menu_pick = NULL;
        window->menu_pick_context = NULL;
        return;
    }
    window->menus = *menus;
    window->has_menus = 1;
    window->menu_pick = pick;
    window->menu_pick_context = context;
}

const tiku_desk_menuset_t *
tiku_desk_window_menus(struct tiku_desk_window *window)
{
    return (window != NULL && window->has_menus) ? &window->menus : NULL;
}
