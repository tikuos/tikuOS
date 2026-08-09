/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display.c - damage tracking over whichever screen backend is built.
 *
 * Clipping and the damage rectangle are the same arithmetic on every part, so
 * they live here; the backend sees only on-screen geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_display.h"
#include "tiku_display_arch.h"

/**
 * @brief Extend the damage rectangle to cover a region, clamped to the screen.
 *
 * @param d  Screen
 * @param x  Region left, may be negative
 * @param y  Region top, may be negative
 * @param w  Region width
 * @param h  Region height
 */
static void
damage_add(tiku_display_t *d, int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;

    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > (int32_t)d->w) { x1 = (int32_t)d->w; }
    if (y1 > (int32_t)d->h) { y1 = (int32_t)d->h; }
    if (x1 <= x0 || y1 <= y0) { return; }   /* fully off-screen / empty */

    if (!d->dirty) {
        d->dx0 = (uint16_t)x0; d->dy0 = (uint16_t)y0;
        d->dx1 = (uint16_t)x1; d->dy1 = (uint16_t)y1;
        d->dirty = 1u;
    } else {
        if ((uint16_t)x0 < d->dx0) { d->dx0 = (uint16_t)x0; }
        if ((uint16_t)y0 < d->dy0) { d->dy0 = (uint16_t)y0; }
        if ((uint16_t)x1 > d->dx1) { d->dx1 = (uint16_t)x1; }
        if ((uint16_t)y1 > d->dy1) { d->dy1 = (uint16_t)y1; }
    }
}

/**
 * @brief Clip a rectangle to the screen for a backend that does not clip.
 *
 * @param d   Screen
 * @param x   In/out left edge
 * @param y   In/out top edge
 * @param w   In/out width
 * @param h   In/out height
 * @return Non-zero when something remains to draw
 */
static int
clip_rect(const tiku_display_t *d, int32_t *x, int32_t *y,
          int32_t *w, int32_t *h)
{
    int32_t x1 = *x + *w, y1 = *y + *h;

    if (*x < 0) { *x = 0; }
    if (*y < 0) { *y = 0; }
    if (x1 > (int32_t)d->w) { x1 = (int32_t)d->w; }
    if (y1 > (int32_t)d->h) { y1 = (int32_t)d->h; }
    *w = x1 - *x;
    *h = y1 - *y;
    return (*w > 0 && *h > 0);
}

/*---------------------------------------------------------------------------*/
/* Lifecycle                                                                 */
/*---------------------------------------------------------------------------*/

int
tiku_display_init(tiku_display_t *d, void *fb, uint16_t w, uint16_t h)
{
    uint16_t bpp;

    if (d == 0 || fb == 0 || w == 0u || h == 0u) {
        return TIKU_DISPLAY_ERR_INVALID;
    }
    d->fmt = (uint8_t)tiku_display_arch_format();
    bpp = (d->fmt == (uint8_t)TIKU_DISPLAY_FMT_RGB565) ? 2u : 4u;

    d->fb     = fb;
    d->front  = fb;             /* single-buffered until paired */
    d->w      = w;
    d->h      = h;
    d->stride = (uint16_t)(w * bpp);
    d->dirty  = 0u;
    d->dx0 = d->dy0 = d->dx1 = d->dy1 = 0u;

    return tiku_display_arch_init(d);
}

uint32_t
tiku_display_caps(void)
{
    return tiku_display_arch_caps();
}

tiku_display_fmt_t
tiku_display_format(void)
{
    return tiku_display_arch_format();
}

void
tiku_display_geometry(uint16_t *w, uint16_t *h)
{
    tiku_display_arch_geometry(w, h);
}

uint16_t
tiku_display_bpp(void)
{
    return (tiku_display_arch_format() == TIKU_DISPLAY_FMT_RGB565) ? 2u : 4u;
}

/*---------------------------------------------------------------------------*/
/* Drawing                                                                   */
/*---------------------------------------------------------------------------*/

int
tiku_display_clear(tiku_display_t *d, uint32_t colour)
{
    if (d == 0 || d->fb == 0) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    damage_add(d, 0, 0, (int32_t)d->w, (int32_t)d->h);
    return tiku_display_arch_fill_rect(d, 0u, 0u, d->w, d->h, colour);
}

