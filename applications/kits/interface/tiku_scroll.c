/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_scroll.c - the scrollbar's own state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_scroll.h"

#include <string.h>

void
tiku_scroll_init(tiku_scroll_t *sb, int step)
{
    if (sb == NULL) {
        return;
    }
    memset(sb, 0, sizeof *sb);
    sb->step = (step > 0) ? step : 1;
}

int
tiku_scroll_max(const tiku_scroll_t *sb)
{
    int max;

    if (sb == NULL) {
        return 0;
    }
    max = sb->total - sb->visible;
    return (max > 0) ? max : 0;
}

int
tiku_scroll_busy(const tiku_scroll_t *sb)
{
    return (sb != NULL && (sb->tracking || sb->detached)) ? 1 : 0;
}

/** @brief Take a deferred range, if one is waiting. */
static void
apply_pending(tiku_scroll_t *sb)
{
    if (!sb->pending || tiku_scroll_busy(sb)) {
        return;
    }
    sb->total = sb->pend_total;
    sb->visible = sb->pend_visible;
    sb->pending = 0;
    (void)tiku_scroll_to(sb, sb->value);
}

int
tiku_scroll_set_range(tiku_scroll_t *sb, int total, int visible)
{
    if (sb == NULL) {
        return 0;
    }
    if (total < 0) { total = 0; }
    if (visible < 0) { visible = 0; }
    if (tiku_scroll_busy(sb)) {
        /* Held, or mid-rebuild: remembered rather than taken.  A range
         * applied under the user's thumb makes the thumb jump out from
         * under it, and one applied mid-rebuild describes a listing that
         * exists for one frame (PVL-052, PVL-060). */
        sb->pend_total = total;
        sb->pend_visible = visible;
        sb->pending = 1;
        return 0;
    }
    sb->total = total;
    sb->visible = visible;
    (void)tiku_scroll_to(sb, sb->value);
    return 1;
}

int
tiku_scroll_to(tiku_scroll_t *sb, int value)
{
    int max;

    if (sb == NULL) {
        return 0;
    }
    max = tiku_scroll_max(sb);
    if (value > max) { value = max; }
    if (value < 0) { value = 0; }
    sb->value = value;
    return sb->value;
}

int
tiku_scroll_line(const tiku_scroll_t *sb)
{
    int half;

    if (sb == NULL) {
        return 1;
    }
    /* Half a line, rounded UP: half of one is not none, and a stepper that
     * moved nothing would be a stepper that looks broken (PVL-051). */
    half = (sb->step + 1) / 2;
    return (half > 0) ? half : 1;
}

int
tiku_scroll_page(const tiku_scroll_t *sb)
{
    int page;

    if (sb == NULL) {
        return 1;
    }
    /* A page keeps one line of what was on screen, so the eye has
     * something to land on across the jump. */
    page = sb->visible - sb->step;
    return (page > 0) ? page : 1;
}

int
tiku_scroll_thumb(const tiku_scroll_t *sb, int len, int *pos,
                      int *thick)
{
    int t, p, max;

    if (pos != NULL)   { *pos = 0; }
    if (thick != NULL) { *thick = len; }
    if (sb == NULL || len <= 0 || sb->total <= 0 ||
        sb->visible >= sb->total) {
        return 0;               /* it all fits: nothing to scroll */
    }
    t = (int)(((long)len * sb->visible) / sb->total);
    if (t < TIKU_SCROLL_MIN_THUMB) {
        /* A thumb proportional to ten thousand rows is a thumb nobody can
         * catch, so the proportion has a floor. */
        t = TIKU_SCROLL_MIN_THUMB;
    }
    if (t > len) {
        t = len;
    }
    max = tiku_scroll_max(sb);
    p = (max > 0) ? (int)(((long)(len - t) * sb->value) / max) : 0;
    if (pos != NULL)   { *pos = p; }
    if (thick != NULL) { *thick = t; }
    return 1;
}

tiku_scroll_hit_t
tiku_scroll_hit(const tiku_scroll_t *sb, int len, int at)
{
    int pos = 0, thick = 0;

    if (!tiku_scroll_thumb(sb, len, &pos, &thick)) {
        return TIKU_SCROLL_HIT_NONE;
    }
    if (at < 0 || at >= len) {
        return TIKU_SCROLL_HIT_NONE;
    }
    if (at < pos) {
        return TIKU_SCROLL_HIT_UP;
    }
    if (at >= pos + thick) {
        return TIKU_SCROLL_HIT_DOWN;
    }
    return TIKU_SCROLL_HIT_THUMB;
}

int
tiku_scroll_press(tiku_scroll_t *sb, int len, int at)
{
    tiku_scroll_hit_t what = tiku_scroll_hit(sb, len, at);
    int pos = 0, thick = 0;

    if (sb == NULL) {
        return 0;
    }
    switch (what) {
    case TIKU_SCROLL_HIT_UP:
        return tiku_scroll_to(sb, sb->value - tiku_scroll_page(sb));
    case TIKU_SCROLL_HIT_DOWN:
        return tiku_scroll_to(sb, sb->value + tiku_scroll_page(sb));
    case TIKU_SCROLL_HIT_THUMB:
        (void)tiku_scroll_thumb(sb, len, &pos, &thick);
        /* Where IN the thumb it was grabbed, so the content does not jump
         * to put the thumb's top under the pointer. */
        sb->grab_off = at - pos;
        sb->tracking = 1;
        return sb->value;
    default:
        return sb->value;
    }
}

int
tiku_scroll_drag(tiku_scroll_t *sb, int len, int at)
{
    int pos, thick = 0, travel, max;

    if (sb == NULL || !sb->tracking) {
        return (sb != NULL) ? sb->value : 0;
    }
    (void)tiku_scroll_thumb(sb, len, &pos, &thick);
    travel = len - thick;
    max = tiku_scroll_max(sb);
    if (travel <= 0 || max <= 0) {
        return sb->value;
    }
    pos = at - sb->grab_off;
    if (pos < 0) { pos = 0; }
    if (pos > travel) { pos = travel; }
    return tiku_scroll_to(sb, (int)(((long)pos * max) / travel));
}

void
tiku_scroll_release(tiku_scroll_t *sb)
{
    if (sb == NULL) {
        return;
    }
    sb->tracking = 0;
    sb->grab_off = 0;
    sb->arrow_dir = 0;
    sb->arrow_pulses = 0;
    apply_pending(sb);
}

void
tiku_scroll_arrow_press(tiku_scroll_t *sb, int dir)
{
    if (sb == NULL) {
        return;
    }
    sb->arrow_dir = (dir < 0) ? -1 : 1;
    sb->arrow_pulses = 0;
}

int
tiku_scroll_arrow_repeat(tiku_scroll_t *sb)
{
    if (sb == NULL || sb->arrow_dir == 0) {
        return 0;
    }
    sb->arrow_pulses++;
    /* An initial hesitation, then a steady walk: the pause is what keeps
     * a plain click from stepping twice. */
    if (sb->arrow_pulses < 12 || (sb->arrow_pulses - 12) % 4 != 0) {
        return 0;
    }
    return sb->arrow_dir;
}

void
tiku_scroll_detach(tiku_scroll_t *sb)
{
    if (sb != NULL) {
        sb->detached = 1;
    }
}

void
tiku_scroll_attach(tiku_scroll_t *sb)
{
    if (sb == NULL) {
        return;
    }
    sb->detached = 0;
    apply_pending(sb);
}
