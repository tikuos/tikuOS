/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_window.h - composited in-process windows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_WINDOW_H_
#define TIKU_DESK_WINDOW_H_

#include "tiku_desk_event.h"
#include "tiku_desk_gfx.h"

#define TIKU_DESK_WINDOW_MAX       16
#define TIKU_DESK_WINDOW_TAB_H     21
#define TIKU_DESK_WINDOW_BORDER     5
#define TIKU_DESK_WINDOW_TITLE_MAX 160

typedef enum {
    TIKU_DESK_HIT_NONE = 0,
    TIKU_DESK_HIT_CONTENT,
    TIKU_DESK_HIT_TAB,
    TIKU_DESK_HIT_CLOSE,
    TIKU_DESK_HIT_ZOOM,
    TIKU_DESK_HIT_RESIZE,
    TIKU_DESK_HIT_BORDER
} tiku_desk_hit_t;

typedef struct tiku_desk_window tiku_desk_window_t;
typedef struct tiku_desk_workspace tiku_desk_workspace_t;

typedef void (*tiku_desk_window_draw_fn)(tiku_desk_window_t *window,
    tiku_desk_surface_t *surface, tiku_desk_rect_t content, void *context);
typedef int (*tiku_desk_window_event_fn)(tiku_desk_window_t *window,
    const tiku_desk_event_t *event, void *context);
typedef int (*tiku_desk_window_close_fn)(tiku_desk_window_t *window,
                                         void *context);
typedef void (*tiku_desk_window_destroy_fn)(tiku_desk_window_t *window,
                                             void *context);

struct tiku_desk_window {
    char                         title[TIKU_DESK_WINDOW_TITLE_MAX];
    tiku_desk_rect_t             frame;
    int                          z;
    int                          open;
    int                          minimized;
    int                          resizable;
    int                          zoomable;
    tiku_desk_window_draw_fn     draw;
    tiku_desk_window_event_fn    event;
    tiku_desk_window_close_fn    close_requested;
    tiku_desk_window_destroy_fn  destroy;
    void                        *context;
    void                        *tag;
    tiku_desk_rect_t             saved_zoom;
    int                          zoom_valid;
};

tiku_desk_workspace_t *tiku_desk_workspace_new(
    tiku_desk_surface_t *surface);
void tiku_desk_workspace_free(tiku_desk_workspace_t *workspace);

tiku_desk_window_t *tiku_desk_workspace_open(
    tiku_desk_workspace_t *workspace, const char *title,
    tiku_desk_rect_t frame, tiku_desk_window_draw_fn draw, void *context,
    void *tag);
tiku_desk_window_t *tiku_desk_workspace_find(
    tiku_desk_workspace_t *workspace, void *tag);
void tiku_desk_workspace_close(tiku_desk_workspace_t *workspace,
                               tiku_desk_window_t *window);
int tiku_desk_workspace_request_close(tiku_desk_workspace_t *workspace,
                                      tiku_desk_window_t *window);
void tiku_desk_workspace_activate(tiku_desk_workspace_t *workspace,
                                  tiku_desk_window_t *window);
tiku_desk_window_t *tiku_desk_workspace_focused(
    tiku_desk_workspace_t *workspace);
int tiku_desk_workspace_count(const tiku_desk_workspace_t *workspace);
tiku_desk_window_t *tiku_desk_workspace_at(
    tiku_desk_workspace_t *workspace, int index);

void tiku_desk_window_set_handlers(tiku_desk_window_t *window,
    tiku_desk_window_event_fn event,
    tiku_desk_window_close_fn close_requested,
    tiku_desk_window_destroy_fn destroy);
int tiku_desk_window_send(tiku_desk_window_t *window,
                          const tiku_desk_event_t *event);
tiku_desk_rect_t tiku_desk_window_content(const tiku_desk_window_t *window);
void tiku_desk_window_set_size_controls(tiku_desk_window_t *window,
                                        int resizable, int zoomable);

tiku_desk_hit_t tiku_desk_workspace_hit(tiku_desk_workspace_t *workspace,
    int x, int y, tiku_desk_window_t **window);
void tiku_desk_workspace_begin_track(tiku_desk_workspace_t *workspace,
    tiku_desk_window_t *window, tiku_desk_hit_t what, int x, int y);
int tiku_desk_workspace_track(tiku_desk_workspace_t *workspace, int x, int y);
void tiku_desk_workspace_end_track(tiku_desk_workspace_t *workspace);
int tiku_desk_workspace_tracking(const tiku_desk_workspace_t *workspace);

void tiku_desk_workspace_draw(tiku_desk_workspace_t *workspace);
void tiku_desk_workspace_set_backdrop(tiku_desk_workspace_t *workspace,
    void (*draw)(tiku_desk_surface_t *surface, void *context), void *context);
int tiku_desk_workspace_zoom(tiku_desk_workspace_t *workspace,
    tiku_desk_window_t *window, int fit_width, int fit_height);
void tiku_desk_workspace_set_reserved(tiku_desk_workspace_t *workspace,
                                      tiku_desk_rect_t reserved);

#endif /* TIKU_DESK_WINDOW_H_ */
