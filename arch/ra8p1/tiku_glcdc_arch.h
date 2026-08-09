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

/**
 * @brief Drive the board's parallel RGB panel from a framebuffer.
 *
 * Routes the 24 data lines and the timing signals to the controller, releases
 * the panel's reset, lights the backlight and scans @p fb out at the panel's
 * own timing.  Parallel, not MIPI: the DSI block stays stopped.
 *
 * @note Requires board switch SW4-6 OFF, and a core clock rung of 240 MHz --
 *       the pixel clock divides PLL1P, which follows the rung.
 *
 * @param fb  RGB565 framebuffer of TIKU_GLCDC_PANEL_W x TIKU_GLCDC_PANEL_H
 * @return TIKU_GLCDC_OK, or a negative error
 */
int tiku_glcdc_arch_panel_start(const void *fb);

/**
 * @brief Point the running layer at a different framebuffer.
 *
 * @note Takes effect at the next frame boundary, which is what makes it the
 *       basis of page flipping as well as of re-binding.
 *
 * @param fb  RGB565 framebuffer of the panel's geometry
 * @return TIKU_GLCDC_OK, or TIKU_GLCDC_ERR_STATE when nothing is running
 */
int tiku_glcdc_arch_rebind(const void *fb);

/** @brief The parallel panel's visible geometry. */
#define TIKU_GLCDC_PANEL_W  1024U
#define TIKU_GLCDC_PANEL_H  600U

#endif /* TIKU_RA8P1_GLCDC_ARCH_H_ */
