/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd.h - Platform-independent segment-LCD interface
 *
 * A small, ergonomic API for fixed-segment LCDs (e.g. the FH-1138P
 * 96-segment glass on the MSP-EXP430FR6989 LaunchPad). Designed so
 * application code can talk to the display without caring which
 * controller is underneath.
 *
 * QUICK START
 * -----------
 *   #include <interfaces/lcd/tiku_lcd.h>
 *
 *   tiku_lcd_init();
 *   tiku_lcd_puts("HELLO");          // left-aligned banner
 *   tiku_lcd_puts_right("3");        // right-aligned, e.g. version
 *   tiku_lcd_put_uint(1234);         // counter / sensor value
 *   tiku_lcd_put_int(-42);           // signed value
 *   tiku_lcd_puts_at(4, "OK");       // partial overwrite at pos 4..
 *   tiku_lcd_clear();                // blank everything
 *
 * Icons (optional, board declares them via TIKU_BOARD_LCD_HAS_ICONS):
 *
 *   tiku_lcd_icon_on(TIKU_LCD_ICON_HEART);
 *   tiku_lcd_icon_toggle(TIKU_LCD_ICON_DOT2);
 *   tiku_lcd_icons_clear();
 *
 * PORTABILITY
 * -----------
 * Boards without an LCD compile this header just fine; every entry
 * point is a no-op when TIKU_BOARD_HAS_LCD is 0, and the runtime
 * predicate TIKU_LCD_PRESENT folds to a constant so portable code
 * can branch without #ifdef:
 *
 *   if (TIKU_LCD_PRESENT) { tiku_lcd_puts("ALIVE"); }
 *
 * The icon API is only declared when the board sets
 * TIKU_BOARD_LCD_HAS_ICONS — guard calls with #ifdef if you want
 * the same source to compile on icon-less boards.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
#define TIKU_BOARD_HAS_LCD          0
#endif

#ifndef TIKU_BOARD_LCD_NUM_CHARS
#define TIKU_BOARD_LCD_NUM_CHARS    0
#endif

/**
 * @brief Compile-time + runtime predicate: 1 if the board has an LCD.
 *
 * Folds to a constant; safe to use as the condition in a normal
 * `if` so portable code can avoid `#ifdef` clutter.
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
 * Configures the LCD peripheral (charge pump, mux ratio, frame
 * frequency, pin muxing) and blanks every segment. Safe to call
 * once at boot before the scheduler starts. No-op on boards
 * without an LCD.
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
 * Positions to the left of the string are blanked. Useful for
 * displaying a value (clock, sensor, version) flush-right while a
 * fixed label sits on the left via tiku_lcd_puts_at().
 *
 * @param s  Null-terminated ASCII string, or NULL (clears).
 */
void tiku_lcd_puts_right(const char *s);

/**
 * @brief Overwrite characters starting at @p pos without blanking
 *        the rest of the line.
 *
 * Lets you compose "label + value" displays without rewriting the
 * untouched cells. The write stops at the end of the panel.
 *
 * @param pos  Starting position, 0 .. tiku_lcd_num_chars()-1.
 * @param s    Null-terminated ASCII string. NULL is treated as "".
 */
void tiku_lcd_puts_at(uint8_t pos, const char *s);

/*===========================================================================*/
/* NUMBER FORMATTING (all right-aligned, blank-padded)                        */
/*===========================================================================*/

/**
 * @brief Display a non-negative integer, right-aligned.
 *
 * Values larger than 10^N - 1 (where N is the panel width) are
 * clamped to all-nines so the user sees "overflow" rather than
 * the wrong number.
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
 * @param digits  1 .. tiku_lcd_num_chars(). Larger values are
 *                clamped to the panel width.
 *
 * Useful for register dumps and debug prints. Cells to the left
 * of the hex field are blanked.
 */
void tiku_lcd_put_hex(uint32_t value, uint8_t digits);

/**
 * @brief Display a fixed-point value, right-aligned, with the
 *        decimal point shown via the board's inter-digit dot icon.
 *
 * Renders @p value as an integer and lights the dot icon between
 * the integer and fractional cells, so e.g.
 * `tiku_lcd_put_fixed(2345, 2)` shows `   23.45` (last 2 digits
 * are the fractional part) and `tiku_lcd_put_fixed(-50, 1)` shows
 * `   -5.0`. Negative values reserve one cell for the leading
 * '-'. If the magnitude has fewer digits than @p decimals + 1, a
 * leading zero is added so the integer cell is never empty.
 * Values that would not fit (including the sign) are clamped to
 * all-9s and the dot is cleared.
 *
 * On boards that do not declare TIKU_BOARD_LCD_DOT_COUNT the
 * function is still callable but no dot is lit — the digits
 * appear without a visible separator. On boards without an LCD
 * the call is a no-op.
 *
 * @param value     Signed value to render (raw, no scaling).
 * @param decimals  Number of fractional digits. 0 behaves like
 *                  tiku_lcd_put_int(). Values >= panel width are
 *                  treated as overflow.
 */
void tiku_lcd_put_fixed(int32_t value, uint8_t decimals);

/*===========================================================================*/
/* ICONS (only available when TIKU_BOARD_LCD_HAS_ICONS)                       */
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
