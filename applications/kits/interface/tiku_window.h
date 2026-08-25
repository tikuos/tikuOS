/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_window.h - composited in-process windows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WINDOW_H_
#define TIKU_WINDOW_H_

#include "tiku_event.h"
#include "tiku_gfx.h"

#define TIKU_WINDOW_MAX       16
#define TIKU_WINDOW_BORDER     5
#define TIKU_WINDOW_TITLE_MAX 160

/**
 * @brief How tall a window's tab is, for the face in force.
 *
 * It was 21 pixels, written down, which is what a 12-pixel bold face
 * needs and what nothing else does: at 16 the title was drawn into a
 * strip too short for it.  A tab is as tall as its title plus room, so
 * it is asked for rather than assumed -- and at 12 px it still answers
 * 21, which is why nothing that was pinned against it has moved.
 *
 * Every layout that reserves space for the tab must CALL this, including
 * the ones that only add it up to size a frame; a caller that keeps its
 * own copy is the bug this replaces.
 */
int tiku_window_tab_h(void);

/** @brief The side of a tab's close or zoom button, for that face. */
int tiku_window_tab_button(void);

typedef enum {
    TIKU_HIT_NONE = 0,
    TIKU_HIT_CONTENT,
    TIKU_HIT_TAB,
    TIKU_HIT_CLOSE,
    TIKU_HIT_ZOOM,
    TIKU_HIT_RESIZE,
    TIKU_HIT_BORDER
} tiku_hit_t;

typedef struct tiku_window tiku_window_t;
typedef struct tiku_workspace tiku_workspace_t;

typedef void (*tiku_window_draw_fn)(tiku_window_t *window,
    tiku_surface_t *surface, tiku_rect_t content, void *context);
typedef int (*tiku_window_event_fn)(tiku_window_t *window,
    const tiku_event_t *event, void *context);
typedef int (*tiku_window_close_fn)(tiku_window_t *window,
                                         void *context);
typedef void (*tiku_window_destroy_fn)(tiku_window_t *window,
                                             void *context);

/*
 * The published-menu protocol (phase two of the global menu bar): a window
 * DESCRIBES its menus as plain data, and whoever owns the top of the
 * screen renders whichever description the focused window published.  The
 * publisher never draws and the bar owner never reaches into the
 * publisher's widgets -- the description is the whole contract, which is
 * what will let a device application's menus appear in a bar it does not
 * own.  One level of submenu is all this carries, and deliberately: a
 * published menu is a description an application hands over, not a tree
 * it drives, and every menu any application here has wanted is one deep.
 * Entries at level 1 belong to the submenu of the nearest level-0
 * entry above them.
 */
#define TIKU_MENUSET_MENUS 6
#define TIKU_MENUSET_ITEMS 28
#define TIKU_MENUSET_LABEL 44

typedef struct {
    char          label[TIKU_MENUSET_LABEL];
    int           command;
    char          sc;           /* shortcut character, 0 for none        */
    unsigned      mods;
    unsigned char enabled;
    unsigned char marked;
    unsigned char separator;
    unsigned char level;        /* 0 top, 1 inside the submenu above     */
} tiku_menu_entry_t;

typedef struct {
    char                   title[TIKU_MENUSET_LABEL];
    int                    nitem;
    tiku_menu_entry_t item[TIKU_MENUSET_ITEMS];
} tiku_menu_list_t;

typedef struct {
    int                   nmenu;
    tiku_menu_list_t menu[TIKU_MENUSET_MENUS];
} tiku_menuset_t;

struct tiku_window;
typedef void (*tiku_menu_pick_fn)(struct tiku_window *window,
                                       int command, void *context);

struct tiku_window {
    char                         title[TIKU_WINDOW_TITLE_MAX];
    tiku_rect_t             frame;
    int                          z;
    int                          open;
    int                          minimized;
    int                          resizable;
    int                          zoomable;
    tiku_window_draw_fn     draw;
    tiku_window_event_fn    event;
    tiku_window_close_fn    close_requested;
    tiku_window_destroy_fn  destroy;
    void                        *context;
    void                        *tag;
    /* What the window says its menus are (the publish protocol above);
     * has_menus 0 means it says nothing and the bar shows only its own. */
    tiku_menuset_t          menus;
    int                          has_menus;
    tiku_menu_pick_fn       menu_pick;
    void                        *menu_pick_context;
    tiku_rect_t             saved_zoom;
    int                          zoom_valid;
};

