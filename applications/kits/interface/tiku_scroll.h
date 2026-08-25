/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_scroll.h - a scrollbar that is a THING, not a drawing.
 *
 * The state behind tiku_ui_scrollbar(), which paints one: any
 * application with content longer than its window needs exactly this
 * object, which is why it lives here rather than in the shells.
 *
 * The port's bar was a pure function of the view evaluated per frame: it
 * could be looked at and never touched, and there was nothing to hold a
 * range steady while the user dragged it or while a bulk re-layout moved
 * the content underneath.  This is the object those rows are about -- a
 * range, a thumb, and the two states that make it more than arithmetic:
 * TRACKING (a button is held on it) and DETACHED (the view is being
 * rebuilt).  Both defer the range they are handed rather than dropping it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SCROLL_H_
#define TIKU_SCROLL_H_

/** @brief The smallest thumb that can still be grabbed. */
#define TIKU_SCROLL_MIN_THUMB 16

typedef struct {
    int total;            /* the whole content, in units             */
    int visible;          /* how much of it is on screen             */
    int value;            /* the first unit shown                    */
    int step;             /* one line, in units                      */
    /* A range handed over while the bar could not take it, and whether
     * there is one.  Deferred rather than dropped: the content really did
     * change, and forgetting it would leave the bar describing a listing
     * that is no longer there. */
    int pend_total, pend_visible;
    int pending;
    int tracking;         /* a button is held on the bar             */
    /* A held ARROW repeats its step (NeXT UIG ch3: wherever clicking
     * repeats an action, pressing repeats it automatically).  Counted in
     * loop pulses so a script can hold an arrow deterministically. */
    int arrow_dir;        /* -1 back, +1 on, 0 when no arrow is held */
    int arrow_pulses;
    int detached;         /* a bulk re-layout is in progress         */
    int grab_off;         /* where in the thumb the grab landed      */
} tiku_scroll_t;

/** @brief Start empty: no content, nothing shown, at the top. */
void tiku_scroll_init(tiku_scroll_t *sb, int step);

/**
 * @brief Tell it what there is to scroll (PVL-052).
 *
 * Applied at once when the bar is idle; held while a button is down or
 * while the view is being rebuilt, and applied when that ends.
 *
 * @return 1 when it took effect now.
 */
int tiku_scroll_set_range(tiku_scroll_t *sb, int total, int visible);

/** @brief Move to @p value, clamped to what there is to show. */
int tiku_scroll_to(tiku_scroll_t *sb, int value);

/** @brief An arrow was pressed and is now held. */
void tiku_scroll_arrow_press(tiku_scroll_t *sb, int dir);

/**
 * @brief One loop pulse with the arrow still held.
 *
 * @return the direction to step, or 0 when no repeat is due yet -- the
 *         first repeat waits, the rest come steadily.
 */
int tiku_scroll_arrow_repeat(tiku_scroll_t *sb);

/** @brief The largest value that still shows content. */
int tiku_scroll_max(const tiku_scroll_t *sb);

/**
 * @brief Where the thumb sits in a track @p len long (PVL-051).
 *
 * Sized by the visible fraction and never smaller than a grab: a thumb
 * proportional to a thousand rows is a thumb nobody can catch.
 *
 * @return 1 when there is anything to scroll at all.
 */
int tiku_scroll_thumb(const tiku_scroll_t *sb, int len, int *pos,
                          int *thick);

/** @brief One click of a stepper: half a line, rounded up (PVL-051). */
int tiku_scroll_line(const tiku_scroll_t *sb);

/** @brief One page: what is visible, less an overlap of one line. */
int tiku_scroll_page(const tiku_scroll_t *sb);

/** @brief What a press at @p at in a track @p len long asks for. */
typedef enum {
    TIKU_SCROLL_HIT_NONE = 0,
    TIKU_SCROLL_HIT_UP,       /* before the thumb: a page back    */
    TIKU_SCROLL_HIT_DOWN,     /* after it: a page on              */
    TIKU_SCROLL_HIT_THUMB
} tiku_scroll_hit_t;

tiku_scroll_hit_t tiku_scroll_hit(const tiku_scroll_t *sb,
                                          int len, int at);

/**
 * @brief Press at @p at: page, or take hold of the thumb.
 *
 * @return the new value.
 */
int tiku_scroll_press(tiku_scroll_t *sb, int len, int at);

/** @brief Drag to @p at while holding the thumb.  @return the new value. */
int tiku_scroll_drag(tiku_scroll_t *sb, int len, int at);

/** @brief Let go: any range that arrived while held is applied now. */
void tiku_scroll_release(tiku_scroll_t *sb);

/**
 * @brief Detach for a bulk re-layout, and attach again after (PVL-060).
 *
 * A mode switch, a clean-up or a folder switch moves the content through
 * intermediate shapes; a bar that took each of them would feed positions
 * back into a view that is halfway rebuilt.
 */
void tiku_scroll_detach(tiku_scroll_t *sb);
void tiku_scroll_attach(tiku_scroll_t *sb);

/** @brief Whether it is detached or held, i.e. not taking ranges. */
int tiku_scroll_busy(const tiku_scroll_t *sb);

#endif /* TIKU_SCROLL_H_ */
