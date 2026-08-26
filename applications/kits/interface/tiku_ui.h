/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ui.h - the R5 control set.
 *
 * Immediate-mode drawing: each call paints one control in one state.  Layout
 * and hit-testing belong to the window system (S2), so these stay pure.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_UI_H_
#define TIKU_UI_H_

#include "tiku_font.h"
#include "tiku_gfx.h"

/*---------------------------------------------------------------------------*/
/* How big a thing has to be, given the letters that go in it.               */
/*                                                                          */
/* These used to be written down.  A list row was 22 pixels because 22 was  */
/* what a 12-pixel face needed, and a stepper's halves were split at pixel  */
/* 300 because that was where its plus sign happened to land.  Then the     */
/* face became a preference and the numbers stayed -- so at 16 the words no */
/* longer fitted their rows, and at 10 the plus sign moved LEFT of the      */
/* split and every click on it was read as a minus.  A control that is not  */
/* where the click is looked for is not a control.                          */
/*                                                                          */
/* So: ask.  Each answers exactly what was written down before at 12 px,    */
/* which is the size the interface has always defaulted to, and the right   */
/* number at every other.                                                   */
/*---------------------------------------------------------------------------*/

/** @brief One row of a list, with room above and below the letters. */
int tiku_ui_row_h(void);

/** @brief A push button's height. */
int tiku_ui_button_h(void);

/**
 * @brief The width a button needs to hold @p label.
 *
 * @param least A floor, for a row of buttons that should match rather than
 *              each shrink to its own word.  0 for none.
 */
int tiku_ui_button_w(const char *label, int least);

/** @brief A stepper's square half -- the - or the + of a value control. */
int tiku_ui_step_w(void);

/** @brief Control states, combinable where it makes sense. */
#define TIKU_S_NORMAL    0x00u
#define TIKU_S_PRESSED   0x01u
#define TIKU_S_FOCUS     0x02u   /* keyboard navigation ring */
#define TIKU_S_DEFAULT   0x04u   /* the window's default button */
#define TIKU_S_DISABLED  0x08u
#define TIKU_S_ON        0x10u   /* checkbox/radio value */

/** @brief Fill @p r with the panel grey. */
void tiku_ui_panel(tiku_surface_t *s, tiku_rect_t r);

/** @brief A raised panel-coloured face with the standard bevel pair. */
void tiku_ui_raised(tiku_surface_t *s, tiku_rect_t r);

/** @brief A sunken document well (list bodies, text fields). */
void tiku_ui_sunken(tiku_surface_t *s, tiku_rect_t r,
                         tiku_rgb_t face);

void tiku_ui_button(tiku_surface_t *s, tiku_rect_t r,
                         const char *label, unsigned state);

void tiku_ui_checkbox(tiku_surface_t *s, tiku_rect_t r,
                           const char *label, unsigned state);

