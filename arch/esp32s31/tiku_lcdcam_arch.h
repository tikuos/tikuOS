/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcdcam_arch.h - The LCD_CAM block's bus clocks, reset and core
 * clock, shared by the RGB display and the DVP camera.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_LCDCAM_ARCH_H_
#define TIKU_S31_LCDCAM_ARCH_H_
/** @brief Take a reference on the LCD_CAM block; kernel context.
 * The first reference runs the core clock from the 40 MHz crystal, ungates
 * the bus clocks and resets the block. Later references change nothing. */
void tiku_s31_lcdcam_claim(void);
/** @brief Drop a reference; kernel context. The last one gates the core and
 * bus clocks and holds the block in reset. Without a reference it does
 * nothing. */
void tiku_s31_lcdcam_release(void);
#endif
