/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display.h - minimal GPU-accelerated compositor for tikuOS.
 *
 * A thin layer over the from-scratch Apollo510 GPU (tiku_gpu_arch) and display
 * controller (tiku_dc_arch): one framebuffer surface, GPU draw operations that
 * accumulate a damage rectangle, and a flush() that pushes ONLY the damaged
 * region to the panel (a partial-rect transfer) -- so a small change costs a
 * small transfer, not a whole 468x468 frame.
 *
 * Usage:
 *   static uint8_t fb[468*468*4] __attribute__((section(".ssram"), aligned(32)));
 *   tiku_display_t d;
 *   tiku_display_init(&d, fb, 468, 468);
 *   tiku_display_clear(&d, 0xFF101418);       // dark background
 *   tiku_display_fill_rect(&d, 40, 40, 80, 80, 0xFF3366CC);
 *   tiku_display_flush(&d);                    // presents just the damage
 *
 * apollo510/apollo510b only; compiled behind TIKU_DRV_DC_ENABLE (which implies
 * the GPU driver). The framebuffer MUST live in SSRAM (GPU/DC bus-master
 * visible; never DTCM).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DISPLAY_H_
#define TIKU_DISPLAY_H_

#include <stdint.h>
#include <arch/ambiq/tiku_gpu_arch.h>
#include <arch/ambiq/tiku_dc_arch.h>

/**
 * @brief A GPU-backed screen: framebuffer + accumulated damage rectangle.
 *
 * Fields are internal; construct with tiku_display_init() and use the draw /
 * flush entry points. The damage rectangle is [dx0,dx1) x [dy0,dy1) and is
 * empty when @p dirty is 0.
 */
typedef struct {
    void    *fb;        /**< framebuffer base (SSRAM, RGBA8888)               */
    uint16_t w;         /**< width in pixels                                  */
    uint16_t h;         /**< height in pixels                                 */
    uint16_t stride;    /**< bytes per row (w * 4)                            */
    uint16_t dx0;       /**< damage min x (inclusive)                         */
    uint16_t dy0;       /**< damage min y (inclusive)                         */
    uint16_t dx1;       /**< damage max x (exclusive)                         */
    uint16_t dy1;       /**< damage max y (exclusive)                         */
    uint8_t  dirty;     /**< 1 if a region is pending flush                   */
} tiku_display_t;

/**
 * @brief Bring up the GPU + panel and bind a framebuffer.
 *
 * Powers the GFX domain, brings up the display path (DSI + NemaDC + panel),
 * and binds @p fb as the screen. @p fb MUST be a w*h*4-byte SSRAM buffer.
 * @return TIKU_DC_OK, or a DC/GPU error.
 */
tiku_dc_err_t tiku_display_init(tiku_display_t *d, void *fb,
                                uint16_t w, uint16_t h);

/** @brief Clear the whole screen to @p color (GPU fill); marks full damage. */
tiku_gpu_err_t tiku_display_clear(tiku_display_t *d, uint32_t color);

/** @brief Fill a rectangle (GPU) and extend the damage region. */
tiku_gpu_err_t tiku_display_fill_rect(tiku_display_t *d,
                                      int16_t x, int16_t y,
                                      uint16_t w, uint16_t h, uint32_t color);

/** @brief Fill a rounded rectangle (GPU) and extend the damage region. */
tiku_gpu_err_t tiku_display_fill_rounded_rect(tiku_display_t *d,
                                              int16_t x, int16_t y,
                                              uint16_t w, uint16_t h,
                                              uint16_t r, uint32_t color);

/** @brief Fill a circle (GPU) and extend the damage region. */
tiku_gpu_err_t tiku_display_fill_circle(tiku_display_t *d,
                                        int16_t cx, int16_t cy,
                                        uint16_t r, uint32_t color);

/**
 * @brief Blit a source surface onto the screen at (@p x, @p y) and damage it.
 *
 * @p src MUST be an SSRAM surface (see tiku_gpu_surface_t).
 */
tiku_gpu_err_t tiku_display_blit(tiku_display_t *d,
                                 const tiku_gpu_surface_t *src,
                                 int16_t x, int16_t y, tiku_gpu_blend_t blend);

/**
 * @brief Push the accumulated damage region to the panel and clear it.
 *
 * A partial-rect transfer of exactly the damaged bounds (nothing if clean).
 * @return TIKU_DC_OK (also when there was nothing to flush).
 */
tiku_dc_err_t tiku_display_flush(tiku_display_t *d);

/** @brief Bounds of the pending damage; returns 0 if clean, 1 if dirty. */
int tiku_display_damage_bounds(const tiku_display_t *d,
                               uint16_t *x, uint16_t *y,
                               uint16_t *w, uint16_t *h);

#endif /* TIKU_DISPLAY_H_ */
