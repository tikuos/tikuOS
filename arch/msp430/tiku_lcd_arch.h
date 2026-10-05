/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd_arch.h - MSP430 LCD_C peripheral arch interface.
 *
 * Implemented in tiku_lcd_arch.c, compiled only when the device declares
 * LCD_C and the board declares a panel; otherwise the unit compiles to
 * nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_LCD_ARCH_H_
#define TIKU_LCD_ARCH_H_

#include <stdint.h>
#include "tiku.h"

/**
 * @brief Bring up LCD_C and the board's LCD pins, with LCD memory cleared
 *        and the panel on.
 */
void tiku_lcd_arch_init(void);

/**
 * @brief Blank every LCDMEM byte (text positions and icons).
 */
void tiku_lcd_arch_clear(void);

/**
 * @brief Render one ASCII character at a position.
 *
 * Digits, letters of either case and '-' have glyphs; any other character
 * is a blank cell.  Byte1 bits in TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK
 * keep their value, so a lit icon stays lit.
 *
 * @note @p pos must be below TIKU_BOARD_LCD_NUM_CHARS; it is not checked.
 */
void tiku_lcd_arch_putchar(uint8_t pos, char ch);

#if defined(TIKU_BOARD_LCD_HAS_ICONS) && TIKU_BOARD_LCD_HAS_ICONS

/**
 * @brief Set or clear a single icon segment by board-defined ID.
 *
 * @param icon_id  0 .. TIKU_BOARD_LCD_NUM_ICONS-1.
 *                 Out-of-range IDs are ignored.
 * @param on       0 = clear, non-zero = set.
 */
void tiku_lcd_arch_icon_set(uint8_t icon_id, uint8_t on);

/**
 * @brief Toggle a single icon segment; out-of-range IDs are ignored.
 */
void tiku_lcd_arch_icon_toggle(uint8_t icon_id);

#endif /* TIKU_BOARD_LCD_HAS_ICONS */

#endif /* TIKU_LCD_ARCH_H_ */
