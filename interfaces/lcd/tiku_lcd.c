/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd.c - Generic segment-LCD glue
 *
 * Forwards the platform-independent API to the active arch driver
 * (currently arch/msp430/tiku_lcd_arch.c). Owns the formatting
 * helpers — alignment, integer-to-digits, hex — so each arch only
 * has to implement init/clear/putchar (+ optional icon hook).
 *
 * On boards without a segment LCD (TIKU_BOARD_HAS_LCD == 0) every
 * entry point is a no-op so portable code that calls into the LCD
 * compiles and links everywhere without #ifdef.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_lcd.h"

#if TIKU_BOARD_HAS_LCD
#include "arch/msp430/tiku_lcd_arch.h"
#endif

/*===========================================================================*/
/* PRESENCE                                                                   */
/*===========================================================================*/

uint8_t
tiku_lcd_num_chars(void)
{
    return TIKU_BOARD_LCD_NUM_CHARS;
}

/*===========================================================================*/
/* LIFECYCLE                                                                  */
/*===========================================================================*/

void
tiku_lcd_init(void)
{
#if TIKU_BOARD_HAS_LCD
    tiku_lcd_arch_init();
    tiku_lcd_arch_clear();
#endif
}

void
tiku_lcd_clear(void)
{
#if TIKU_BOARD_HAS_LCD
    tiku_lcd_arch_clear();
#endif
}

/*===========================================================================*/
/* CHARACTER / STRING WRITES                                                  */
/*===========================================================================*/

void
tiku_lcd_putchar(uint8_t pos, char ch)
{
#if TIKU_BOARD_HAS_LCD
    if (pos >= TIKU_BOARD_LCD_NUM_CHARS) {
        return;
    }
    tiku_lcd_arch_putchar(pos, ch);
#else
    (void)pos;
    (void)ch;
#endif
}

void
tiku_lcd_puts(const char *s)
{
#if TIKU_BOARD_HAS_LCD
    uint8_t i;
    for (i = 0; i < TIKU_BOARD_LCD_NUM_CHARS; i++) {
        char ch = (s != (const char *)0 && *s) ? *s++ : ' ';
        tiku_lcd_arch_putchar(i, ch);
    }
#else
    (void)s;
#endif
}

void
tiku_lcd_puts_right(const char *s)
{
#if TIKU_BOARD_HAS_LCD
    uint8_t  width  = TIKU_BOARD_LCD_NUM_CHARS;
    uint8_t  len    = 0;
    uint8_t  i;
    uint8_t  pad;
    const char *p;

    if (s == (const char *)0) {
        for (i = 0; i < width; i++) {
            tiku_lcd_arch_putchar(i, ' ');
        }
        return;
    }

    /* Measure (clamped). Avoids strlen so we don't pull in <string.h>
     * on parts where it isn't already linked. */
    for (p = s; *p && len < width; p++) {
        len++;
    }

    pad = (uint8_t)(width - len);
    for (i = 0; i < pad; i++) {
        tiku_lcd_arch_putchar(i, ' ');
    }
    for (i = 0; i < len; i++) {
        tiku_lcd_arch_putchar((uint8_t)(pad + i), s[i]);
    }
#else
    (void)s;
#endif
}

void
tiku_lcd_puts_at(uint8_t pos, const char *s)
{
#if TIKU_BOARD_HAS_LCD
    if (pos >= TIKU_BOARD_LCD_NUM_CHARS || s == (const char *)0) {
        return;
    }
    while (pos < TIKU_BOARD_LCD_NUM_CHARS && *s) {
        tiku_lcd_arch_putchar(pos++, *s++);
    }
#else
    (void)pos;
    (void)s;
#endif
}

/*===========================================================================*/
/* NUMBER FORMATTING                                                          */
/*===========================================================================*/

#if TIKU_BOARD_HAS_LCD
/* Render a base-10 unsigned value into the END of buf, returning the
 * number of digits written. buf is left untouched outside the digit
 * span — caller is responsible for blanking the rest. Returns 0 if
 * the value would overflow `width` cells, in which case the buffer
 * is filled with '9's by the caller. */
static uint8_t
fmt_uint_into(char *buf, uint8_t width, uint32_t value)
{
    uint8_t  digits = 0;
    uint32_t v      = value;

    if (v == 0) {
        buf[width - 1] = '0';
        return 1;
    }
    while (v > 0 && digits < width) {
        buf[width - 1 - digits] = (char)('0' + (v % 10U));
        v /= 10U;
        digits++;
    }
    if (v > 0) {
        return 0;   /* overflow */
    }
    return digits;
}

