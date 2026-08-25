/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_slider.c - the dragged value and the stepped one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_slider.h"
#include "tiku_ui.h"
#include "tiku_font.h"

/** @brief The knob is square, so the travel is shorter than the track. */
static int
travel(const tiku_slider_t *sl, tiku_rect_t r)
{
    int knob = r.h;

    (void)sl;
    return (r.w > knob) ? r.w - knob : 0;
}

void
tiku_slider_init(tiku_slider_t *sl, int min, int max, int value, int step)
{
    if (sl == NULL) {
        return;
    }
    memset(sl, 0, sizeof *sl);
    /* A range handed over backwards is taken as the range it describes
     * rather than refused: every answer below then still has a meaning. */
    sl->min = (min <= max) ? min : max;
    sl->max = (min <= max) ? max : min;
    sl->step = (step > 0) ? step : 1;
    (void)tiku_slider_set(sl, value);
}

int
tiku_slider_set(tiku_slider_t *sl, int value)
{
    if (sl == NULL) {
        return 0;
    }
    if (value < sl->min) { value = sl->min; }
    if (value > sl->max) { value = sl->max; }
    sl->value = value;
    return sl->value;
}

int
tiku_slider_step(tiku_slider_t *sl, int dir)
{
    if (sl == NULL) {
        return 0;
    }
    return tiku_slider_set(sl, sl->value + dir * sl->step);
}

float
tiku_slider_fraction(const tiku_slider_t *sl)
{
    int span;

    if (sl == NULL) {
        return 0.0f;
    }
    span = sl->max - sl->min;
    /* A range of one value is wholly at its start, not a division by
     * nothing: a slider over a single choice has nowhere to travel. */
    if (span <= 0) {
        return 0.0f;
    }
    return (float)(sl->value - sl->min) / (float)span;
}

tiku_rect_t
tiku_slider_knob(const tiku_slider_t *sl, tiku_rect_t r)
{
    tiku_rect_t k = r;

    k.w = r.h;
    k.x = r.x + (int)((float)travel(sl, r) * tiku_slider_fraction(sl));
    return k;
}

/** @brief The value a knob centred on @p x would carry. */
static int
value_at(const tiku_slider_t *sl, tiku_rect_t r, int x)
{
    int span = sl->max - sl->min;
    int run = travel(sl, r);
    int at = x - r.x - r.h / 2;

    if (run <= 0 || span <= 0) {
        return sl->min;
    }
    if (at < 0) { at = 0; }
    if (at > run) { at = run; }
    /* Rounded rather than truncated, so dragging to the middle of a
     * coarse range lands on the nearer value instead of always the lower
     * one -- a slider that drifts down as you scrub it is a broken one. */
    return sl->min + (at * span + run / 2) / run;
}

int
tiku_slider_press(tiku_slider_t *sl, tiku_rect_t r, int x)
{
    if (sl == NULL) {
        return 0;
    }
    sl->tracking = 1;
    return tiku_slider_set(sl, value_at(sl, r, x));
}

int
tiku_slider_drag(tiku_slider_t *sl, tiku_rect_t r, int x)
{
    if (sl == NULL || !sl->tracking) {
        return (sl != NULL) ? sl->value : 0;
    }
    return tiku_slider_set(sl, value_at(sl, r, x));
}

void
tiku_slider_release(tiku_slider_t *sl)
{
    if (sl != NULL) {
        sl->tracking = 0;
    }
}

void
tiku_slider_draw(const tiku_slider_t *sl, tiku_surface_t *s, tiku_rect_t r)
{
    tiku_rect_t groove, knob;

    if (sl == NULL || s == NULL) {
        return;
    }
    /* The groove is a thin sunken line down the middle of the track; the
     * knob rides over it, which is what makes the track read as a rail
     * rather than as a well the knob is sunk into. */
    groove = (tiku_rect_t){ r.x + r.h / 2, r.y + r.h / 2 - 2,
                            (r.w > r.h) ? r.w - r.h : 0, 4 };
    tiku_ui_sunken(s, groove, TIKU_C_DOC);
    knob = tiku_slider_knob(sl, r);
    tiku_ui_raised(s, knob);
}

tiku_stepper_hit_t
tiku_stepper_hit(tiku_rect_t r, int x, int y)
{
    int aw = tiku_ui_step_w();
    int ax = r.x + r.w - aw;

    if (x < ax || x >= r.x + r.w || y < r.y || y >= r.y + r.h) {
        return TIKU_STEPPER_HIT_NONE;
    }
    /* The upper half counts up.  Split at the middle, so neither arrow is
     * the easier one to hit. */
    return (y < r.y + r.h / 2) ? TIKU_STEPPER_HIT_UP
                               : TIKU_STEPPER_HIT_DOWN;
}

int
tiku_stepper_press(tiku_slider_t *sl, tiku_rect_t r, int x, int y)
{
    tiku_stepper_hit_t hit = tiku_stepper_hit(r, x, y);
    int was;

    if (sl == NULL || hit == TIKU_STEPPER_HIT_NONE) {
        return 0;
    }
    was = sl->value;
    (void)tiku_slider_step(sl, (hit == TIKU_STEPPER_HIT_UP) ? 1 : -1);
    return sl->value != was;
}

void
tiku_stepper_draw(const tiku_slider_t *sl, tiku_surface_t *s, tiku_rect_t r)
{
    const tiku_font_t *f = tiku_font_plain();
    int aw = tiku_ui_step_w();
    tiku_rect_t field, up, down;
    char digits[24];

    if (sl == NULL || s == NULL) {
        return;
    }
    field = (tiku_rect_t){ r.x, r.y, (r.w > aw) ? r.w - aw : 0, r.h };
    up = (tiku_rect_t){ r.x + r.w - aw, r.y, aw, r.h / 2 };
    down = (tiku_rect_t){ up.x, r.y + r.h / 2, aw, r.h - r.h / 2 };

    tiku_ui_sunken(s, field, TIKU_C_DOC);
    snprintf(digits, sizeof digits, "%d", sl->value);
    /* Right against the arrows, the way a number reads: the digits grow
     * leftward from the place the eye already is. */
    tiku_text(s, f, field.x + field.w - 6 - tiku_text_width(f, digits),
              field.y + (field.h - f->height) / 2 + f->ascent, digits,
              TIKU_C_TEXT);
    tiku_ui_raised(s, up);
    tiku_ui_raised(s, down);
}