int
tiku_display_fill_rect(tiku_display_t *d, int16_t x, int16_t y,
                       uint16_t w, uint16_t h, uint32_t colour)
{
    int32_t cx = x, cy = y, cw = w, ch = h;

    if (d == 0 || d->fb == 0) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    damage_add(d, x, y, (int32_t)w, (int32_t)h);
    if (!clip_rect(d, &cx, &cy, &cw, &ch)) {
        return TIKU_DISPLAY_OK;             /* nothing on screen to draw */
    }
    return tiku_display_arch_fill_rect(d, (uint16_t)cx, (uint16_t)cy,
                                       (uint16_t)cw, (uint16_t)ch, colour);
}

int
tiku_display_fill_circle(tiku_display_t *d, int16_t cx, int16_t cy,
                         uint16_t r, uint32_t colour)
{
    if (d == 0 || d->fb == 0) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    if ((tiku_display_arch_caps() & TIKU_DISPLAY_CAP_CIRCLE) == 0u) {
        return TIKU_DISPLAY_ERR_UNSUPPORTED;
    }
    damage_add(d, (int32_t)cx - (int32_t)r, (int32_t)cy - (int32_t)r,
               (int32_t)r * 2 + 1, (int32_t)r * 2 + 1);
    return tiku_display_arch_fill_circle(d, cx, cy, r, colour);
}

int
tiku_display_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y,
                               uint16_t w, uint16_t h, uint16_t r,
                               uint32_t colour)
{
    if (d == 0 || d->fb == 0) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    if ((tiku_display_arch_caps() & TIKU_DISPLAY_CAP_ROUNDED) == 0u) {
        return TIKU_DISPLAY_ERR_UNSUPPORTED;
    }
    damage_add(d, x, y, (int32_t)w, (int32_t)h);
    return tiku_display_arch_fill_rounded_rect(d, x, y, w, h, r, colour);
}

/*---------------------------------------------------------------------------*/
/* Buffer pairing                                                            */
/*---------------------------------------------------------------------------*/

int
tiku_display_set_buffers(tiku_display_t *d, void *front, void *back)
{
    int rc;

    if (d == 0 || front == 0 || back == 0 || front == back) {
        return TIKU_DISPLAY_ERR_INVALID;
    }
    if ((tiku_display_arch_caps() & TIKU_DISPLAY_CAP_FLIP) == 0u) {
        return TIKU_DISPLAY_ERR_UNSUPPORTED;
    }
    rc = tiku_display_arch_set_scanout(d, front);
    if (rc != TIKU_DISPLAY_OK) {
        return rc;
    }
    d->front = front;
    d->fb    = back;
    d->dirty = 0u;
    return TIKU_DISPLAY_OK;
}

int
tiku_display_flip(tiku_display_t *d)
{
    void *shown;
    int rc;

    if (d == 0 || d->fb == 0) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    if ((tiku_display_arch_caps() & TIKU_DISPLAY_CAP_FLIP) == 0u) {
        return TIKU_DISPLAY_ERR_UNSUPPORTED;
    }
    /* Publish what was drawn before showing it: on a scanning controller
     * that is the cache clean, on a transferring one the transfer itself. */
    rc = tiku_display_flush(d);
    if (rc != TIKU_DISPLAY_OK) {
        return rc;
    }
    rc = tiku_display_arch_set_scanout(d, d->fb);
    if (rc != TIKU_DISPLAY_OK) {
        return rc;
    }
    shown    = d->fb;
    d->fb    = d->front;    /* draw into what has just left the glass */
    d->front = shown;
    return TIKU_DISPLAY_OK;
}

/*---------------------------------------------------------------------------*/
/* Present                                                                   */
/*---------------------------------------------------------------------------*/

int
tiku_display_flush(tiku_display_t *d)
{
    int rc;

    if (d == 0 || d->fb == 0) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    if (!d->dirty) {
        return TIKU_DISPLAY_OK;
    }
    rc = tiku_display_arch_present(d, d->dx0, d->dy0,
                                   (uint16_t)(d->dx1 - d->dx0),
                                   (uint16_t)(d->dy1 - d->dy0));
    d->dirty = 0u;
    return rc;
}

int
tiku_display_damage_bounds(const tiku_display_t *d, uint16_t *x, uint16_t *y,
                           uint16_t *w, uint16_t *h)
{
    if (d == 0 || !d->dirty) {
        return 0;
    }
    if (x != (uint16_t *)0) { *x = d->dx0; }
    if (y != (uint16_t *)0) { *y = d->dy0; }
    if (w != (uint16_t *)0) { *w = (uint16_t)(d->dx1 - d->dx0); }
    if (h != (uint16_t *)0) { *h = (uint16_t)(d->dy1 - d->dy0); }
    return 1;
}