static void
flush_buf(const char *buf)
{
    uint8_t i;
    for (i = 0; i < TIKU_BOARD_LCD_NUM_CHARS; i++) {
        tiku_lcd_arch_putchar(i, buf[i]);
    }
}

static void
fill_buf(char *buf, char ch)
{
    uint8_t i;
    for (i = 0; i < TIKU_BOARD_LCD_NUM_CHARS; i++) {
        buf[i] = ch;
    }
}
#endif

void
tiku_lcd_put_uint(uint32_t value)
{
#if TIKU_BOARD_HAS_LCD
    char buf[TIKU_BOARD_LCD_NUM_CHARS];

    fill_buf(buf, ' ');
    if (fmt_uint_into(buf, TIKU_BOARD_LCD_NUM_CHARS, value) == 0) {
        fill_buf(buf, '9');
    }
    flush_buf(buf);
#else
    (void)value;
#endif
}

void
tiku_lcd_put_int(int32_t value)
{
#if TIKU_BOARD_HAS_LCD
    char     buf[TIKU_BOARD_LCD_NUM_CHARS];
    uint32_t mag;
    uint8_t  digits;
    uint8_t  width = TIKU_BOARD_LCD_NUM_CHARS;

    fill_buf(buf, ' ');

    if (value >= 0) {
        if (fmt_uint_into(buf, width, (uint32_t)value) == 0) {
            fill_buf(buf, '9');
        }
        flush_buf(buf);
        return;
    }

    /* Negative: reserve cell 0 for the sign, render magnitude into
     * the remaining width-1 cells. INT32_MIN handled via unsigned
     * negate trick (-(uint32_t)v == |v| for two's complement). */
    mag = (uint32_t)(-(value + 1)) + 1U;
    digits = fmt_uint_into(buf + 1, (uint8_t)(width - 1), mag);
    if (digits == 0) {
        /* Overflow: "-9999.." across full width. */
        fill_buf(buf, '9');
        buf[0] = '-';
        flush_buf(buf);
        return;
    }
    buf[width - 1 - digits] = '-';
    flush_buf(buf);
#else
    (void)value;
#endif
}

/*---------------------------------------------------------------------------*/
/* FIXED-POINT (uses inter-digit dot icons if the board exposes them)        */
/*---------------------------------------------------------------------------*/

#if TIKU_BOARD_HAS_LCD && \
    defined(TIKU_BOARD_LCD_DOT_COUNT) && TIKU_BOARD_LCD_DOT_COUNT > 0

/* Turn off every inter-digit dot. Used before lighting the one we
 * want, so a previous put_fixed doesn't leave a stale separator. */
static void
clear_all_dots(void)
{
    uint8_t n;
    for (n = 1; n <= TIKU_BOARD_LCD_DOT_COUNT; n++) {
        uint8_t id = TIKU_BOARD_LCD_DOT_ICON(n);
        if (id != 0xFFU) {
            tiku_lcd_arch_icon_set(id, 0);
        }
    }
}

static void
light_dot(uint8_t n)
{
    uint8_t id;
    if (n < 1 || n > TIKU_BOARD_LCD_DOT_COUNT) {
        return;
    }
    id = TIKU_BOARD_LCD_DOT_ICON(n);
    if (id != 0xFFU) {
        tiku_lcd_arch_icon_set(id, 1);
    }
}
#endif

