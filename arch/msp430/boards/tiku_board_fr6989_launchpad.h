/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr6989_launchpad.h - MSP430FR6989 LaunchPad board definitions
 *
 * This header defines the PCB-level GPIO pin assignments for the
 * MSP-EXP430FR6989 LaunchPad development board: LEDs, buttons, and
 * other board-specific peripherals.
 *
 * Board layout (per TI MSP-EXP430FR6989 user's guide, SLAU627):
 *   - LED1 (Red)   -> P1.0
 *   - LED2 (Green) -> P9.7
 *   - Button S1    -> P1.1 (Active low)
 *   - Button S2    -> P1.2 (Active low)
 *   - LFXT crystal -> PJ.4 (LFXIN) / PJ.5 (LFXOUT) — populated
 *   - HFXT crystal -> PJ.6 (HFXIN) / PJ.7 (HFXOUT) — not populated
 *   - On-board LCD glass on P5/P6/P7/P8/P9/P10 (LCD_C kept disabled by
 *     the kernel, so pins behave as GPIO defaults)
 *
 * UART transport (compile-time):
 *   The kernel UART defaults to eUSCI_A1 on P3.4 (TX) / P3.5 (RX). On
 *   this LaunchPad those pins are simultaneously the eZ-FET backchannel
 *   and the BoosterPack J1.3/J1.4 UART pads, so the same MCU pinout
 *   covers all practical wiring choices:
 *     - on-board eZ-FET (jumpers in place)        → /dev/ttyACM0
 *     - external FT232 in place of eZ-FET (jumpers pulled, FT232 wired
 *       to the MCU side of the same J5/J6 jumper block)  → /dev/ttyUSB*
 *     - external FT232 on BoosterPack J1.3/J1.4   → /dev/ttyUSB*
 *
 *   Note: P2.0/P2.1 (eUSCI_A0) on this LaunchPad are routed to the
 *   on-board segment-LCD glass and are NOT broken out to any header;
 *   driving UART on UCA0 only makes sense on a custom carrier that has
 *   physically rerouted P2.0/P2.1. For that case, override:
 *
 *     make MCU=msp430fr6989 EXTRA_CFLAGS=-DTIKU_BOARD_UART_MODULE=0
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

#ifndef TIKU_BOARD_FR6989_LAUNCHPAD_H_
#define TIKU_BOARD_FR6989_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "MSP430FR6989 LaunchPad"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_LED_COUNT        2

/*---------------------------------------------------------------------------*/
/* LED1 (Red) - P1.0                                                         */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_LED1_INIT()      do { P1DIR |= BIT0; P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED1_ON()        do { P1OUT |= BIT0; } while(0)
#define TIKU_BOARD_LED1_OFF()       do { P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED1_TOGGLE()    do { P1OUT ^= BIT0; } while(0)

/*---------------------------------------------------------------------------*/
/* LED2 (Green) - P9.7                                                       */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_LED2_INIT()      do { P9DIR |= BIT7; P9OUT &= ~BIT7; } while(0)
#define TIKU_BOARD_LED2_ON()        do { P9OUT |= BIT7; } while(0)
#define TIKU_BOARD_LED2_OFF()       do { P9OUT &= ~BIT7; } while(0)
#define TIKU_BOARD_LED2_TOGGLE()    do { P9OUT ^= BIT7; } while(0)

/*---------------------------------------------------------------------------*/
/* UART pin selection (eUSCI_A1 on P3.4/P3.5 by default)                     */
/*---------------------------------------------------------------------------*/

/*
 * On the MSP-EXP430FR6989 LaunchPad, eUSCI_A1 (P3.4 TX / P3.5 RX) is the
 * only UART path that actually reaches a physical header — both the
 * eZ-FET backchannel and the BoosterPack J1.3/J1.4 pads land on these
 * pins. eUSCI_A0 on P2.0/P2.1 is consumed by the on-board LCD glass and
 * is unreachable without a custom carrier; expose it only as an opt-in.
 *
 * Override at compile time for a custom carrier:
 *   make MCU=msp430fr6989 EXTRA_CFLAGS=-DTIKU_BOARD_UART_MODULE=0
 *
 * FR6989 pin-mux note: eUSCI A0/A1 sit on the *primary* peripheral slot
 * of P2.0/P2.1 and P3.4/P3.5 (PxSEL0=1, PxSEL1=0). This is the OPPOSITE
 * polarity from FR5969 where UCA0 on P2.0/P2.1 is the secondary function
 * (SEL1=1, SEL0=0). Getting this wrong silently mis-muxes the pin to a
 * Timer_B function — symptom is "UART driver runs, no bytes on the
 * cable". TI's MSP-EXP430FR6989 backchannel UART example is the
 * canonical reference for the SEL polarity.
 */
#ifndef TIKU_BOARD_UART_MODULE
#define TIKU_BOARD_UART_MODULE      1   /* default: eUSCI_A1 / P3.4-P3.5 */
#endif

#if TIKU_BOARD_UART_MODULE == 1

/* eUSCI_A1 on P3.4 = TXD, P3.5 = RXD. */
#define TIKU_BOARD_UART_PINS_INIT()                                            \
    do {                                                                       \
        P3DIR |= BIT4;                                                         \
        P3DIR &= (uint8_t)~BIT5;                                               \
        P3REN &= (uint8_t)~(BIT4 | BIT5);                                      \
        P3OUT &= (uint8_t)~BIT4;                                               \
        P3SEL0 |= BIT4 | BIT5;                                                 \
        P3SEL1 &= (uint8_t)~(BIT4 | BIT5);                                     \
    } while(0)

#elif TIKU_BOARD_UART_MODULE == 0

/* eUSCI_A0 on P2.0 = TXD, P2.1 = RXD.
 * Note: these pins drive the on-board LCD on the stock LaunchPad —
 * useful only on custom carriers that have rerouted P2.0/P2.1. */
#define TIKU_BOARD_UART_PINS_INIT()                                            \
    do {                                                                       \
        P2DIR |= BIT0;                                                         \
        P2DIR &= (uint8_t)~BIT1;                                               \
        P2REN &= (uint8_t)~(BIT0 | BIT1);                                      \
        P2OUT &= (uint8_t)~BIT0;                                               \
        P2SEL0 |= BIT0 | BIT1;                                                 \
        P2SEL1 &= (uint8_t)~(BIT0 | BIT1);                                     \
    } while(0)

#else
#error "TIKU_BOARD_UART_MODULE must be 0 (eUSCI_A0) or 1 (eUSCI_A1)"
#endif

/** UART baud-rate selection from 8 MHz SMCLK (oversampling).
 *  Values from TI SLAU367 Table 30-5 for eUSCI_A @ 8 MHz.
 *
 *  Override at compile time:
 *    make MCU=msp430fr6989 UART_BAUD=115200
 *
 *  Supported: 9600 (default), 19200, 38400, 57600, 115200.
 */
#define TIKU_BOARD_UART_CLK_SEL     UCSSEL__SMCLK

#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        9600
#endif

#if   TIKU_BOARD_UART_BAUD == 9600
/*  N = 8000000/9600 = 833.33  -> BRW=52, BRF=1, BRS=0x49 */
#define TIKU_BOARD_UART_BRW         52
#define TIKU_BOARD_UART_MCTLW       ((0x49 << 8) | UCOS16 | (0x01 << 4))

#elif TIKU_BOARD_UART_BAUD == 19200
/*  N = 8000000/19200 = 416.67 -> BRW=26, BRF=0, BRS=0xB6 */
#define TIKU_BOARD_UART_BRW         26
#define TIKU_BOARD_UART_MCTLW       ((0xB6 << 8) | UCOS16 | (0x00 << 4))

#elif TIKU_BOARD_UART_BAUD == 38400
/*  N = 8000000/38400 = 208.33 -> BRW=13, BRF=0, BRS=0x84 */
#define TIKU_BOARD_UART_BRW         13
#define TIKU_BOARD_UART_MCTLW       ((0x84 << 8) | UCOS16 | (0x00 << 4))

#elif TIKU_BOARD_UART_BAUD == 57600
/*  N = 8000000/57600 = 138.89 -> BRW=8, BRF=10, BRS=0xF7 */
#define TIKU_BOARD_UART_BRW         8
#define TIKU_BOARD_UART_MCTLW       ((0xF7 << 8) | UCOS16 | (0x0A << 4))

#elif TIKU_BOARD_UART_BAUD == 115200
/*  N = 8000000/115200 = 69.44 -> BRW=4, BRF=5, BRS=0x55 */
#define TIKU_BOARD_UART_BRW         4
#define TIKU_BOARD_UART_MCTLW       ((0x55 << 8) | UCOS16 | (0x05 << 4))

#else
#error "Unsupported TIKU_BOARD_UART_BAUD (use 9600/19200/38400/57600/115200)"
#endif

/*---------------------------------------------------------------------------*/
/* Button S1 - P1.1 (Active low)                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN1_INIT()      do { P1DIR &= ~BIT1; P1REN |= BIT1; P1OUT |= BIT1; } while(0)
#define TIKU_BOARD_BTN1_PRESSED()   (!(P1IN & BIT1))

/*---------------------------------------------------------------------------*/
/* Button S2 - P1.2 (Active low)                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN2_INIT()      do { P1DIR &= ~BIT2; P1REN |= BIT2; P1OUT |= BIT2; } while(0)
#define TIKU_BOARD_BTN2_PRESSED()   (!(P1IN & BIT2))

/*---------------------------------------------------------------------------*/
/* CPU-clock-out pin availability                                            */
/*---------------------------------------------------------------------------*/

/*
 * The cpuclock test (tests/cpuclock/test_cpuclock_basic.c) historically
 * routes SMCLK out of P3.4 by setting both PxSEL bits. On this board
 * P3.4 is the UCA1 UART TX, so trampling its SEL bits would silently
 * disable serial output and time out the runner. Mark the pin busy so
 * the test skips that step.
 */
#define TIKU_BOARD_CPUCLOCK_OUT_PIN_BUSY    1

/*---------------------------------------------------------------------------*/
/* I2C on eUSCI_B0: P1.6 = SDA, P1.7 = SCL                                  */
/*---------------------------------------------------------------------------*/

/** Configure P1.6 and P1.7 for eUSCI_B0 I2C function (SEL1=1, SEL0=0). */
#define TIKU_BOARD_I2C_PINS_INIT() \
    do { P1SEL1 |= BIT6 | BIT7; P1SEL0 &= ~(BIT6 | BIT7); } while(0)

/** I2C clock prescaler for 100 kHz from 8 MHz SMCLK: 8000000/100000 = 80. */
#define TIKU_BOARD_I2C_BRW_100K     80

/** I2C clock prescaler for 400 kHz from 8 MHz SMCLK: 8000000/400000 = 20. */
#define TIKU_BOARD_I2C_BRW_400K     20

/*---------------------------------------------------------------------------*/
/* ADC12_B                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * ADC12_B is available on the FR6989 LaunchPad.
 * Internal channels: ch30 = temperature sensor, ch31 = battery monitor.
 * External channels routed to BoosterPack headers depend on PCB rev;
 * consult the LaunchPad pinout (SLAU627) before wiring analog inputs.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1

/*---------------------------------------------------------------------------*/
/* On-board segment LCD (FH-1138P, 4-mux, 96 segments)                       */
/*---------------------------------------------------------------------------*/

/*
 * The MSP-EXP430FR6989 LaunchPad carries a 6-character 14-segment
 * alphanumeric LCD plus icon segments, driven by the FR6989 LCD_C
 * peripheral in 4-mux mode. Pin/segment routing per TI SLAU627
 * Sec. 4.10 (LCD circuit) and the lcd_c_lib reference example.
 *
 * Common pins:    L0..L3   = COM0..COM3 (8mux pins repurposed)
 * Segment pins:   L4..L31, L36..L39  (28 + 4 = 32 segment lines)
 * The FH-1138P uses 4-mux at 1/3 bias with the on-chip charge pump
 * sourcing VLCD ~ 3.0 V from VDD.
 *
 * Six 14-segment alphanumeric positions, indexed left-to-right:
 *   pos 0 .. pos 5
 * Each character occupies two bytes inside the LCD memory map; the
 * arch driver knows the per-position LCDMEM index pair and pushes
 * the font byte pair into them.
 */

#define TIKU_BOARD_HAS_LCD          1
#define TIKU_BOARD_LCD_NUM_CHARS    6

/*
 * LCD pin enable mask - which Lxx pins are physically connected to
 * the FH-1138P glass on this LaunchPad. Values come from Energia's
 * LCD_Launchpad init() for FR6989, which is on-hardware verified.
 *
 *   LCDCPCTL0 (L0..L15)  = 0xFFD0 — L4,L6,L7,L8..L15
 *   LCDCPCTL1 (L16..L31) = 0xF83F — L16..L21, L27..L31
 *   LCDCPCTL2 (L32..L43) = 0x00F8 — L35..L39 (digit A4 lives here)
 *
 * (My earlier 0x0F00 mask on CPCTL2 enabled L40..L43 — pins that do
 * not exist on FR6989 — and left L36..L39 disabled, so digit A4 had
 * no LCD pins to drive: the 'U' in TIKUOS rendered as a blank cell.)
 */
#define TIKU_BOARD_LCD_PIN_MASK0    0xFFD0U
#define TIKU_BOARD_LCD_PIN_MASK1    0xF83FU
#define TIKU_BOARD_LCD_PIN_MASK2    0x00F8U

/*
 * Per-character-position LCDMEM index pair. Each alphanumeric
 * character on the FH-1138P spans two CONSECUTIVE LCDMEM bytes:
 * byte0 carries A,B,C,D,E,F,G,M and byte1 carries H,J,K,N,P,Q,DP.
 * Positions are zero-indexed left to right.
 *
 * The Energia LCD_Launchpad driver and TI's MSP-EXP430FR6989 demos
 * use 0-indexed LCDMEM[] (where LCDMEM is defined as
 * `(volatile char *) &LCDM1`, so LCDMEM[0] == LCDM1). Our arch
 * driver uses LCD_MEM_BYTE(i) which is 1-indexed (LCD_MEM_BYTE(1)
 * == LCDM1), so the constants below are Energia's pos values + 1.
 *
 * Energia A1=9 → LCDM10/LCDM11, A2=5 → LCDM6/LCDM7, etc.
 */
#define TIKU_BOARD_LCD_POS0_BYTE0   10      /* A1 (leftmost) - LCDM10 */
#define TIKU_BOARD_LCD_POS0_BYTE1   11
#define TIKU_BOARD_LCD_POS1_BYTE0   6       /* A2 - LCDM6 */
#define TIKU_BOARD_LCD_POS1_BYTE1   7
#define TIKU_BOARD_LCD_POS2_BYTE0   4       /* A3 - LCDM4 */
#define TIKU_BOARD_LCD_POS2_BYTE1   5
#define TIKU_BOARD_LCD_POS3_BYTE0   19      /* A4 - LCDM19 */
#define TIKU_BOARD_LCD_POS3_BYTE1   20
#define TIKU_BOARD_LCD_POS4_BYTE0   15      /* A5 - LCDM15 */
#define TIKU_BOARD_LCD_POS4_BYTE1   16
#define TIKU_BOARD_LCD_POS5_BYTE0   8       /* A6 (rightmost) - LCDM8 */
#define TIKU_BOARD_LCD_POS5_BYTE1   9

/*
 * Each digit's byte1 LCDMEM cell is shared with two icon segments:
 *   bit 0 = a "dot" between digits  (DOT1..DOT5 / RX)
 *   bit 2 = a secondary marker      (MINUS1, COLON2, RADIO, COLON4,
 *                                    DEG5, TX)
 * Any digit-write must preserve those bits so an icon turned on by
 * the application doesn't get clobbered when the digit changes. The
 * arch driver consults this mask in tiku_lcd_arch_putchar().
 */
#define TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK   0x05U

/*---------------------------------------------------------------------------*/
/* On-board LCD - icon segments                                              */
/*---------------------------------------------------------------------------*/

/*
 * The FH-1138P glass on the FR6989 LaunchPad carries a fixed set
 * of icon segments alongside the six 14-segment digit positions.
 * The (LCDMEM-byte-index, bit-mask) pairs below come from the
 * Energia LCD_Launchpad reference (on-hardware verified). Indices
 * are in our 1-indexed LCD_MEM_BYTE() form (Energia's value + 1).
 *
 * IDs are dense, in icon-table order — keep TIKU_LCD_ICON_* and
 * TIKU_BOARD_LCD_ICON_TABLE in lockstep when adding new icons.
 */
#define TIKU_BOARD_LCD_HAS_ICONS    1
#define TIKU_BOARD_LCD_NUM_ICONS    24

#define TIKU_LCD_ICON_MARK          0
#define TIKU_LCD_ICON_R             1
#define TIKU_LCD_ICON_HEART         2
#define TIKU_LCD_ICON_CLOCK         3
#define TIKU_LCD_ICON_DOT3          4
#define TIKU_LCD_ICON_RADIO         5
#define TIKU_LCD_ICON_DOT2          6
#define TIKU_LCD_ICON_COLON2        7
#define TIKU_LCD_ICON_RX            8
#define TIKU_LCD_ICON_TX            9
#define TIKU_LCD_ICON_DOT1          10
#define TIKU_LCD_ICON_MINUS1        11
#define TIKU_LCD_ICON_BAT_POL       12
#define TIKU_LCD_ICON_BAT1          13
#define TIKU_LCD_ICON_BAT3          14
#define TIKU_LCD_ICON_BAT5          15
#define TIKU_LCD_ICON_DOT5          16
#define TIKU_LCD_ICON_DEG5          17
#define TIKU_LCD_ICON_BAT_ENDS      18
#define TIKU_LCD_ICON_BAT0          19
#define TIKU_LCD_ICON_BAT2          20
#define TIKU_LCD_ICON_BAT4          21
#define TIKU_LCD_ICON_DOT4          22
#define TIKU_LCD_ICON_COLON4        23

#define TIKU_BOARD_LCD_ICON_TABLE                                              \
    { 3,  0x01 },   /* MARK     */                                             \
    { 3,  0x02 },   /* R        */                                             \
    { 3,  0x04 },   /* HEART    */                                             \
    { 3,  0x08 },   /* CLOCK    */                                             \
    { 5,  0x01 },   /* DOT3     */                                             \
    { 5,  0x04 },   /* RADIO    */                                             \
    { 7,  0x01 },   /* DOT2     */                                             \
    { 7,  0x04 },   /* COLON2   */                                             \
    { 9,  0x01 },   /* RX       */                                             \
    { 9,  0x04 },   /* TX       */                                             \
    { 11, 0x01 },   /* DOT1     */                                             \
    { 11, 0x04 },   /* MINUS1   */                                             \
    { 14, 0x10 },   /* BAT_POL  */                                             \
    { 14, 0x20 },   /* BAT1     */                                             \
    { 14, 0x40 },   /* BAT3     */                                             \
    { 14, 0x80 },   /* BAT5     */                                             \
    { 16, 0x01 },   /* DOT5     */                                             \
    { 16, 0x04 },   /* DEG5     */                                             \
    { 18, 0x10 },   /* BAT_ENDS */                                             \
    { 18, 0x20 },   /* BAT0     */                                             \
    { 18, 0x40 },   /* BAT2     */                                             \
    { 18, 0x80 },   /* BAT4     */                                             \
    { 20, 0x01 },   /* DOT4     */                                             \
    { 20, 0x04 }    /* COLON4   */

/*
 * Decimal-point map for tiku_lcd_put_fixed(): the FH-1138P has five
 * inter-digit dots, one between each adjacent pair of the six digit
 * positions. Number them 1..5 left-to-right; dot N sits to the right
 * of digit position (N-1) in 0-indexed terms.
 *
 *   pos: 0 . 1 . 2 . 3 . 4 . 5
 *        ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^
 *        digits and DOT1..DOT5
 */
#define TIKU_BOARD_LCD_DOT_COUNT    5
#define TIKU_BOARD_LCD_DOT_ICON(n)                                             \
    ((n) == 1 ? TIKU_LCD_ICON_DOT1 :                                           \
     (n) == 2 ? TIKU_LCD_ICON_DOT2 :                                           \
     (n) == 3 ? TIKU_LCD_ICON_DOT3 :                                           \
     (n) == 4 ? TIKU_LCD_ICON_DOT4 :                                           \
     (n) == 5 ? TIKU_LCD_ICON_DOT5 : 0xFFU)

#endif /* TIKU_BOARD_FR6989_LAUNCHPAD_H_ */
