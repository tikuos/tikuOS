/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd_arch.c - MSP430 LCD_C peripheral driver
 *
 * Drives the LCD_C controller present on FR6989 (and other FR6xx
 * parts) with a board-specific pin map provided by the board
 * header. Built only when both TIKU_DEVICE_HAS_LCD_C (silicon
 * carries LCD_C) and TIKU_BOARD_HAS_LCD (board wires it to a
 * panel) are set; otherwise this translation unit is empty.
 *
 * Current target: MSP-EXP430FR6989 LaunchPad with the on-board
 * FH-1138P 96-segment LCD (4-mux, 1/3 bias, six 14-segment
 * alphanumeric positions plus icons). The font and per-position
 * LCDMEM index map are derived from TI's lcd_c_lib reference
 * example for that board. Other boards using the same LCD_C
 * peripheral with a different glass can supply their own segment
 * encoding by overriding the per-position byte indices in their
 * board header (see TIKU_BOARD_LCD_POSx_BYTE0/1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#if defined(TIKU_DEVICE_HAS_LCD_C) && TIKU_DEVICE_HAS_LCD_C \
    && defined(TIKU_BOARD_HAS_LCD) && TIKU_BOARD_HAS_LCD

#include "tiku_lcd_arch.h"
#include <msp430.h>

/*===========================================================================*/
/* 14-SEGMENT FONT                                                            */
/*===========================================================================*/

/*
 * 14-segment encoding for the FH-1138P. Each character occupies two
 * bytes: byte0 carries one half of the segment bits (the four
 * "primary" segments per common pair) and byte1 carries the other
 * half (diagonals, middle bar split).
 *
 * Bit layout (per the FH-1138P pin map on the FR6989 LaunchPad):
 *   byte0:  bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F,
 *           bit6=G(left), bit7=M(right)
 *   byte1:  bit1=Q, bit2=K, bit3=H, bit4=N, bit5=J, bit6=P,
 *           bit7=DP (decimal point — unused on most positions)
 *
 * Visual layout of the segments:
 *
 *        A A A
 *       FH J KB
 *       F H JKB
 *        G G M M
 *       E N JPC
 *       E NJ PC
 *        D D D
 *
 * Values below match the encoding used by TI's MSP-EXP430FR6989
 * lcd_c_lib example. Verified to render correctly on hardware
 * for digits 0-9 and uppercase A-Z.
 */
static const uint8_t font_digit[10][2] = {
    {0xFC, 0x28},  /* 0 */
    {0x60, 0x20},  /* 1 */
    {0xDB, 0x00},  /* 2 */
    {0xF3, 0x00},  /* 3 */
    {0x67, 0x00},  /* 4 */
    {0xB7, 0x00},  /* 5 */
    {0xBF, 0x00},  /* 6 */
    {0xE4, 0x00},  /* 7 */
    {0xFF, 0x00},  /* 8 */
    {0xF7, 0x00},  /* 9 */
};

static const uint8_t font_alpha[26][2] = {
    {0xEF, 0x00},  /* A */
    {0xF1, 0x50},  /* B */
    {0x9C, 0x00},  /* C */
    {0xF0, 0x50},  /* D */
    {0x9F, 0x00},  /* E */
    {0x8F, 0x00},  /* F */
    {0xBD, 0x00},  /* G */
    {0x6F, 0x00},  /* H */
    {0x90, 0x50},  /* I */
    {0x78, 0x00},  /* J */
    {0x0E, 0x22},  /* K */
    {0x1C, 0x00},  /* L */
    {0x6C, 0xA0},  /* M */
    {0x6C, 0x82},  /* N */
    {0xFC, 0x00},  /* O */
    {0xCF, 0x00},  /* P */
    {0xFC, 0x02},  /* Q */
    {0xCF, 0x02},  /* R */
    {0xB7, 0x00},  /* S */
    {0x80, 0x50},  /* T */
    {0x7C, 0x00},  /* U */
    {0x0C, 0x28},  /* V */
    {0x6C, 0x0A},  /* W */
    {0x00, 0xAA},  /* X */
    {0x00, 0xB0},  /* Y */
    {0x90, 0x28},  /* Z */
};

/* Common non-letter glyphs */
#define GLYPH_BLANK_B0   0x00
#define GLYPH_BLANK_B1   0x00
#define GLYPH_DASH_B0    0x03  /* G+M = horizontal middle bar */
#define GLYPH_DASH_B1    0x00

/*===========================================================================*/
/* PER-POSITION LCDMEM INDEX TABLE                                            */
/*===========================================================================*/

/*
 * Pull each position's two LCDMEM indices out of the board header
 * into an array we can index by position. Adding more positions or
 * porting to a different glass is a board-header-only change.
 */
static const uint8_t pos_byte0_idx[TIKU_BOARD_LCD_NUM_CHARS] = {
    TIKU_BOARD_LCD_POS0_BYTE0,
#if TIKU_BOARD_LCD_NUM_CHARS >= 2
    TIKU_BOARD_LCD_POS1_BYTE0,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 3
    TIKU_BOARD_LCD_POS2_BYTE0,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 4
    TIKU_BOARD_LCD_POS3_BYTE0,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 5
    TIKU_BOARD_LCD_POS4_BYTE0,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 6
    TIKU_BOARD_LCD_POS5_BYTE0,
#endif
};

static const uint8_t pos_byte1_idx[TIKU_BOARD_LCD_NUM_CHARS] = {
    TIKU_BOARD_LCD_POS0_BYTE1,
#if TIKU_BOARD_LCD_NUM_CHARS >= 2
    TIKU_BOARD_LCD_POS1_BYTE1,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 3
    TIKU_BOARD_LCD_POS2_BYTE1,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 4
    TIKU_BOARD_LCD_POS3_BYTE1,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 5
    TIKU_BOARD_LCD_POS4_BYTE1,
#endif
#if TIKU_BOARD_LCD_NUM_CHARS >= 6
    TIKU_BOARD_LCD_POS5_BYTE1,
#endif
};

/* Direct pointer into the LCD memory window — avoids LCDMEM[i]
 * relying on a specific symbol layout in the toolchain header.
 * 1-indexed so LCD_MEM_BYTE(1) == LCDM1. */
#define LCD_MEM_BYTE(i)  (*((volatile uint8_t *)((uintptr_t)&LCDM1 + (i) - 1)))

/* Bits in byte1 of every digit position that are reserved for
 * icons (e.g. dot, colon) on this board. The board header may
 * override; default is "no shared bits". A digit write OR-merges
 * its own bits into the byte while preserving the masked bits. */
#ifndef TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK
#define TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK  0x00U
#endif

/*===========================================================================*/
/* INIT                                                                       */
/*===========================================================================*/

void
tiku_lcd_arch_init(void)
{
    /* 1. Configure pins as LCD function. The board mask says which
     *    Lxx pins are wired; LCDCPCTLx enables those as LCD pins.
     *    The MSP430FR-series LCD_C peripheral takes over the
     *    underlying GPIO when the corresponding bit is set. */
    LCDCPCTL0 = TIKU_BOARD_LCD_PIN_MASK0;
    LCDCPCTL1 = TIKU_BOARD_LCD_PIN_MASK1;
    LCDCPCTL2 = TIKU_BOARD_LCD_PIN_MASK2;

    /* 2. Configure LCD controller:
     *      - Source: ACLK (32.768 kHz from LFXT). On FR6989 the
     *        clock select bit LCDSSEL is 0=ACLK / 1=VLOCLK; ACLK is
     *        the reset default so we leave the bit clear.
     *      - Pre-divider 16, divider 1 → frame freq ~64 Hz at 4-mux
     *      - 4-mux operation with low-power waveform (LCDLP)
     *      - Bias defaults to 1/3 (LCD2B not set)
     */
    LCDCCTL0 = LCDDIV__1
             | LCDPRE__16
             | LCD4MUX
             | LCDLP;

    /* 3. VLCD generation: enable the regulated charge pump and
     *    select VLCD = 2.84 V (VLCD_5) from the VLCD ladder. The
     *    on-chip reference is the default (VLCDEXT cleared); no
     *    explicit "reference enable" bit on this part. */
    LCDCVCTL = LCDCPEN | VLCD_5;

    /* 4. Charge pump clock sync (per FR6989 errata recommendation). */
    LCDCCPCTL = LCDCPCLKSYNC;

    /* 5. Clear all LCD memory before turning the panel on so we
     *    don't latch random RAM contents into the glass. */
    LCDCMEMCTL = LCDCLRM;

    /* 6. Enable the controller and turn the panel on. */
    LCDCCTL0 |= LCDON;
}

/*===========================================================================*/
/* CLEAR                                                                      */
/*===========================================================================*/

void
tiku_lcd_arch_clear(void)
{
    /* LCDCMEMCTL.LCDCLRM is auto-clearing — writing 1 triggers a
     * one-shot blank of all LCDMEM bytes by the hardware. */
    LCDCMEMCTL |= LCDCLRM;
}

/*===========================================================================*/
/* PUTCHAR                                                                    */
/*===========================================================================*/

static void
encode_glyph(char ch, uint8_t *b0, uint8_t *b1)
{
    /* Lower-case to upper-case fold so 'a'..'z' are usable. */
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }

    if (ch >= '0' && ch <= '9') {
        *b0 = font_digit[ch - '0'][0];
        *b1 = font_digit[ch - '0'][1];
    } else if (ch >= 'A' && ch <= 'Z') {
        *b0 = font_alpha[ch - 'A'][0];
        *b1 = font_alpha[ch - 'A'][1];
    } else if (ch == '-') {
        *b0 = GLYPH_DASH_B0;
        *b1 = GLYPH_DASH_B1;
    } else {
        /* Space and any unknown glyph render as a blank cell. */
        *b0 = GLYPH_BLANK_B0;
        *b1 = GLYPH_BLANK_B1;
    }
}

