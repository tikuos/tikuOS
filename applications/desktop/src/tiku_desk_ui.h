/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_ui.h - the R5 control set.
 *
 * Immediate-mode drawing: each call paints one control in one state.  Layout
 * and hit-testing belong to the window system (S2), so these stay pure.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_UI_H_
#define TIKU_DESK_UI_H_

#include "tiku_desk_font.h"
#include "tiku_desk_gfx.h"

/** @brief Control states, combinable where it makes sense. */
#define TIKU_DESK_S_NORMAL    0x00u
#define TIKU_DESK_S_PRESSED   0x01u
#define TIKU_DESK_S_FOCUS     0x02u   /* keyboard navigation ring */
#define TIKU_DESK_S_DEFAULT   0x04u   /* the window's default button */
#define TIKU_DESK_S_DISABLED  0x08u
#define TIKU_DESK_S_ON        0x10u   /* checkbox/radio value */

/** @brief Fill @p r with the panel grey. */
void tiku_desk_ui_panel(tiku_desk_surface_t *s, tiku_desk_rect_t r);

/** @brief A raised panel-coloured face with the standard bevel pair. */
void tiku_desk_ui_raised(tiku_desk_surface_t *s, tiku_desk_rect_t r);

/** @brief A sunken document well (list bodies, text fields). */
void tiku_desk_ui_sunken(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                         tiku_desk_rgb_t face);

void tiku_desk_ui_button(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                         const char *label, unsigned state);

void tiku_desk_ui_checkbox(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                           const char *label, unsigned state);

void tiku_desk_ui_radio(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                        const char *label, unsigned state);

/**
 * @brief A menu field: the control a popup menu drops from.
 *
 * Built from the same primitives as a button -- dark frame, clipped corners,
 * the two-step bevel -- because in this look both are raised controls, and a
 * menu field drawn as a plain raised panel sits visibly flatter than the
 * buttons beside it.  The marker is a stroked down arrow, matching the way
 * the menus themselves stroke their submenu arrows.
 */
void tiku_desk_ui_menufield(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                            const char *label, unsigned state);

/** @brief Which widget a window tab is carrying. */
#define TIKU_DESK_TAB_CLOSE  0
#define TIKU_DESK_TAB_ZOOM   1

/**
 * @brief A window tab's close or zoom widget.
 *
 * These are NOT bevelled controls.  Each square is a diagonal gradient from
 * white at its top-left to the tab colour at its bottom-right, inside a
 * single darkened outline -- and the zoom widget is TWO such squares, a small
 * one at the top-left and a larger one offset down and right, overlapping.
 * Drawing it as one square inside another is the thing that makes a window
 * tab read as a checkbox.
 *
 * @param face The tab colour the gradient runs to.
 * @param down Non-zero while pressed, which inverts the gradient.
 */
void tiku_desk_ui_tab_widget(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                             int which, tiku_desk_rgb_t face, int down);

/** @brief A text entry field; @p caret < 0 draws none. */
void tiku_desk_ui_textfield(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                            const char *text, int caret, unsigned state);

/**
 * @brief The same field with a selected RANGE drawn in reverse video.
 *
 * An editor that opens with its whole text selected -- which is how an
 * in-place rename opens, so the first keystroke replaces the old name --
 * needs the selection to be visible, or the user cannot tell that typing is
 * about to discard what is there.
 *
 * @param sel_a,sel_b Character offsets; equal means no selection.
 */
void tiku_desk_ui_textfield_sel(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                                const char *text, int caret, int sel_a,
                                int sel_b, unsigned state);

/**
 * @brief A scrollbar with the knurled thumb.
 *
 * @param pos    Thumb start, 0..1 of the track.
 * @param frac   Thumb length, 0..1 of the track.
 * @param horiz  Non-zero for a horizontal bar.
 */
void tiku_desk_ui_scrollbar(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                            float pos, float frac, int horiz);

/** @brief A menu bar with @p n labels; @p active highlights one (-1 none). */
void tiku_desk_ui_menubar(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                          const char *const *items, int n, int active);

/** @brief A dropped menu panel with @p n items; @p hot highlights one. */
/** @brief Per-row art for a menu; drawn over the row's ground. */
typedef void (*tiku_desk_ui_menu_icon_fn)(tiku_desk_surface_t *s, int index,
                                          int x, int y, int size,
                                          void *context);

/** @brief The menu with an icon gutter; @p icon may be NULL for none. */
void tiku_desk_ui_menu_icons(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                             const char *const *items, int n, int hot,
                             tiku_desk_ui_menu_icon_fn icon, void *context);

void tiku_desk_ui_menu(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                       const char *const *items, int n, int hot);

/** @brief One list row: selected rows take the selection stripe. */
void tiku_desk_ui_list_row(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                           const char *text, int selected);

/** @brief Column headers for a list view. */
void tiku_desk_ui_list_header(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                              const char *const *cols, const int *widths,
                              int n, int sort_col);

/**
 * @brief A window frame with the yellow tab.
 *
 * @param r       The whole window including its border.
 * @param title   Tab text.
 * @param active  Non-zero for the focused window (yellow), else grey.
 * @return The content rectangle inside the frame.
 */
tiku_desk_rect_t tiku_desk_ui_window(tiku_desk_surface_t *s,
                                     tiku_desk_rect_t r, const char *title,
                                     int active);

#endif /* TIKU_DESK_UI_H_ */