void
tiku_lcd_put_fixed(int32_t value, uint8_t decimals)
{
#if TIKU_BOARD_HAS_LCD
    char     buf[TIKU_BOARD_LCD_NUM_CHARS];
    uint8_t  width = TIKU_BOARD_LCD_NUM_CHARS;
    uint8_t  digits;
    uint8_t  used;
    uint8_t  neg = 0;
    uint32_t mag;

    fill_buf(buf, ' ');

    if (value < 0) {
        neg = 1;
        mag = (uint32_t)(-(value + 1)) + 1U;
    } else {
        mag = (uint32_t)value;
    }

    /* Render magnitude into the right end of the buffer. */
    digits = fmt_uint_into(buf, width, mag);
    if (digits == 0) {
        /* Magnitude doesn't fit at all: clamp to 9s, clear dot. */
        fill_buf(buf, '9');
        if (neg) {
            buf[0] = '-';
        }
#if defined(TIKU_BOARD_LCD_DOT_COUNT) && TIKU_BOARD_LCD_DOT_COUNT > 0
        clear_all_dots();
#endif
        flush_buf(buf);
        return;
    }

    /* Pad with a leading zero so the integer cell is never empty
     * (e.g. value=23, decimals=2 → "023" → "0.23"). */
    if (digits <= decimals) {
        uint8_t zeros = (uint8_t)((decimals + 1U) - digits);
        uint8_t i;
        for (i = 0; i < zeros; i++) {
            if (digits + i >= width) {
                /* Ran out of room — overflow path. */
                fill_buf(buf, '9');
                if (neg) {
                    buf[0] = '-';
                }
#if defined(TIKU_BOARD_LCD_DOT_COUNT) && TIKU_BOARD_LCD_DOT_COUNT > 0
                clear_all_dots();
#endif
                flush_buf(buf);
                return;
            }
            buf[width - 1 - (digits + i)] = '0';
        }
        digits = (uint8_t)(digits + zeros);
    }

    used = digits;
    if (neg) {
        if (used >= width) {
            /* Sign won't fit either — overflow. */
            fill_buf(buf, '9');
            buf[0] = '-';
#if defined(TIKU_BOARD_LCD_DOT_COUNT) && TIKU_BOARD_LCD_DOT_COUNT > 0
            clear_all_dots();
#endif
            flush_buf(buf);
            return;
        }
        buf[width - 1 - used] = '-';
        used++;
    }

    /* Suppress used (silence -Wunused on the no-dots path). */
    (void)used;

#if defined(TIKU_BOARD_LCD_DOT_COUNT) && TIKU_BOARD_LCD_DOT_COUNT > 0
    clear_all_dots();
    /* Dot index is 1-based: dot N sits to the right of digit
     * position (N-1), i.e. between cells (N-1) and N. We want the
     * dot between the last integer digit and the first fractional
     * digit, which is at cell index (width - decimals). */
    if (decimals > 0 && decimals < width) {
        light_dot((uint8_t)(width - decimals));
    }
#endif

    flush_buf(buf);
#else
    (void)value;
    (void)decimals;
#endif
}

void
tiku_lcd_put_hex(uint32_t value, uint8_t digits)
{
#if TIKU_BOARD_HAS_LCD
    char     buf[TIKU_BOARD_LCD_NUM_CHARS];
    uint8_t  width = TIKU_BOARD_LCD_NUM_CHARS;
    uint8_t  i;
    uint32_t v = value;

    if (digits == 0) {
        digits = 1;
    }
    if (digits > width) {
        digits = width;
    }

    fill_buf(buf, ' ');
    for (i = 0; i < digits; i++) {
        uint8_t nib = (uint8_t)(v & 0xFU);
        char    glyph = (nib < 10U)
                        ? (char)('0' + nib)
                        : (char)('A' + (nib - 10U));
        buf[width - 1 - i] = glyph;
        v >>= 4;
    }
    flush_buf(buf);
#else
    (void)value;
    (void)digits;
#endif
}

/*===========================================================================*/
/* ICONS                                                                      */
/*===========================================================================*/

#if defined(TIKU_BOARD_LCD_HAS_ICONS) && TIKU_BOARD_LCD_HAS_ICONS

uint8_t
tiku_lcd_icon_count(void)
{
    return TIKU_BOARD_LCD_NUM_ICONS;
}

void
tiku_lcd_icon_on(uint8_t icon_id)
{
    tiku_lcd_arch_icon_set(icon_id, 1);
}

void
tiku_lcd_icon_off(uint8_t icon_id)
{
    tiku_lcd_arch_icon_set(icon_id, 0);
}

void
tiku_lcd_icon_toggle(uint8_t icon_id)
{
    tiku_lcd_arch_icon_toggle(icon_id);
}

void
tiku_lcd_icon_set(uint8_t icon_id, uint8_t on)
{
    tiku_lcd_arch_icon_set(icon_id, on);
}

void
tiku_lcd_icons_clear(void)
{
    uint8_t i;
    for (i = 0; i < TIKU_BOARD_LCD_NUM_ICONS; i++) {
        tiku_lcd_arch_icon_set(i, 0);
    }
}

#endif /* TIKU_BOARD_LCD_HAS_ICONS */
