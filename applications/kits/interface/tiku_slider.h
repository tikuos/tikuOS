/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_slider.h - a value dragged along a track, and a number stepped by
 * a pair of arrows.
 *
 * Both are the same idea wearing different clothes: a number with a
 * floor, a ceiling and a step, which the user changes by pointing at it.
 * The slider is for a quantity whose FEEL matters more than its digits
 * -- a volume, a delay -- and the stepper for one whose digits are the
 * point.  The state is here because a drag is a state machine: where the
 * grab started, what the value was then, and whether the pointer is
 * still down.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SLIDER_H_
#define TIKU_SLIDER_H_

#include "tiku_gfx.h"

typedef struct {
    int min, max;
    int value;
    int step;          /* one arrow click, or one arrow key */
    int tracking;      /* a press is being followed         */
} tiku_slider_t;

/** @brief Set up over @p min..@p max, starting at @p value. */
void tiku_slider_init(tiku_slider_t *sl, int min, int max, int value,
                      int step);

/** @brief Set the value, clamped to the range.  @return the value. */
int tiku_slider_set(tiku_slider_t *sl, int value);

/** @brief Step by @p dir steps (-1 or +1), clamped.  @return the value. */
int tiku_slider_step(tiku_slider_t *sl, int dir);

/** @brief How far along the range the value sits, 0..1. */
float tiku_slider_fraction(const tiku_slider_t *sl);

/**
 * @brief Where the knob sits in a horizontal track @p r.
 *
 * Square, the track's height, and inset so the knob's travel keeps it
 * wholly inside the track at both ends rather than half off it.
 */
tiku_rect_t tiku_slider_knob(const tiku_slider_t *sl, tiku_rect_t r);

/**
 * @brief Take a press at @p x in track @p r: jump to it and grab.
 *
 * A press anywhere on the track moves the value there, the way a
 * scrollbar's page area does, and leaves the knob held so the same
 * gesture carries on as a drag.  @return the value.
 */
int tiku_slider_press(tiku_slider_t *sl, tiku_rect_t r, int x);

/** @brief Drag to @p x while held.  @return the value. */
int tiku_slider_drag(tiku_slider_t *sl, tiku_rect_t r, int x);

/** @brief Let go. */
void tiku_slider_release(tiku_slider_t *sl);

/** @brief Draw the track with its knob at the value. */
void tiku_slider_draw(const tiku_slider_t *sl, tiku_surface_t *s,
                      tiku_rect_t r);

/*-------------------------------------------------------------------------*
 * The stepper: the same number, shown as digits with two arrows.
 *-------------------------------------------------------------------------*/

/** @brief Which half of a stepper a point is on. */
typedef enum {
    TIKU_STEPPER_HIT_NONE = 0,
    TIKU_STEPPER_HIT_UP,
    TIKU_STEPPER_HIT_DOWN
} tiku_stepper_hit_t;

/** @brief Where a point at @p x,@p y lands on the stepper drawn in @p r. */
tiku_stepper_hit_t tiku_stepper_hit(tiku_rect_t r, int x, int y);

/**
 * @brief Take a press on the stepper: step the value if it hit an arrow.
 * @return nonzero when the value changed.
 */
int tiku_stepper_press(tiku_slider_t *sl, tiku_rect_t r, int x, int y);

/** @brief Draw @p sl as a field of digits with its two arrows. */
void tiku_stepper_draw(const tiku_slider_t *sl, tiku_surface_t *s,
                       tiku_rect_t r);

#endif /* TIKU_SLIDER_H_ */