tiku_workspace_t *tiku_workspace_new(
    tiku_surface_t *surface);
void tiku_workspace_free(tiku_workspace_t *workspace);

tiku_window_t *tiku_workspace_open(
    tiku_workspace_t *workspace, const char *title,
    tiku_rect_t frame, tiku_window_draw_fn draw, void *context,
    void *tag);
tiku_window_t *tiku_workspace_find(
    tiku_workspace_t *workspace, void *tag);
void tiku_workspace_close(tiku_workspace_t *workspace,
                               tiku_window_t *window);
int tiku_workspace_request_close(tiku_workspace_t *workspace,
                                      tiku_window_t *window);
void tiku_workspace_activate(tiku_workspace_t *workspace,
                                  tiku_window_t *window);
tiku_window_t *tiku_workspace_focused(
    tiku_workspace_t *workspace);
int tiku_workspace_count(const tiku_workspace_t *workspace);
tiku_window_t *tiku_workspace_at(
    tiku_workspace_t *workspace, int index);

void tiku_window_set_handlers(tiku_window_t *window,
    tiku_window_event_fn event,
    tiku_window_close_fn close_requested,
    tiku_window_destroy_fn destroy);
int tiku_window_send(tiku_window_t *window,
                          const tiku_event_t *event);
tiku_rect_t tiku_window_content(const tiku_window_t *window);

/** @brief Publish (or clear, with NULL) the window's menu description. */
void tiku_window_publish_menus(struct tiku_window *window,
                                    const tiku_menuset_t *menus,
                                    tiku_menu_pick_fn pick,
                                    void *context);

/** @brief The published description, or NULL when there is none. */
const tiku_menuset_t *tiku_window_menus(
    struct tiku_window *window);
void tiku_window_set_size_controls(tiku_window_t *window,
                                        int resizable, int zoomable);

tiku_hit_t tiku_workspace_hit(tiku_workspace_t *workspace,
    int x, int y, tiku_window_t **window);
void tiku_workspace_begin_track(tiku_workspace_t *workspace,
    tiku_window_t *window, tiku_hit_t what, int x, int y);
int tiku_workspace_track(tiku_workspace_t *workspace, int x, int y);
void tiku_workspace_end_track(tiku_workspace_t *workspace);
int tiku_workspace_tracking(const tiku_workspace_t *workspace);

void tiku_workspace_draw(tiku_workspace_t *workspace);

/**
 * @brief Say what @p window shows, as the plain lines of
 *        tiku_dl_narrate(), into @p out.
 *
 * The window's content is drawn once onto a recording surface of the
 * workspace's size -- never the shown one, so answering does not repaint
 * what it observes -- and the recorded list is narrated with no pixel
 * consulted.  A window that strokes marks the wire cannot carry gets a
 * closing line saying so, because a narration that silently omits what
 * was drawn is worse than one that confesses.
 *
 * This is the reader's door: an assistive reader, an agent, or a test on
 * the far end of the conductor channel hears a window it has never seen
 * pixels of.
 *
 * @return the bytes written, 0 (and an empty @p out) when there is
 *         nothing to narrate -- no window, no draw, no room.
 */
size_t tiku_workspace_narrate(tiku_workspace_t *workspace,
                                   tiku_window_t *window,
                                   char *out, size_t max);
void tiku_workspace_set_backdrop(tiku_workspace_t *workspace,
    void (*draw)(tiku_surface_t *surface, void *context), void *context);
int tiku_workspace_zoom(tiku_workspace_t *workspace,
    tiku_window_t *window, int fit_width, int fit_height);
void tiku_workspace_set_reserved(tiku_workspace_t *workspace,
                                      tiku_rect_t reserved);

#endif /* TIKU_WINDOW_H_ */
