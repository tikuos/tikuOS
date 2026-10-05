/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lcd_arch.c - MSP430 LCD_C peripheral driver.
 *
 * Drives LCD_C with the board's pin mask and per-position LCDMEM bytes,
 * built only where the device has LCD_C and the build defines
 * TIKU_BOARD_HAS_LCD.  The 14-segment font suits the FH-1138P glass.
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
 * 14-segment encoding for the FH-1138P.  Each character occupies two
 * bytes: byte0 carries the outer segments and the middle bars, byte1 the
 * diagonals and the centre verticals.
 *
 * Bit layout (per the FH-1138P pin map on the FR6989 LaunchPad):
 *   byte0:  bit7=A, bit6=B, bit5=C, bit4=D, bit3=E, bit2=F,
 *           bit1=G (left bar), bit0=M (right bar)
 *   byte1:  bit7=H, bit6=J, bit5=K, bit4=P, bit3=N, bit1=Q;
 *           bits 0 and 2 belong to icons
 *
 * Visual layout of the segments:
 *
 *        AAAAAAA
 *       F H J K B
 *       F  HJK  B
 *        GGG MMM
 *       E  NPQ  C
 *       E N P Q C
 *        DDDDDDD
 *
 * Values below match the encoding used by TI's MSP-EXP430FR6989
 * lcd_c_lib example, for digits 0-9 and uppercase A-Z.
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
 * Each position's two LCDMEM indices, from the board header's
 * TIKU_BOARD_LCD_POSn_BYTE0/1, in arrays indexed by position.  The
 * tables take up to six positions.
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

/* LCD memory byte i, counted from 1: LCD_MEM_BYTE(1) is LCDM1.  It is
 * addressed from &LCDM1, so it needs no LCDMEM array in the toolchain
 * header. */
#define LCD_MEM_BYTE(i)  (*((volatile uint8_t *)((uintptr_t)&LCDM1 + (i) - 1)))

/* Byte1 bits that a digit write leaves alone because icons share them.
 * A board header without a mask gets 0: digit writes own all of
 * byte1. */
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
     *    Lxx pins are wired; a set LCDCPCTLx bit hands that pin from
     *    GPIO to LCD_C. */
    LCDCPCTL0 = TIKU_BOARD_LCD_PIN_MASK0;
    LCDCPCTL1 = TIKU_BOARD_LCD_PIN_MASK1;
    LCDCPCTL2 = TIKU_BOARD_LCD_PIN_MASK2;

    /* 2. Configure LCD controller:
     *      - Source: ACLK (LCDSSEL = 0, the reset value, left clear):
     *        32.768 kHz when LFXT runs.
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

    /* 4. Charge-pump clock synchronisation, as the FR6989 errata
     *    recommends. */
    LCDCCPCTL = LCDCPCLKSYNC;

    /* 5. Clear LCD memory before the panel turns on, so the glass shows
     *    no leftover RAM contents. */
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

/**
 * @brief Look up the two segment bytes of @p ch.
 *
 * Digits, letters of either case and '-' have glyphs; anything else is
 * blank.
 */
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

    /* Bits in TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK belong to icons
     * that share byte1 (the dots and colons between digits on the
     * FH-1138P); the write keeps them, so a lit icon stays lit. */
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

/* Per-icon (LCDMEM byte index, bit mask) pair, filled from the board
 * header's TIKU_BOARD_LCD_ICON_TABLE. */
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
