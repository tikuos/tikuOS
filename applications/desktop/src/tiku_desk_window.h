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

/*
 * The published-menu protocol (phase two of the global menu bar): a window
 * DESCRIBES its menus as plain data, and whoever owns the top of the
 * screen renders whichever description the focused window published.  The
 * publisher never draws and the bar owner never reaches into the
 * publisher's widgets -- the description is the whole contract, which is
 * what will let a device application's menus appear in a bar it does not
 * own.  Entries at level 1 belong to the submenu of the nearest level-0
 * entry above them.
 */
#define TIKU_DESK_MENUSET_MENUS 6
#define TIKU_DESK_MENUSET_ITEMS 28
#define TIKU_DESK_MENUSET_LABEL 44

typedef struct {
    char          label[TIKU_DESK_MENUSET_LABEL];
    int           command;
    char          sc;           /* shortcut character, 0 for none        */
    unsigned      mods;
    unsigned char enabled;
    unsigned char marked;
    unsigned char separator;
    unsigned char level;        /* 0 top, 1 inside the submenu above     */
} tiku_desk_menu_entry_t;

typedef struct {
    char                   title[TIKU_DESK_MENUSET_LABEL];
    int                    nitem;
    tiku_desk_menu_entry_t item[TIKU_DESK_MENUSET_ITEMS];
} tiku_desk_menu_list_t;

typedef struct {
    int                   nmenu;
    tiku_desk_menu_list_t menu[TIKU_DESK_MENUSET_MENUS];
} tiku_desk_menuset_t;

struct tiku_desk_window;
typedef void (*tiku_desk_menu_pick_fn)(struct tiku_desk_window *window,
                                       int command, void *context);

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
    /* What the window says its menus are (the publish protocol above);
     * has_menus 0 means it says nothing and the bar shows only its own. */
    tiku_desk_menuset_t          menus;
    int                          has_menus;
    tiku_desk_menu_pick_fn       menu_pick;
    void                        *menu_pick_context;
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

/** @brief Publish (or clear, with NULL) the window's menu description. */
void tiku_desk_window_publish_menus(struct tiku_desk_window *window,
                                    const tiku_desk_menuset_t *menus,
                                    tiku_desk_menu_pick_fn pick,
                                    void *context);

/** @brief The published description, or NULL when there is none. */
const tiku_desk_menuset_t *tiku_desk_window_menus(
    struct tiku_desk_window *window);
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
