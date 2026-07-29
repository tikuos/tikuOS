/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr5994_launchpad.h - MSP430FR5994 LaunchPad board definitions.
 *
 * PCB-level assignments for the MSP-EXP430FR5994: LEDs, buttons, the UART
 * back-channel, the BoosterPack I2C/SPI/ADC/1-Wire mappings and the bit-bang test
 * pin, per the TI schematic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_FR5994_LAUNCHPAD_H_
#define TIKU_BOARD_FR5994_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "MSP430FR5994 LaunchPad"

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
/* LED2 (Green) - P1.1                                                       */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_LED2_INIT()      do { P1DIR |= BIT1; P1OUT &= ~BIT1; } while(0)
#define TIKU_BOARD_LED2_ON()        do { P1OUT |= BIT1; } while(0)
#define TIKU_BOARD_LED2_OFF()       do { P1OUT &= ~BIT1; } while(0)
#define TIKU_BOARD_LED2_TOGGLE()    do { P1OUT ^= BIT1; } while(0)

/*---------------------------------------------------------------------------*/
/* Backchannel UART - TXD P2.0, RXD P2.1 (eUSCI_A0)                          */
/*---------------------------------------------------------------------------*/

/* Backchannel UART on eUSCI_A0: P2.0 = UCA0TXD, P2.1 = UCA0RXD.
 * On FR5994 the eUSCI_A0 function is the SECONDARY peripheral on
 * these pins (PSEL = 10: SEL1=1, SEL0=0).  PSEL = 01 selects port
 * mapping (default = none); PSEL = 11 selects UCA0CLK. The PxSEL
 * encoding here is per-pin, not uniform across the device. */
#define TIKU_BOARD_UART_PINS_INIT()                                            \
    do {                                                                       \
        P2DIR |= BIT0;                                                         \
        P2DIR &= (uint8_t)~BIT1;                                               \
        P2REN &= (uint8_t)~(BIT0 | BIT1);                                      \
        P2OUT &= (uint8_t)~BIT0;                                               \
        P2SEL1 |= BIT0 | BIT1;                                                 \
        P2SEL0 &= (uint8_t)~(BIT0 | BIT1);                                     \
    } while(0)

/*
 * UART baud-rate selection from an 8 MHz SMCLK, oversampled; values from TI
 * SLAU367 Table 30-5.  9600 by default, with 19200, 38400, 57600 and 115200
 * selectable at build time through UART_BAUD.
 */
#define TIKU_BOARD_UART_CLK_SEL     UCSSEL__SMCLK

#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        9600
#endif

#if   TIKU_BOARD_UART_BAUD == 9600
/*  N = 8000000/9600 = 833.33  → BRW=52, BRF=1, BRS=0x49 */
#define TIKU_BOARD_UART_BRW         52
#define TIKU_BOARD_UART_MCTLW       ((0x49 << 8) | UCOS16 | (0x01 << 4))

#elif TIKU_BOARD_UART_BAUD == 19200
/*  N = 8000000/19200 = 416.67 → BRW=26, BRF=0, BRS=0xB6 */
#define TIKU_BOARD_UART_BRW         26
#define TIKU_BOARD_UART_MCTLW       ((0xB6 << 8) | UCOS16 | (0x00 << 4))

#elif TIKU_BOARD_UART_BAUD == 38400
/*  N = 8000000/38400 = 208.33 → BRW=13, BRF=0, BRS=0x84 */
#define TIKU_BOARD_UART_BRW         13
#define TIKU_BOARD_UART_MCTLW       ((0x84 << 8) | UCOS16 | (0x00 << 4))

#elif TIKU_BOARD_UART_BAUD == 57600
/*  N = 8000000/57600 = 138.89 → BRW=8, BRF=10, BRS=0xF7 */
#define TIKU_BOARD_UART_BRW         8
#define TIKU_BOARD_UART_MCTLW       ((0xF7 << 8) | UCOS16 | (0x0A << 4))

#elif TIKU_BOARD_UART_BAUD == 115200
/*  N = 8000000/115200 = 69.44 → BRW=4, BRF=5, BRS=0x55 */
#define TIKU_BOARD_UART_BRW         4
#define TIKU_BOARD_UART_MCTLW       ((0x55 << 8) | UCOS16 | (0x05 << 4))

#else
#error "Unsupported TIKU_BOARD_UART_BAUD (use 9600/19200/38400/57600/115200)"
#endif

/*---------------------------------------------------------------------------*/
/* Button S1 - P5.6 (Active low)                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN1_INIT()      do { P5DIR &= ~BIT6; P5REN |= BIT6; P5OUT |= BIT6; } while(0)
#define TIKU_BOARD_BTN1_PRESSED()   (!(P5IN & BIT6))

/*---------------------------------------------------------------------------*/
/* Button S2 - P5.5 (Active low)                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN2_INIT()      do { P5DIR &= ~BIT5; P5REN |= BIT5; P5OUT |= BIT5; } while(0)
#define TIKU_BOARD_BTN2_PRESSED()   (!(P5IN & BIT5))

/*---------------------------------------------------------------------------*/
/* I2C on eUSCI_B0: P1.6 = SDA, P1.7 = SCL                                   */
/*---------------------------------------------------------------------------*/

