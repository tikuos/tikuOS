/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_p4x_panel.h - P4X Function EV Board panel startup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4X_PANEL_H_
#define TIKU_P4X_PANEL_H_
/** @brief Initialize DSI and wake the EK79007 with its backlight off. */
int tiku_p4x_panel_init(void);
/** @brief Enable the backlight after video starts. */
void tiku_p4x_panel_light(void);
/** @brief Reset the EK79007, enable color bars and turn on the backlight.
 * @note Requires CPU0 at 40 MHz and exclusive access to the display.
 */
int tiku_p4x_panel_pattern(void);
/** @brief Close the kernel display and release its paired buffers after DMA stops. */
void tiku_p4_display_close(void);
#endif