void tiku_ui_radio(tiku_surface_t *s, tiku_rect_t r,
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
void tiku_ui_menufield(tiku_surface_t *s, tiku_rect_t r,
                            const char *label, unsigned state);

/** @brief Which widget a window tab is carrying. */
#define TIKU_TAB_CLOSE  0
#define TIKU_TAB_ZOOM   1

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
void tiku_ui_tab_widget(tiku_surface_t *s, tiku_rect_t r,
                             int which, tiku_rgb_t face, int down);

/** @brief A text entry field; @p caret < 0 draws none. */
void tiku_ui_textfield(tiku_surface_t *s, tiku_rect_t r,
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
void tiku_ui_textfield_sel(tiku_surface_t *s, tiku_rect_t r,
                                const char *text, int caret, int sel_a,
                                int sel_b, unsigned state);

/**
 * @brief The selectable field, with its text slid left by @p scroll_px.
 *
 * For an editor that keeps its caret visible when the text outgrows the
 * box: the caller decides the slide, this draws it.
 */
void tiku_ui_textfield_scroll(tiku_surface_t *s,
                                   tiku_rect_t r, const char *text,
                                   int caret, int sel_a, int sel_b,
                                   unsigned state, int scroll_px);

/**
 * @brief A scrollbar with the knurled thumb.
 *
 * @param pos    Thumb start, 0..1 of the track.
 * @param frac   Thumb length, 0..1 of the track.
 * @param horiz  Non-zero for a horizontal bar.
 */
/**
 * @brief A gauge: how much of something is done or in use, as a bar.
 *
 * A sunken well with a proportional fill, which is the shape a bar is
 * read in -- it fills toward full.  @p fraction is clamped to 0..1, so a
 * caller handing over a ratio of counts does not have to guard the
 * empty-denominator case before drawing.
 *
 * No state: a gauge shows a number somebody else owns.  Whether that is
 * bytes copied, memory in use or steps taken is the caller's business.
 */
/**
 * @brief The little triangle a stepper or a scrollbar end wears.
 *
 * @p dir is 0 up, 1 down, 2 left, 3 right; @p enabled false fades the
 * glyph toward the panel.  That fade is the rule the scrollbar has
 * always kept and the one thing an arrow owes the person pressing it: an
 * arrow that cannot go any further has to SAY so, or it is a control
 * that looks live and does nothing.
 *
 * The glyph only -- whatever chrome it sits on is the caller's, because
 * a scrollbar end is a bevelled button and a stepper's is a raised one.
 */
void tiku_ui_arrow(tiku_surface_t *s, tiku_rect_t r, int dir, int enabled);

void tiku_ui_gauge(tiku_surface_t *s, tiku_rect_t r, float fraction);

/**
 * @brief How wide and tall a tip saying @p text needs to be.
 *
 * Given so a caller can place the box before drawing it -- a tip is put
 * BESIDE the thing it explains, so where it goes depends on how big it
 * is, and that sum should not be spelled out again at every call.
 */
tiku_rect_t tiku_ui_tip_size(const char *text);

/**
 * @brief A tip: the whole of something said beside what could not fit it.
 *
 * The note-coloured box every hover explanation wears, framed and with
 * its text inset.  @p r is where it goes -- from tiku_ui_tip_size(),
 * moved to sit beside the row or cell it belongs to; a caller that has
 * pushed it back on-screen keeps whatever it decided.
 */
void tiku_ui_tip(tiku_surface_t *s, tiku_rect_t r, const char *text);

void tiku_ui_scrollbar(tiku_surface_t *s, tiku_rect_t r,
                            float pos, float frac, int horiz);

/** @brief A menu bar with @p n labels; @p active highlights one (-1 none). */
void tiku_ui_menubar(tiku_surface_t *s, tiku_rect_t r,
                          const char *const *items, int n, int active);

/** @brief A dropped menu panel with @p n items; @p hot highlights one. */
/** @brief Per-row art for a menu; drawn over the row's ground. */
typedef void (*tiku_ui_menu_icon_fn)(tiku_surface_t *s, int index,
                                          int x, int y, int size,
                                          void *context);

/** @brief The menu with an icon gutter; @p icon may be NULL for none. */
void tiku_ui_menu_icons(tiku_surface_t *s, tiku_rect_t r,
                             const char *const *items, int n, int hot,
                             tiku_ui_menu_icon_fn icon, void *context);

void tiku_ui_menu(tiku_surface_t *s, tiku_rect_t r,
                       const char *const *items, int n, int hot);

/** @brief One list row: selected rows take the selection stripe. */
/**
 * @brief The mark on a menu row that opens a submenu.
 *
 * A small right-pointing triangle, set against the right edge of @p item
 * and centred in it.  Drawn as horizontal runs rather than as a stroked
 * outline: a two-pixel pen loses its point at a twelve-pixel row, and a
 * run is a command the display list can carry, where a plotted pixel is
 * not -- so a menu sent down a line arrives with its arrows on.
 */
void tiku_ui_submenu_arrow(tiku_surface_t *s, tiku_rect_t item,
                           tiku_rgb_t c);

void tiku_ui_list_row(tiku_surface_t *s, tiku_rect_t r,
                           const char *text, int selected);

/** @brief Column headers for a list view. */
void tiku_ui_list_header(tiku_surface_t *s, tiku_rect_t r,
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
tiku_rect_t tiku_ui_window(tiku_surface_t *s,
                                     tiku_rect_t r, const char *title,
                                     int active);

/**
 * @brief Draw @p text at @p x, @p y (baseline) as runs that MEAN things.
 *
 * Each run takes its shade from tiku_ink(), so a highlighted line
 * re-themes with everything else and carries no palette of its own.  A
 * run table shorter than the line leaves the tail plain, and a NULL or
 * empty one draws the whole line plain -- so a caller may pass whatever
 * the classifier gave it without checking first.
 *
 * The runs are drawn as separate text calls, which is exactly how they
 * cross the wire: one command per run, each carrying its own colour, the
 * way every other coloured thing on this desktop travels.  A remote
 * window is therefore highlighted without the protocol learning a word
 * about syntax.  The cost is honest and worth naming: a narrated line
 * arrives as several text facts rather than one.
 *
 * @return the total advance, so a caller can place what follows.
 */
/**
 * @brief A frame around a block of controls, with @p label notched into
 *        its top edge.
 *
 * The etched box the toolkit never had: two hairlines, light under
 * dark, so it reads as a groove rather than as another raised panel --
 * a group is a boundary, not a surface, and a raised one would fight
 * the controls standing on it.  The label sits in a gap cut in the top
 * line, which is what makes the line read as belonging to the words.
 *
 * @return the rect INSIDE the frame, so a caller lays its controls in
 *         what came back rather than working out the inset twice.
 */
tiku_rect_t tiku_ui_group(tiku_surface_t *s, tiku_rect_t r,
                               const char *label);

int tiku_ui_text_spans(tiku_surface_t *s, const tiku_font_t *f,
                            int x, int y, const char *text,
                            const tiku_span_t *span, int n);

#endif /* TIKU_UI_H_ */
