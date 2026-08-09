/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_glcdc_arch.h - RA8P1 graphics LCD controller.
 *
 * Generates display timing and scans a framebuffer out of memory.  Nothing
 * here drives a panel: the pixels leave through the MIPI link, so this is
 * usable and checkable with no display attached.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_GLCDC_ARCH_H_
#define TIKU_RA8P1_GLCDC_ARCH_H_

#include <stdint.h>

#define TIKU_GLCDC_OK           0
#define TIKU_GLCDC_ERR_STATE   -1   /**< not initialised, or already running */
#define TIKU_GLCDC_ERR_INVALID -2   /**< timing outside the field widths     */
#define TIKU_GLCDC_ERR_TIMEOUT -3   /**< a clock or enable never settled     */

/** @brief One display mode, in pixel clocks and lines. */
typedef struct {
    uint16_t h_active;      /**< visible pixels per line            */
    uint16_t h_total;       /**< pixels per line including blanking */
    uint16_t h_sync;        /**< sync pulse width, pixels           */
    uint16_t h_start;       /**< first visible pixel                */
    uint16_t v_active;      /**< visible lines per frame            */
    uint16_t v_total;       /**< lines per frame including blanking */
    uint16_t v_sync;        /**< sync pulse width, lines            */
    uint16_t v_start;       /**< first visible line                 */
} tiku_glcdc_mode_t;

/**
 * @brief Start timing generation, optionally scanning a framebuffer.
 *
 * @note Leaves LCDCLK on MOCO undivided, so the frame rate is
 *       8 MHz / (h_total * v_total) and a measured rate checks the clock and
 *       the timing registers together.
 *
 * @param mode  Timing to generate
 * @param fb    RGB565 framebuffer for layer 1, or NULL for timing only
 * @return TIKU_GLCDC_OK, or a negative error
 */
int tiku_glcdc_arch_start(const tiku_glcdc_mode_t *mode, const void *fb);

/** @brief Stop the background plane and release the layer. */
void tiku_glcdc_arch_stop(void);

/**
 * @brief Whether the background plane reports itself running.
 *
 * @return Non-zero when BG_MON says the plane is operating
 */
int tiku_glcdc_arch_running(void);

/**
 * @brief Consume the line-detect flag.
 *
 * @return Non-zero if a frame boundary was seen since the previous call
 */
int tiku_glcdc_arch_vpos_take(void);

/**
 * @brief Whether layer 1 has reported a framebuffer underflow.
 *
 * @return Non-zero when the underflow flag is set; the flag latches
 */
int tiku_glcdc_arch_underflow(void);

/** @brief The LCDCLK frequency this driver configures, in Hz. */
uint32_t tiku_glcdc_arch_pixel_hz(void);

#endif /* TIKU_RA8P1_GLCDC_ARCH_H_ */
