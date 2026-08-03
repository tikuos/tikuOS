/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd_arch.c - STM32N6 segment-LCD backend: none on this board.
 *
 * The part carries LTDC and NeoChrom for pixel displays, not the segment
 * controller this layer drives, and the Nucleo has no panel fitted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* interfaces/lcd gates itself to no-ops when TIKU_BOARD_HAS_LCD is unset, so
 * this translation unit exists only to keep the build symmetric. */
typedef int tiku_stm32n6_lcd_unused_t;
