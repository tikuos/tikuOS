/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display.c - minimal GPU-accelerated compositor for tikuOS.
 *
 * See tiku_display.h. Draw operations render into an SSRAM framebuffer via the
 * from-scratch GPU driver and accumulate a bounding damage rectangle; flush()
 * pushes only that rectangle to the panel with a partial-rect DC transfer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_display.h"

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/** Build a GPU surface descriptor for the whole screen framebuffer. */
static tiku_gpu_surface_t
screen_surface(const tiku_display_t *d)
{
    tiku_gpu_surface_t s;
    s.base     = d->fb;
    s.w        = d->w;
    s.h        = d->h;
    s.stride   = d->stride;
    s.format   = TIKU_GPU_FMT_RGBA8888;
    s.sampling = TIKU_GPU_SAMPLE_POINT;
    return s;
}

/** Extend the damage rectangle to cover (x,y,w,h), clamped to the screen. */
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

/*---------------------------------------------------------------------------*/
/* Lifecycle                                                                 */
/*---------------------------------------------------------------------------*/

tiku_dc_err_t
tiku_display_init(tiku_display_t *d, void *fb, uint16_t w, uint16_t h)
{
    tiku_dc_err_t err;

    d->fb     = fb;
    d->w      = w;
    d->h      = h;
    d->stride = (uint16_t)(w * 4u);
    d->dirty  = 0u;
    d->dx0 = d->dy0 = d->dx1 = d->dy1 = 0u;

    if (tiku_gpu_init(TIKU_GPU_PERF_LP_96MHZ) != TIKU_GPU_OK) {
        return TIKU_DC_ERR_POWER;
    }
    err = tiku_dc_init();
    return err;
}

/*---------------------------------------------------------------------------*/
/* Drawing                                                                   */
/*---------------------------------------------------------------------------*/

tiku_gpu_err_t
tiku_display_clear(tiku_display_t *d, uint32_t color)
{
    tiku_gpu_err_t err = tiku_gpu_fill(d->fb, d->w, d->h, d->stride, color);
    damage_add(d, 0, 0, (int32_t)d->w, (int32_t)d->h);
    return err;
}

tiku_gpu_err_t
tiku_display_fill_rect(tiku_display_t *d, int16_t x, int16_t y,
                       uint16_t w, uint16_t h, uint32_t color)
{
    tiku_gpu_surface_t s = screen_surface(d);
    tiku_gpu_err_t err = tiku_gpu_fill_rect(&s, x, y, w, h, color);
    damage_add(d, x, y, (int32_t)w, (int32_t)h);
    return err;
}

tiku_gpu_err_t
tiku_display_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y,
                               uint16_t w, uint16_t h, uint16_t r, uint32_t color)
{
    tiku_gpu_surface_t s = screen_surface(d);
    tiku_gpu_err_t err = tiku_gpu_fill_rounded_rect(&s, x, y, w, h, r, color);
    damage_add(d, x, y, (int32_t)w, (int32_t)h);
    return err;
}

tiku_gpu_err_t
tiku_display_fill_circle(tiku_display_t *d, int16_t cx, int16_t cy,
                         uint16_t r, uint32_t color)
{
    tiku_gpu_surface_t s = screen_surface(d);
    tiku_gpu_err_t err = tiku_gpu_fill_circle(&s, cx, cy, r, color);
    damage_add(d, (int32_t)cx - (int32_t)r, (int32_t)cy - (int32_t)r,
               (int32_t)r * 2 + 1, (int32_t)r * 2 + 1);
    return err;
}

tiku_gpu_err_t
tiku_display_blit(tiku_display_t *d, const tiku_gpu_surface_t *src,
                  int16_t x, int16_t y, tiku_gpu_blend_t blend)
{
    tiku_gpu_surface_t s = screen_surface(d);
    tiku_gpu_err_t err = tiku_gpu_blit(&s, src, x, y, blend);
    damage_add(d, x, y, (int32_t)src->w, (int32_t)src->h);
    return err;
}

/*---------------------------------------------------------------------------*/
/* Present                                                                   */
/*---------------------------------------------------------------------------*/

tiku_dc_err_t
tiku_display_flush(tiku_display_t *d)
{
    tiku_dc_err_t err;

    if (!d->dirty) {
        return TIKU_DC_OK;
    }
    err = tiku_dc_present_rect(d->fb, d->stride, d->dx0, d->dy0,
                               (uint16_t)(d->dx1 - d->dx0),
                               (uint16_t)(d->dy1 - d->dy0),
                               TIKU_DC_FMT_RGBA8888);
    d->dirty = 0u;
    return err;
}

int
tiku_display_damage_bounds(const tiku_display_t *d, uint16_t *x, uint16_t *y,
                           uint16_t *w, uint16_t *h)
{
    if (!d->dirty) {
        return 0;
    }
    if (x != (uint16_t *)0) { *x = d->dx0; }
    if (y != (uint16_t *)0) { *y = d->dy0; }
    if (w != (uint16_t *)0) { *w = (uint16_t)(d->dx1 - d->dx0); }
    if (h != (uint16_t *)0) { *h = (uint16_t)(d->dy1 - d->dy0); }
    return 1;
}