/*
 * Configure P1.6 and P1.7 for eUSCI_B0 I2C.  The mapping is per-pin on this
 * part, and I2C is the SECONDARY function here -- the primary is a timer, so
 * choosing it would silently route SDA and SCL into TA0/TA1.
 */
#define TIKU_BOARD_I2C_PINS_INIT() \
    do { P1SEL1 |= BIT6 | BIT7; P1SEL0 &= ~(BIT6 | BIT7); } while(0)

/** I2C clock prescaler for 100 kHz from 8 MHz SMCLK: 8000000/100000 = 80. */
#define TIKU_BOARD_I2C_BRW_100K     80

/** I2C clock prescaler for 400 kHz from 8 MHz SMCLK: 8000000/400000 = 20. */
#define TIKU_BOARD_I2C_BRW_400K     20

/*---------------------------------------------------------------------------*/
/* SPI on eUSCI_B1: P5.0 = SIMO, P5.1 = SOMI, P5.2 = CLK                    */
/*---------------------------------------------------------------------------*/

/*
 * MSP-EXP430FR5994 LaunchPad routes the BoosterPack-standard SPI
 * pins to eUSCI_B1 on P5.0 / P5.1 / P5.2 (these are physically
 * exposed on the J2 BoosterPack header). The FR5969 LaunchPad uses
 * eUSCI_A1 on P2.5 / P2.6 / P2.7 instead — those pins exist in the
 * MSP430FR5994 silicon but are NOT broken out on the FR5994
 * LaunchPad PCB, so wiring an external SPI peripheral to them is
 * physically impossible on this board.
 *
 * TIKU_BOARD_SPI_MODULE selects which eUSCI module the SPI driver
 * (arch/msp430/tiku_spi_arch.c) drives. Value 1 = eUSCI_B1; the
 * default (0 = eUSCI_A1, used by FR5969) would not work here
 * because UCA1's primary pins aren't on the BoosterPack headers.
 */

#define TIKU_BOARD_SPI_MODULE       1

/*
 * Configure P5.0/P5.1/P5.2 for eUSCI_B1 SPI, which is the PRIMARY function on
 * these pins.  The secondary is Timer B0 capture/output, and selecting it
 * instead routes SPI traffic into TB0 registers so the bus never clocks.
 */
#define TIKU_BOARD_SPI_PINS_INIT() \
    do { P5SEL0 |=  (BIT0 | BIT1 | BIT2); \
         P5SEL1 &= ~(BIT0 | BIT1 | BIT2); } while(0)

/** SPI prescaler for 4 MHz from 8 MHz SMCLK: 8000000/4000000 = 2. */
#define TIKU_BOARD_SPI_BRW_4MHZ     2

/** SPI prescaler for 2 MHz from 8 MHz SMCLK: 8000000/2000000 = 4. */
#define TIKU_BOARD_SPI_BRW_2MHZ     4

/** SPI prescaler for 1 MHz from 8 MHz SMCLK: 8000000/1000000 = 8. */
#define TIKU_BOARD_SPI_BRW_1MHZ     8

/** SPI prescaler for 500 kHz from 8 MHz SMCLK: 8000000/500000 = 16. */
#define TIKU_BOARD_SPI_BRW_500KHZ   16

/*---------------------------------------------------------------------------*/
/* ADC12_B                                                                   */
/*---------------------------------------------------------------------------*/

/*
 * ADC12_B is available.  External channels A2-A5 and A8-A15 reach the
 * BoosterPack headers; A0 and A1 are unavailable because they drive the
 * on-board LEDs.  Channel 30 is temperature and 31 the battery monitor.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1

/*---------------------------------------------------------------------------*/
/* 1-Wire on P1.2 (BoosterPack J1 pin 4)                                    */
/*---------------------------------------------------------------------------*/

/*
 * 1-Wire bit-banged on P1.2, which needs an external 4.7 kohm pull-up to 3V3.
 * P1.2 is also ADC channel A2, so a design using both must move one of them.
 */
#define TIKU_BOARD_OW_AVAILABLE     1
#define TIKU_BOARD_OW_DIR           P1DIR
#define TIKU_BOARD_OW_OUT           P1OUT
#define TIKU_BOARD_OW_IN            P1IN
#define TIKU_BOARD_OW_SEL0          P1SEL0
#define TIKU_BOARD_OW_SEL1          P1SEL1
#define TIKU_BOARD_OW_BIT           BIT2

/*---------------------------------------------------------------------------*/
/* Bit-bang test pin (tiku_bitbang demos / backscatter prototyping)          */
/*---------------------------------------------------------------------------*/

/*
 * Default pin for tiku_bitbang transmitters on this board: P1.4, brought out on
 * a BoosterPack header next to a ground pin, which makes it a convenient
 * logic-analyser probe point and clashes with nothing declared above.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       1
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        4
#endif

#endif /* TIKU_BOARD_FR5994_LAUNCHPAD_H_ */