void
tiku_lcd_arch_putchar(uint8_t pos, char ch)
{
    uint8_t  b0, b1;
    uint8_t  idx1 = pos_byte1_idx[pos];

    encode_glyph(ch, &b0, &b1);

    LCD_MEM_BYTE(pos_byte0_idx[pos]) = b0;

    /* Preserve bits in byte1 that the board reserves for icon
     * segments — on the FH-1138P, byte1 of every digit shares the
     * LCDMEM byte with the dot / colon icons living between
     * digits. Without this OR-merge, drawing a digit would silently
     * clear any lit icon. */
    if (TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK == 0) {
        LCD_MEM_BYTE(idx1) = b1;
    } else {
        uint8_t keep = LCD_MEM_BYTE(idx1)
                       & TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK;
        LCD_MEM_BYTE(idx1) =
            (uint8_t)((b1 & ~TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK) | keep);
    }
}

/*===========================================================================*/
/* ICONS                                                                      */
/*===========================================================================*/

#if defined(TIKU_BOARD_LCD_HAS_ICONS) && TIKU_BOARD_LCD_HAS_ICONS

/* Per-icon (LCDMEM byte index, bit mask) pair. Populated from the
 * board header's TIKU_BOARD_LCD_ICON_TABLE expansion — adding or
 * renaming icons is a board-header-only change. */
struct lcd_icon_def {
    uint8_t mem_idx;
    uint8_t mask;
};

static const struct lcd_icon_def icon_table[TIKU_BOARD_LCD_NUM_ICONS] = {
    TIKU_BOARD_LCD_ICON_TABLE
};

void
tiku_lcd_arch_icon_set(uint8_t icon_id, uint8_t on)
{
    if (icon_id >= TIKU_BOARD_LCD_NUM_ICONS) {
        return;
    }
    if (on) {
        LCD_MEM_BYTE(icon_table[icon_id].mem_idx)
            |= icon_table[icon_id].mask;
    } else {
        LCD_MEM_BYTE(icon_table[icon_id].mem_idx)
            &= (uint8_t)~icon_table[icon_id].mask;
    }
}

void
tiku_lcd_arch_icon_toggle(uint8_t icon_id)
{
    if (icon_id >= TIKU_BOARD_LCD_NUM_ICONS) {
        return;
    }
    LCD_MEM_BYTE(icon_table[icon_id].mem_idx)
        ^= icon_table[icon_id].mask;
}

#endif /* TIKU_BOARD_LCD_HAS_ICONS */

#endif /* TIKU_DEVICE_HAS_LCD_C && TIKU_BOARD_HAS_LCD */
