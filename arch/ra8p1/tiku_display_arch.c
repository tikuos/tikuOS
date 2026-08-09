/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_arch.c - the RA8P1 screen behind interfaces/display.
 *
 * The 2D engine draws into the framebuffer and the display controller scans
 * it continuously, so presenting is a cache clean rather than a transfer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/display/tiku_display_arch.h>

#include "tiku_drw_arch.h"
#include "tiku_glcdc_arch.h"
#include "tiku_cache_arch.h"
#include "tiku_sdram_arch.h"

/** @brief Whether the external array is attached and can hold a second frame. */
static uint8_t display_have_sdram;

/**
 * @brief Narrow an ARGB8888 colour to the framebuffer's 565 layout.
 *
 * @param c  Colour as ARGB8888
 * @return The same colour in RGB565
 */
static uint16_t
rgb565_of(uint32_t c)
{
    uint32_t r = (c >> 16) & 0xFFu;
    uint32_t g = (c >> 8) & 0xFFu;
    uint32_t b = c & 0xFFu;

    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

int
tiku_display_arch_init(tiku_display_t *d)
{
    int rc;

    /* The panel's geometry is fixed by the board, so a framebuffer of any
     * other size would scan as a smear rather than fail. */
    if (d->w != TIKU_GLCDC_PANEL_W || d->h != TIKU_GLCDC_PANEL_H) {
        return TIKU_DISPLAY_ERR_INVALID;
    }
    rc = tiku_drw_arch_init();
    if (rc != TIKU_DRW_OK) {
        return TIKU_DISPLAY_ERR_STATE;
    }
    /* Bring the external array into the allocator so a caller can hold a
     * second frame.  A board without it still runs single-buffered, which
     * is why this is not an error. */
    if (!display_have_sdram &&
        tiku_ra8p1_sdram_attach() == TIKU_RA8P1_SDRAM_OK) {
        display_have_sdram = 1u;
    }
    /* Publish the buffer before the controller starts scanning it, or the
     * first frames show whatever the tier held. */
    tiku_ra8p1_dcache_clean(d->fb, (uint32_t)d->stride * d->h);

    return (tiku_glcdc_arch_panel_start(d->front) == TIKU_GLCDC_OK)
           ? TIKU_DISPLAY_OK : TIKU_DISPLAY_ERR_STATE;
}

uint32_t
tiku_display_arch_caps(void)
{
    /* Beziers are still hardware this port has not brought up, so they stay
     * unadvertised; rectangles and circles are proved by pixel count.
     *
     * Flipping is offered only when a second frame can actually be held:
     * two frames of this panel are 2.4 MB and the SRAM tier is smaller than
     * that, so without the external array the capability would be a promise
     * no caller could keep.
     */
    return TIKU_DISPLAY_CAP_CIRCLE |
           (display_have_sdram ? TIKU_DISPLAY_CAP_FLIP : 0u);
}

tiku_display_fmt_t
tiku_display_arch_format(void)
{
    return TIKU_DISPLAY_FMT_RGB565;
}

void
tiku_display_arch_geometry(uint16_t *w, uint16_t *h)
{
    if (w != (uint16_t *)0) { *w = TIKU_GLCDC_PANEL_W; }
    if (h != (uint16_t *)0) { *h = TIKU_GLCDC_PANEL_H; }
}

/**
 * @brief The whole rows a rectangle touches, as a cache-maintenance range.
 *
 * @param d     Screen
 * @param y     Top row
 * @param h     Row count
 * @param bytes Receives the byte length
 * @return Address of the first row
 */
static const void *
row_span(const tiku_display_t *d, uint16_t y, uint16_t h, uint32_t *bytes)
{
    *bytes = (uint32_t)h * d->stride;
    return (const uint8_t *)d->fb + ((uint32_t)y * d->stride);
}

int
tiku_display_arch_fill_rect(tiku_display_t *d, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h, uint32_t colour)
{
    uint32_t bytes;
    const void *rows = row_span(d, y, h, &bytes);
    int rc;

    /*
     * The engine writes memory while the CPU works through its cache, so the
     * region has to change hands twice.  Cleaning first pushes out anything
     * the CPU had pending -- a later clean would otherwise put those stale
     * lines back over the engine's output -- and invalidating afterwards is
     * what lets the CPU read what was actually drawn.
     */
    tiku_ra8p1_dcache_clean_invalidate(rows, bytes);

    rc = tiku_drw_arch_fill(d->fb, d->w, d->h, x, y, w, h, rgb565_of(colour));

    tiku_ra8p1_dcache_invalidate(rows, bytes);

    if (rc == TIKU_DRW_ERR_INVALID) {
        return TIKU_DISPLAY_ERR_INVALID;
    }
    return (rc == TIKU_DRW_OK) ? TIKU_DISPLAY_OK : TIKU_DISPLAY_ERR_STATE;
}

int
tiku_display_arch_fill_circle(tiku_display_t *d, int16_t cx, int16_t cy,
                              uint16_t r, uint32_t colour)
{
    (void)d; (void)cx; (void)cy; (void)r; (void)colour;
    return TIKU_DISPLAY_ERR_UNSUPPORTED;
}

int
tiku_display_arch_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y,
                                    uint16_t w, uint16_t h, uint16_t r,
                                    uint32_t colour)
{
    (void)d; (void)x; (void)y; (void)w; (void)h; (void)r; (void)colour;
    return TIKU_DISPLAY_ERR_UNSUPPORTED;
}

int
tiku_display_arch_set_scanout(tiku_display_t *d, void *fb)
{
    /* The buffer has to be in memory before the controller reads it, and
     * the CPU may have written it without the engine's involvement. */
    tiku_ra8p1_dcache_clean(fb, (uint32_t)d->stride * d->h);
    return (tiku_glcdc_arch_rebind(fb) == TIKU_GLCDC_OK)
           ? TIKU_DISPLAY_OK : TIKU_DISPLAY_ERR_STATE;
}

int
tiku_display_arch_present(tiku_display_t *d, uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h)
{
    uint32_t bytes;
    const void *rows = row_span(d, y, h, &bytes);

    (void)x; (void)w;
    /*
     * The controller scans memory continuously, so presenting is making the
     * CPU's own writes reachable rather than moving anything.  Whole rows,
     * because a partial one shares cache lines with the pixels beside it.
     */
    tiku_ra8p1_dcache_clean(rows, bytes);
    return TIKU_DISPLAY_OK;
}
