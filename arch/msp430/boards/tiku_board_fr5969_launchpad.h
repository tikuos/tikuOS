/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr5969_launchpad.h - MSP430FR5969 LaunchPad board definitions.
 *
 * PCB-level GPIO assignments for the MSP-EXP430FR5969: LEDs, buttons and other
 * board peripherals, per the TI schematic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_FR5969_LAUNCHPAD_H_
#define TIKU_BOARD_FR5969_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "MSP430FR5969 LaunchPad"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_LED_COUNT        2

/*---------------------------------------------------------------------------*/
/* LED1 (Red) - P4.6                                                         */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_LED1_INIT()      do { P4DIR |= BIT6; P4OUT &= ~BIT6; } while(0)
#define TIKU_BOARD_LED1_ON()        do { P4OUT |= BIT6; } while(0)
#define TIKU_BOARD_LED1_OFF()       do { P4OUT &= ~BIT6; } while(0)
#define TIKU_BOARD_LED1_TOGGLE()    do { P4OUT ^= BIT6; } while(0)

/*---------------------------------------------------------------------------*/
/* LED2 (Green) - P1.0                                                       */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_LED2_INIT()      do { P1DIR |= BIT0; P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED2_ON()        do { P1OUT |= BIT0; } while(0)
#define TIKU_BOARD_LED2_OFF()       do { P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED2_TOGGLE()    do { P1OUT ^= BIT0; } while(0)

/*---------------------------------------------------------------------------*/
/* Backchannel UART - TXD P2.0, RXD P2.1                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_UART_PINS_INIT()                                            \
    do {                                                                       \
        /* Backchannel UART on eUSCI_A0: P2.0 = TXD, P2.1 = RXD. */           \
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
/* Button S1 - P4.5 (Active low)                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN1_INIT()      do { P4DIR &= ~BIT5; P4REN |= BIT5; P4OUT |= BIT5; } while(0)
#define TIKU_BOARD_BTN1_PRESSED()   (!(P4IN & BIT5))

/*---------------------------------------------------------------------------*/
/* Button S2 - P1.1 (Active low)                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN2_INIT()      do { P1DIR &= ~BIT1; P1REN |= BIT1; P1OUT |= BIT1; } while(0)
#define TIKU_BOARD_BTN2_PRESSED()   (!(P1IN & BIT1))

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
/* SPI on eUSCI_A1: P2.5 = CLK, P2.6 = SIMO, P2.7 = SOMI                   */
/*---------------------------------------------------------------------------*/

/** Configure P2.5/P2.6/P2.7 for eUSCI_A1 SPI function (SEL1=1, SEL0=0). */
#define TIKU_BOARD_SPI_PINS_INIT() \
    do { P2SEL1 |= BIT5 | BIT6 | BIT7; \
         P2SEL0 &= ~(BIT5 | BIT6 | BIT7); } while(0)

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
 * BoosterPack headers; A0 and A1 clash with LED2 and button S2.  Channel 30 is
 * the internal temperature sensor and 31 the battery monitor.
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

#endif /* TIKU_BOARD_FR5969_LAUNCHPAD_H_ */
