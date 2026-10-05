/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr6989_launchpad.h - MSP430FR6989 LaunchPad board definitions.
 *
 * PCB-level GPIO assignments for the MSP-EXP430FR6989 per TI SLAU627: LEDs,
 * buttons, UART, I2C, ADC and the on-board LCD glass.  The UART transport
 * choices are documented at TIKU_BOARD_UART_MODULE below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_FR6989_LAUNCHPAD_H_
#define TIKU_BOARD_FR6989_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** Human-readable board name. */
#define TIKU_BOARD_NAME             "MSP430FR6989 LaunchPad"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

/** Number of on-board LEDs; interfaces/led indexes them from 0. */
#define TIKU_BOARD_LED_COUNT        2

/*---------------------------------------------------------------------------*/
/* LED1 (RED) - P1.0                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @name LED1 on P1.0, lit when the pin is high
 * INIT makes the pin an output and turns the LED off.
 * @{
 */
#define TIKU_BOARD_LED1_INIT()      do { P1DIR |= BIT0; P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED1_ON()        do { P1OUT |= BIT0; } while(0)
#define TIKU_BOARD_LED1_OFF()       do { P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED1_TOGGLE()    do { P1OUT ^= BIT0; } while(0)
/** @} */

/*---------------------------------------------------------------------------*/
/* LED2 (GREEN) - P9.7                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @name LED2 on P9.7, lit when the pin is high
 * INIT makes the pin an output and turns the LED off.
 * @{
 */
#define TIKU_BOARD_LED2_INIT()      do { P9DIR |= BIT7; P9OUT &= ~BIT7; } while(0)
#define TIKU_BOARD_LED2_ON()        do { P9OUT |= BIT7; } while(0)
#define TIKU_BOARD_LED2_OFF()       do { P9OUT &= ~BIT7; } while(0)
#define TIKU_BOARD_LED2_TOGGLE()    do { P9OUT ^= BIT7; } while(0)
/** @} */

/*---------------------------------------------------------------------------*/
/* UART PIN SELECTION (eUSCI_A1 ON P3.4/P3.5 BY DEFAULT)                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief eUSCI_A module of the UART: 1 (the default) or 0.
 *
 * 1 is eUSCI_A1 on P3.4 (TX) and P3.5 (RX), the pins wired to the eZ-FET
 * backchannel and BoosterPack J1.3/J1.4.  0 is eUSCI_A0 on P2.0/P2.1, which
 * the LCD glass occupies on this LaunchPad; it is for custom carriers.
 *
 * @note make MCU=msp430fr6989 EXTRA_CFLAGS=-DTIKU_BOARD_UART_MODULE=0
 *       selects module 0.
 */
#ifndef TIKU_BOARD_UART_MODULE
#define TIKU_BOARD_UART_MODULE      1   /* default: eUSCI_A1 / P3.4-P3.5 */
#endif

/*
 * Both UARTs are the primary function of their pins on this part (PxSEL0 = 1,
 * PxSEL1 = 0).  PxSEL1 = 1, PxSEL0 = 0 selects a Timer_B function instead:
 * the UART driver then runs and no bytes reach the cable.
 */
#if TIKU_BOARD_UART_MODULE == 1

/** Hand P3.4 (TXD) and P3.5 (RXD) to eUSCI_A1. */
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

/**
 * Hand P2.0 (TXD) and P2.1 (RXD) to eUSCI_A0.  On the stock LaunchPad these
 * pins drive the LCD glass.
 */
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

/*
 * Baud-rate settings for an 8 MHz SMCLK, oversampled, from TI SLAU367 Table
 * 30-5.  TIKU_BOARD_UART_BAUD is 9600 unless the build sets it (make
 * UART_BAUD=...); 19200, 38400, 57600 and 115200 are the other rates here.
 */

/** UART clock source: SMCLK. */
#define TIKU_BOARD_UART_CLK_SEL     UCSSEL__SMCLK

#ifndef TIKU_BOARD_UART_BAUD
/** Console baud rate; any value without a row below fails the build. */
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
/* BUTTON S1 - P1.1 (ACTIVE LOW)                                             */
/*---------------------------------------------------------------------------*/

