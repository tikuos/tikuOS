/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display.h - portable control of one accelerated screen.
 *
 * Draws accumulate a damage rectangle and flush() makes exactly that region
 * visible, whatever "visible" costs on the part: a transfer to the panel on
 * one, a cache clean under a controller that scans continuously on another.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DISPLAY_H_
#define TIKU_DISPLAY_H_

#include <stdint.h>

/** @brief Outcomes; a caller that only tests != OK still behaves. */
#define TIKU_DISPLAY_OK              0
#define TIKU_DISPLAY_ERR_STATE      -1  /**< not initialised, or busy      */
#define TIKU_DISPLAY_ERR_INVALID    -2  /**< geometry or buffer refused    */
#define TIKU_DISPLAY_ERR_UNSUPPORTED -3 /**< backend has no such primitive */

/** @brief Framebuffer pixel layout, which differs by part. */
typedef enum {
    TIKU_DISPLAY_FMT_RGB565 = 0,   /**< 16 bpp                             */
    TIKU_DISPLAY_FMT_RGBA8888      /**< 32 bpp                             */
} tiku_display_fmt_t;

/*
 * Optional primitives.  A backend advertises only what its hardware really
 * does; asking for one it lacks is refused rather than emulated, so a caller
 * can tell "drew nothing" from "drew slowly in software".
 */
#define TIKU_DISPLAY_CAP_CIRCLE   (1u << 0)
#define TIKU_DISPLAY_CAP_ROUNDED  (1u << 1)
#define TIKU_DISPLAY_CAP_FLIP     (1u << 2)  /**< can swap whole buffers  */

/**
 * @brief One screen: its framebuffer and the damage pending on it.
 *
 * Construct with tiku_display_init(); the damage rectangle is
 * [dx0,dx1) x [dy0,dy1) and is empty while @p dirty is zero.
 */
typedef struct {
    void    *fb;        /**< where draws land; the back buffer when paired  */
    void    *front;     /**< what the panel shows; equals fb when single    */
    uint16_t w;         /**< width in pixels                               */
    uint16_t h;         /**< height in pixels                              */
    uint16_t stride;    /**< bytes per row                                 */
    uint16_t dx0;       /**< damage min x (inclusive)                      */
    uint16_t dy0;       /**< damage min y (inclusive)                      */
    uint16_t dx1;       /**< damage max x (exclusive)                      */
    uint16_t dy1;       /**< damage max y (exclusive)                      */
    uint8_t  dirty;     /**< non-zero when a region awaits flush           */
    uint8_t  fmt;       /**< tiku_display_fmt_t of @p fb                   */
} tiku_display_t;

/**
 * @brief Bring the screen up and bind a framebuffer to it.
 *
 * @note @p fb must hold w * h pixels in tiku_display_format(), and must be
 *       memory the display hardware can reach as a bus master.
 *
 * @param d  Screen to initialise
 * @param fb Framebuffer base
 * @param w  Width in pixels
 * @param h  Height in pixels
 * @return TIKU_DISPLAY_OK, or a negative error
 */
int tiku_display_init(tiku_display_t *d, void *fb, uint16_t w, uint16_t h);

/**
 * @brief Which optional primitives this backend really has.
 *
 * @note Ask after tiku_display_init(): a capability can depend on resources
 *       the screen only claims as it comes up, so the answer before then is
 *       the conservative one.
 */
uint32_t tiku_display_caps(void);

/**
 * @brief The screen's native size, which the panel fixes.
 *
 * @note Ask before allocating: a framebuffer of any other size is refused,
 *       because a controller scanning the wrong geometry shows a smear
 *       rather than reporting an error.
 *
 * @param w  Receives width in pixels, or NULL
 * @param h  Receives height in pixels, or NULL
 */
void tiku_display_geometry(uint16_t *w, uint16_t *h);

/** @brief Bytes per pixel of the native format. */
uint16_t tiku_display_bpp(void);

/** @brief The pixel layout this backend's framebuffer uses. */
tiku_display_fmt_t tiku_display_format(void);

/**
 * @brief Fill the whole screen and mark all of it damaged.
 *
 * @param d      Screen
 * @param colour Colour as ARGB8888, converted to the native format
 * @return TIKU_DISPLAY_OK, or a negative error
 */
int tiku_display_clear(tiku_display_t *d, uint32_t colour);

/**
 * @brief Fill a rectangle and extend the damage region.
 *
 * @param d      Screen
 * @param x      Left edge, may be negative
 * @param y      Top edge, may be negative
 * @param w      Width in pixels
 * @param h      Height in pixels
 * @param colour Colour as ARGB8888
 * @return TIKU_DISPLAY_OK, or a negative error
 */
int tiku_display_fill_rect(tiku_display_t *d, int16_t x, int16_t y,
                           uint16_t w, uint16_t h, uint32_t colour);

/**
 * @brief Fill a circle, where the backend advertises CAP_CIRCLE.
 *
 * @param d      Screen
 * @param cx     Centre x
 * @param cy     Centre y
 * @param r      Radius in pixels
 * @param colour Colour as ARGB8888
 * @return TIKU_DISPLAY_OK, or TIKU_DISPLAY_ERR_UNSUPPORTED
 */
int tiku_display_fill_circle(tiku_display_t *d, int16_t cx, int16_t cy,
                             uint16_t r, uint32_t colour);

/**
 * @brief Fill a rounded rectangle, where the backend advertises CAP_ROUNDED.
 *
 * @param d      Screen
 * @param x      Left edge
 * @param y      Top edge
 * @param w      Width in pixels
 * @param h      Height in pixels
 * @param r      Corner radius
 * @param colour Colour as ARGB8888
 * @return TIKU_DISPLAY_OK, or TIKU_DISPLAY_ERR_UNSUPPORTED
 */
int tiku_display_fill_rounded_rect(tiku_display_t *d, int16_t x, int16_t y,
                                   uint16_t w, uint16_t h, uint16_t r,
                                   uint32_t colour);

/**
 * @brief Pair two framebuffers so drawing never touches what is on show.
 *
 * @note Draws go to @p back from here on; @p front is what the panel shows
 *       until the next flip.  Neither buffer's contents are copied, so a
 *       caller that draws only differences must repaint after each flip.
 *
 * @param d      Screen
 * @param front  Buffer to show now
 * @param back   Buffer to draw into
 * @return TIKU_DISPLAY_OK, or TIKU_DISPLAY_ERR_UNSUPPORTED without CAP_FLIP
 */
int tiku_display_set_buffers(tiku_display_t *d, void *front, void *back);

/**
 * @brief Show what was drawn and start drawing into the other buffer.
 *
 * @param d  Screen
 * @return TIKU_DISPLAY_OK, or TIKU_DISPLAY_ERR_UNSUPPORTED without CAP_FLIP
 */
int tiku_display_flip(tiku_display_t *d);

/**
 * @brief Make the damaged region visible and clear the damage.
 *
 * @param d  Screen
 * @return TIKU_DISPLAY_OK, including when there was nothing to do
 */
int tiku_display_flush(tiku_display_t *d);

/**
 * @brief Bounds of the damage awaiting flush.
 *
 * @param d  Screen
 * @param x  Receives left edge, or NULL
 * @param y  Receives top edge, or NULL
 * @param w  Receives width, or NULL
 * @param h  Receives height, or NULL
 * @return 1 when a region is pending, 0 when clean
 */
int tiku_display_damage_bounds(const tiku_display_t *d,
                               uint16_t *x, uint16_t *y,
                               uint16_t *w, uint16_t *h);

#endif /* TIKU_DISPLAY_H_ */
