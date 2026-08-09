/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drw_arch.h - RA8P1 2D drawing engine.
 *
 * Renders into a caller-owned RGB565 framebuffer in memory; nothing here
 * knows about a panel, so it is useful with no display attached.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_DRW_ARCH_H_
#define TIKU_RA8P1_DRW_ARCH_H_

#include <stdint.h>

#define TIKU_DRW_OK             0
#define TIKU_DRW_ERR_STATE     -1   /**< engine absent or never initialised */
#define TIKU_DRW_ERR_INVALID   -2   /**< null buffer, or geometry off-buffer */
#define TIKU_DRW_ERR_TIMEOUT   -3   /**< the render never reported idle      */

/**
 * @brief Release the module stop and confirm the engine answers.
 *
 * @return TIKU_DRW_OK, or TIKU_DRW_ERR_STATE when the ID register does not
 *         read back as a D/AVE revision word
 */
int tiku_drw_arch_init(void);

/**
 * @brief The engine's hardware revision and feature word.
 *
 * @return HWREVISION, or 0 before a successful init
 */
uint32_t tiku_drw_arch_id(void);

/**
 * @brief Fill an axis-aligned rectangle with a solid colour.
 *
 * @note Blocks until the engine is idle.  The caller owns cache maintenance:
 *       the engine reads and writes memory, not the CPU's D-cache.
 *
 * @param fb     Framebuffer base, RGB565, 32-bit aligned
 * @param pitch  Framebuffer width in pixels
 * @param h      Framebuffer height in pixels
 * @param x      Rectangle left, in pixels
 * @param y      Rectangle top, in pixels
 * @param w      Rectangle width in pixels
 * @param rh     Rectangle height in pixels
 * @param rgb565 Fill colour
 * @return TIKU_DRW_OK, or a negative error
 */
int tiku_drw_arch_fill(void *fb, uint32_t pitch, uint32_t h,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t rh,
                       uint16_t rgb565);

/**
 * @brief Wait for the engine to finish, bounded.
 *
 * @return TIKU_DRW_OK once idle, or TIKU_DRW_ERR_TIMEOUT
 */
int tiku_drw_arch_wait(void);


#endif /* TIKU_RA8P1_DRW_ARCH_H_ */