/** Make P1.1 an input with its pull-up enabled. */
#define TIKU_BOARD_BTN1_INIT()      do { P1DIR &= ~BIT1; P1REN |= BIT1; P1OUT |= BIT1; } while(0)
/** Non-zero while S1 is pressed (P1.1 reads low). */
#define TIKU_BOARD_BTN1_PRESSED()   (!(P1IN & BIT1))

/*---------------------------------------------------------------------------*/
/* BUTTON S2 - P1.2 (ACTIVE LOW)                                             */
/*---------------------------------------------------------------------------*/

/** Make P1.2 an input with its pull-up enabled. */
#define TIKU_BOARD_BTN2_INIT()      do { P1DIR &= ~BIT2; P1REN |= BIT2; P1OUT |= BIT2; } while(0)
/** Non-zero while S2 is pressed (P1.2 reads low). */
#define TIKU_BOARD_BTN2_PRESSED()   (!(P1IN & BIT2))

/*---------------------------------------------------------------------------*/
/* CPU-CLOCK-OUT PIN AVAILABILITY                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief 1: P3.4 is taken, so the cpuclock test does not route SMCLK to it.
 *
 * The cpuclock test (test_cpuclock_basic.c) otherwise sets both PxSEL bits of
 * P3.4; on this board P3.4 is the eUSCI_A1 UART TX line.
 */
#define TIKU_BOARD_CPUCLOCK_OUT_PIN_BUSY    1

/*---------------------------------------------------------------------------*/
/* I2C ON eUSCI_B0 - P1.6 = SDA, P1.7 = SCL                                  */
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
 * @brief 1: ADC12_B is usable on this board.
 *
 * Channel 30 is the temperature sensor and 31 the battery monitor.  This
 * header does not record which external channels reach the BoosterPack
 * headers; the LaunchPad pinout in SLAU627 does.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1

/*---------------------------------------------------------------------------*/
/* ON-BOARD SEGMENT LCD (FH-1138P, 4-MUX, 96 SEGMENTS)                       */
/*---------------------------------------------------------------------------*/

/*
 * The LaunchPad carries an FH-1138P glass: six 14-segment alphanumeric
 * positions plus icon segments, driven by LCD_C in 4-mux mode at 1/3 bias
 * with VLCD from the on-chip charge pump.  Pin and segment routing follow TI
 * SLAU627 section 4.10 and TI's lcd_c_lib example.  COM0-COM3 drive the
 * commons; TIKU_BOARD_LCD_PIN_MASK0-2 below list the segment pins.
 *
 * TIKU_BOARD_HAS_LCD is not defined in this header: the Makefile passes
 * -DTIKU_BOARD_HAS_LCD=1 for this board (BOARD_CAPS_fr6989_launchpad := LCD),
 * so every translation unit sees it whatever its include order.
 */

/** Character positions on the glass, numbered from 0 at the left. */
#define TIKU_BOARD_LCD_NUM_CHARS    6

/*
 * Lxx pins wired to the glass, from Energia's LCD_Launchpad init() for the
 * FR6989:
 *
 *   LCDCPCTL0 (L0..L15)  = 0xFFD0 — L4,L6,L7,L8..L15
 *   LCDCPCTL1 (L16..L31) = 0xF83F — L16..L21, L27..L31
 *   LCDCPCTL2 (L32..L43) = 0x00F8 — L35..L39 (digit A4 lives here)
 */
/** @name LCDCPCTL0-2 values: the Lxx pins enabled as LCD pins
 * @{ */
#define TIKU_BOARD_LCD_PIN_MASK0    0xFFD0U
#define TIKU_BOARD_LCD_PIN_MASK1    0xF83FU
#define TIKU_BOARD_LCD_PIN_MASK2    0x00F8U
/** @} */

