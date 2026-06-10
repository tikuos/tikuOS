/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd_arch.c - Apollo 510 segment-LCD driver (none on this board)
 *
 * The Apollo510 EVB has no segment LCD, so this translation unit is
 * intentionally empty (mirrors arch/arm-rp2350/tiku_lcd_arch.c). The
 * generic LCD interface self-gates to no-ops via TIKU_BOARD_HAS_LCD.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
