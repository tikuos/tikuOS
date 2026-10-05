/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd.h - platform-independent segment-LCD interface.
 *
 * A small API for fixed-segment glass, with optional icons where the board sets
 * TIKU_BOARD_LCD_HAS_ICONS.  On a board without an LCD every entry point is a
 * no-op and TIKU_LCD_PRESENT is the constant 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_LCD_H_
#define TIKU_LCD_H_

#include <stdint.h>
#include "tiku.h"

/*===========================================================================*/
/* PRESENCE                                                                   */
/*===========================================================================*/

#ifndef TIKU_BOARD_HAS_LCD
/** @brief 1 when the board header declares an LCD; 0 by default. */
#define TIKU_BOARD_HAS_LCD          0
#endif

#ifndef TIKU_BOARD_LCD_NUM_CHARS
/** @brief Character cells on the board's LCD; 0 without one. */
#define TIKU_BOARD_LCD_NUM_CHARS    0
#endif

/**
 * @brief 1 if the board has an LCD, else 0.
 *
 * A constant, usable in #if and as the condition of a plain `if`.
 */
#define TIKU_LCD_PRESENT            TIKU_BOARD_HAS_LCD

/**
 * @brief Number of writable character positions, zero on boards
 *        without an LCD. 6 on the MSP-EXP430FR6989 LaunchPad.
 */
uint8_t tiku_lcd_num_chars(void);

/*===========================================================================*/
/* LIFECYCLE                                                                  */
/*===========================================================================*/

/**
 * @brief Bring up the LCD controller and clear the panel.
 *
 * Configures the peripheral (charge pump, mux ratio, frame frequency, pin
 * muxing) and blanks every segment; a no-op on boards without an LCD.
 *
 * @note Callable at boot, before the scheduler starts.
 */
void tiku_lcd_init(void);

/**
 * @brief Blank every character position and every icon segment.
 */
void tiku_lcd_clear(void);

/*===========================================================================*/
/* CHARACTER / STRING WRITES                                                  */
/*===========================================================================*/

/**
 * @brief Write one ASCII character to a specific position.
 *
 * @param pos  Zero-based position, 0 .. tiku_lcd_num_chars()-1.
 *             Out-of-range positions are silently ignored.
 * @param ch   ASCII character. Recognised: '0'-'9', 'A'-'Z',
 *             'a'-'z' (folded to upper case), space (blanks the
 *             cell), '-' (middle bar). Unknown characters render
 *             as a blank.
 *
 * Lower- and upper-case render identically on a 14-segment panel.
 */
void tiku_lcd_putchar(uint8_t pos, char ch);

/**
 * @brief Write a left-aligned string starting at position 0.
 *
 * The string is truncated to tiku_lcd_num_chars(); positions to
 * the right of the string are blanked. Pass NULL to clear the
 * whole text area (icons are preserved).
 *
 * @param s  Null-terminated ASCII string, or NULL.
 */
void tiku_lcd_puts(const char *s);

/**
 * @brief Write a right-aligned string ending at the last position.
 *
 * Positions to the left of the string are blanked; a string longer than the
 * panel keeps its first tiku_lcd_num_chars() characters.
 *
 * @param s  Null-terminated ASCII string, or NULL (clears).
 */
void tiku_lcd_puts_right(const char *s);

/**
 * @brief Overwrite characters starting at @p pos without blanking
 *        the rest of the line.
 *
 * Cells outside the written span keep their content.  The write stops at
 * the end of the panel.
 *
 * @param pos  Starting position, 0 .. tiku_lcd_num_chars()-1.
 * @param s    Null-terminated ASCII string. NULL is treated as "".
 */
void tiku_lcd_puts_at(uint8_t pos, const char *s);

/*===========================================================================*/
/* NUMBER FORMATTING (RIGHT-ALIGNED, BLANK-PADDED)                            */
/*===========================================================================*/

/**
 * @brief Display a non-negative integer, right-aligned.
 *
 * A value above 10^N - 1 (N the panel width) shows as all nines.
 *
 * @param value  Unsigned value to render.
 */
void tiku_lcd_put_uint(uint32_t value);

/**
 * @brief Display a signed integer, right-aligned, with a leading
 *        '-' for negative values.
 *
 * The minus consumes one cell, so the magnitude budget is one
 * less digit than tiku_lcd_put_uint(). Out-of-range magnitudes
 * are clamped to "-999.." / "999..".
 *
 * @param value  Signed value to render.
 */
void tiku_lcd_put_int(int32_t value);

/**
 * @brief Display @p value as zero-padded uppercase hexadecimal,
 *        right-aligned, in @p digits cells.
 *
 * @param value   Value to render.
 * @param digits  1 .. tiku_lcd_num_chars(). 0 counts as 1, and larger
 *                values are clamped to the panel width.
 *
 * Cells to the left of the hex field are blanked.
 */
void tiku_lcd_put_hex(uint32_t value, uint8_t digits);

/**
 * @brief Display a fixed-point value, right-aligned, with the decimal point
 *        shown via the board's inter-digit dot icon.
 *
 * Renders @p value as an integer and lights the dot before the last @p decimals
 * digits, reserving a cell for a leading '-' and adding a leading zero so the
 * integer cell is never empty.  A value that will not fit clamps to all-9s.
 *
 * @param value     Signed value to render (raw, no scaling).
 * @param decimals  Number of fractional digits. 0 behaves like
 *                  tiku_lcd_put_int(). Values >= panel width are
 *                  treated as overflow.
 */
void tiku_lcd_put_fixed(int32_t value, uint8_t decimals);

/*===========================================================================*/
/* ICONS (WITH TIKU_BOARD_LCD_HAS_ICONS)                                      */
/*===========================================================================*/

#if defined(TIKU_BOARD_LCD_HAS_ICONS) && TIKU_BOARD_LCD_HAS_ICONS

/**
 * @brief Number of named icon segments on the active board.
 *
 * Icon IDs are dense, in the range [0, tiku_lcd_icon_count()).
 * The board header gives them friendly names like
 * `TIKU_LCD_ICON_HEART`.
 */
uint8_t tiku_lcd_icon_count(void);

/**
 * @brief Light a single icon segment.
 *
 * @param icon_id  Symbolic ID from the board header
 *                 (e.g. TIKU_LCD_ICON_HEART). Out-of-range IDs
 *                 are silently ignored.
 */
void tiku_lcd_icon_on(uint8_t icon_id);

/** @brief Turn a single icon segment off. */
void tiku_lcd_icon_off(uint8_t icon_id);

/** @brief Toggle a single icon segment. */
void tiku_lcd_icon_toggle(uint8_t icon_id);

/** @brief Set an icon to @p on (0 = off, non-zero = on). */
void tiku_lcd_icon_set(uint8_t icon_id, uint8_t on);

/**
 * @brief Turn off every named icon at once. Text positions are
 *        left untouched.
 */
void tiku_lcd_icons_clear(void);

#endif /* TIKU_BOARD_LCD_HAS_ICONS */

#endif /* TIKU_LCD_H_ */