/*
 * A character spans two consecutive LCDMEM bytes: byte0 carries segments
 * A-F, G and M, byte1 carries H, J, K, N, P, Q and the icon bits named by
 * TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK.
 *
 * The indices count from 1, as LCD_MEM_BYTE() in tiku_lcd_arch.c does
 * (LCD_MEM_BYTE(1) is LCDM1).  Energia's LCD_Launchpad and TI's demos index
 * LCDMEM[] from 0 (LCDMEM[0] is LCDM1), so each value here is theirs plus
 * one: Energia's A1 = 9 is LCDM10/LCDM11 here.
 */
/** @name LCDMEM byte pair of each character position, left to right
 * @{ */
#define TIKU_BOARD_LCD_POS0_BYTE0   10      /**< A1 (leftmost) - LCDM10 */
#define TIKU_BOARD_LCD_POS0_BYTE1   11
#define TIKU_BOARD_LCD_POS1_BYTE0   6       /**< A2 - LCDM6 */
#define TIKU_BOARD_LCD_POS1_BYTE1   7
#define TIKU_BOARD_LCD_POS2_BYTE0   4       /**< A3 - LCDM4 */
#define TIKU_BOARD_LCD_POS2_BYTE1   5
#define TIKU_BOARD_LCD_POS3_BYTE0   19      /**< A4 - LCDM19 */
#define TIKU_BOARD_LCD_POS3_BYTE1   20
#define TIKU_BOARD_LCD_POS4_BYTE0   15      /**< A5 - LCDM15 */
#define TIKU_BOARD_LCD_POS4_BYTE1   16
#define TIKU_BOARD_LCD_POS5_BYTE0   8       /**< A6 (rightmost) - LCDM8 */
#define TIKU_BOARD_LCD_POS5_BYTE1   9
/** @} */

/**
 * @brief Byte1 bits that belong to icons rather than to the character.
 *
 * Bit 0 is a dot between digits (DOT1..DOT5, RX), bit 2 a second marker
 * (MINUS1, COLON2, RADIO, COLON4, DEG5, TX).  tiku_lcd_arch_putchar() keeps
 * these bits when it writes a character, so a lit icon stays lit.
 */
#define TIKU_BOARD_LCD_DIGIT_BYTE1_PRESERVE_MASK   0x05U

/*---------------------------------------------------------------------------*/
/* ON-BOARD LCD - ICON SEGMENTS                                              */
/*---------------------------------------------------------------------------*/

/** 1: the glass has icon segments, so the icon API is built. */
#define TIKU_BOARD_LCD_HAS_ICONS    1
/** Number of icons: the rows of TIKU_BOARD_LCD_ICON_TABLE. */
#define TIKU_BOARD_LCD_NUM_ICONS    24

/**
 * @name Icon IDs
 * TIKU_LCD_ICON_x is the row of icon x in TIKU_BOARD_LCD_ICON_TABLE; the IDs
 * are dense, so a new icon takes the next ID and a new row at the end.
 * @{
 */
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
/** @} */

/**
 * @brief Initializer rows { LCDMEM byte, bit mask }, one per icon ID.
 *
 * Values from Energia's LCD_Launchpad, with byte indices counted from 1 as
 * LCD_MEM_BYTE() counts them (Energia's value plus one).
 */
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

/** Number of inter-digit dots, numbered 1..5 from the left. */
#define TIKU_BOARD_LCD_DOT_COUNT    5

/**
 * @brief Icon ID of inter-digit dot @p n, or 0xFF when @p n is not 1..5.
 *
 * Dot n sits right of character position n - 1.  tiku_lcd_put_fixed() lights
 * one as the decimal point.
 */
#define TIKU_BOARD_LCD_DOT_ICON(n)                                             \
    ((n) == 1 ? TIKU_LCD_ICON_DOT1 :                                           \
     (n) == 2 ? TIKU_LCD_ICON_DOT2 :                                           \
     (n) == 3 ? TIKU_LCD_ICON_DOT3 :                                           \
     (n) == 4 ? TIKU_LCD_ICON_DOT4 :                                           \
     (n) == 5 ? TIKU_LCD_ICON_DOT5 : 0xFFU)

#endif /* TIKU_BOARD_FR6989_LAUNCHPAD_H_ */
