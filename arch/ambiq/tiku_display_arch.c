/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_arch.c - the Apollo510 screen behind interfaces/display.
 *
 * The Nema-class GPU draws into the framebuffer and the display controller
 * transfers a rectangle of it to the panel, so presenting costs a transfer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/display/tiku_display_arch.h>

#include "tiku_gpu_arch.h"
#include "tiku_dc_arch.h"

/**
 * @brief Describe the bound framebuffer the way the GPU expects a surface.
 *
 * @param d  Screen
 * @return Surface covering the whole framebuffer
 */
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

/**
 * @brief Map a GPU outcome onto the portable one.
 *
 * @param e  GPU error
 * @return TIKU_DISPLAY_OK, or a negative error
 */
static int
of_gpu(tiku_gpu_err_t e)
{
    return (e == TIKU_GPU_OK) ? TIKU_DISPLAY_OK : TIKU_DISPLAY_ERR_STATE;
}

int
tiku_display_arch_init(tiku_display_t *d)
{
    (void)d;
    if (tiku_gpu_init(TIKU_GPU_PERF_LP_96MHZ) != TIKU_GPU_OK) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    return (tiku_dc_init() == TIKU_DC_OK) ? TIKU_DISPLAY_OK
                                          : TIKU_DISPLAY_ERR_STATE;
}

uint32_t
tiku_display_arch_caps(void)
{
    return TIKU_DISPLAY_CAP_CIRCLE | TIKU_DISPLAY_CAP_ROUNDED |
           TIKU_DISPLAY_CAP_FLIP;
}

tiku_display_fmt_t
tiku_display_arch_format(void)
{
    return TIKU_DISPLAY_FMT_RGBA8888;
}

void
tiku_display_arch_geometry(uint16_t *w, uint16_t *h)
{
    if (w != (uint16_t *)0) { *w = TIKU_DC_PANEL_W; }
    if (h != (uint16_t *)0) { *h = TIKU_DC_PANEL_H; }
}

int
tiku_display_arch_fill_rect(tiku_display_t *d, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h, uint32_t colour)
{
    /* A whole-screen fill has its own faster path on this engine. */
    if (x == 0u && y == 0u && w == d->w && h == d->h) {
        return of_gpu(tiku_gpu_fill(d->fb, d->w, d->h, d->stride, colour));
    }
    {
        tiku_gpu_surface_t s = screen_surface(d);

        return of_gpu(tiku_gpu_fill_rect(&s, (int16_t)x, (int16_t)y, w, h,
                                         colour));
    }
}

int
tiku_display_arch_fill_circle(tiku_display_t *d, int16_t cx, int16_t cy,
                              uint16_t r, uint32_t colour)
{
    tiku_gpu_surface_t s = screen_surface(d);

    return of_gpu(tiku_gpu_fill_circle(&s, cx, cy, r, colour));
}

int
tiku_display_arch_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y,
                                    uint16_t w, uint16_t h, uint16_t r,
                                    uint32_t colour)
{
    tiku_gpu_surface_t s = screen_surface(d);

    return of_gpu(tiku_gpu_fill_rounded_rect(&s, x, y, w, h, r, colour));
}

int
tiku_display_arch_set_scanout(tiku_display_t *d, void *fb)
{
    /* Nothing to retarget: this controller is handed an address on every
     * transfer, so the next present reads whichever buffer the screen
     * names by then. */
    (void)d; (void)fb;
    return TIKU_DISPLAY_OK;
}

int
tiku_display_arch_present(tiku_display_t *d, uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h)
{
    return (tiku_dc_present_rect(d->fb, d->stride, x, y, w, h,
                                 TIKU_DC_FMT_RGBA8888) == TIKU_DC_OK)
           ? TIKU_DISPLAY_OK : TIKU_DISPLAY_ERR_STATE;
}
