/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd_arch.c - RP2350 (no on-chip LCD controller) — empty TU
 *
 * The Pico 2 W has no on-chip LCD peripheral. The interfaces/lcd
 * layer self-gates to no-ops on builds where TIKU_BOARD_HAS_LCD is
 * unset, so this file just needs to compile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
