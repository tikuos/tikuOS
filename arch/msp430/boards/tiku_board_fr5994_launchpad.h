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
 * backchannel, the BoosterPack I2C/SPI/ADC/1-Wire mappings and the bit-bang
 * pin, per the TI schematic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_FR5994_LAUNCHPAD_H_
#define TIKU_BOARD_FR5994_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** Human-readable board name. */
#define TIKU_BOARD_NAME             "MSP430FR5994 LaunchPad"

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
/* LED2 (GREEN) - P1.1                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @name LED2 on P1.1, lit when the pin is high
 * INIT makes the pin an output and turns the LED off.
 * @{
 */
#define TIKU_BOARD_LED2_INIT()      do { P1DIR |= BIT1; P1OUT &= ~BIT1; } while(0)
#define TIKU_BOARD_LED2_ON()        do { P1OUT |= BIT1; } while(0)
#define TIKU_BOARD_LED2_OFF()       do { P1OUT &= ~BIT1; } while(0)
#define TIKU_BOARD_LED2_TOGGLE()    do { P1OUT ^= BIT1; } while(0)
/** @} */

/*---------------------------------------------------------------------------*/
/* BACKCHANNEL UART - TXD P2.0, RXD P2.1 (eUSCI_A0)                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Hand P2.0 (UCA0TXD) and P2.1 (UCA0RXD) to eUSCI_A0.
 *
 * eUSCI_A0 is the secondary function on these pins (PxSEL1 = 1, PxSEL0 = 0).
 * The PxSEL encoding of a function differs from pin to pin on this part.
 */
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
/* BUTTON S1 - P5.6 (ACTIVE LOW)                                             */
/*---------------------------------------------------------------------------*/

/** Make P5.6 an input with its pull-up enabled. */
#define TIKU_BOARD_BTN1_INIT()      do { P5DIR &= ~BIT6; P5REN |= BIT6; P5OUT |= BIT6; } while(0)
/** Non-zero while S1 is pressed (P5.6 reads low). */
#define TIKU_BOARD_BTN1_PRESSED()   (!(P5IN & BIT6))

/*---------------------------------------------------------------------------*/
/* BUTTON S2 - P5.5 (ACTIVE LOW)                                             */
/*---------------------------------------------------------------------------*/

/** Make P5.5 an input with its pull-up enabled. */
#define TIKU_BOARD_BTN2_INIT()      do { P5DIR &= ~BIT5; P5REN |= BIT5; P5OUT |= BIT5; } while(0)
/** Non-zero while S2 is pressed (P5.5 reads low). */
#define TIKU_BOARD_BTN2_PRESSED()   (!(P5IN & BIT5))

/*---------------------------------------------------------------------------*/
/* I2C ON eUSCI_B0 - P1.6 = SDA, P1.7 = SCL                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Hand P1.6 and P1.7 to eUSCI_B0 I2C.
 *
 * I2C is the secondary function on these pins (PxSEL1 = 1, PxSEL0 = 0); the
 * primary function is a timer, which takes SDA and SCL off the bus.
 */
#define TIKU_BOARD_I2C_PINS_INIT() \
    do { P1SEL1 |= BIT6 | BIT7; P1SEL0 &= ~(BIT6 | BIT7); } while(0)

/** I2C clock prescaler for 100 kHz from 8 MHz SMCLK: 8000000/100000 = 80. */
#define TIKU_BOARD_I2C_BRW_100K     80

/** I2C clock prescaler for 400 kHz from 8 MHz SMCLK: 8000000/400000 = 20. */
#define TIKU_BOARD_I2C_BRW_400K     20

/*---------------------------------------------------------------------------*/
/* SPI ON eUSCI_B1 - P5.0 = SIMO, P5.1 = SOMI, P5.2 = CLK                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief eUSCI module that arch/msp430/tiku_spi_arch.c drives: 1 = eUSCI_B1.
 *
 * The BoosterPack SPI pins on this LaunchPad are P5.0-P5.2 (eUSCI_B1, header
 * J2).  The eUSCI_A1 pins P2.5-P2.7 that module 0 uses are not brought out.
 */
#define TIKU_BOARD_SPI_MODULE       1

/**
 * @brief Hand P5.0-P5.2 to eUSCI_B1 SPI, their primary function.
 *
 * PxSEL0 = 1, PxSEL1 = 0.  The secondary function is Timer_B0 capture/output:
 * with it selected, SPI writes reach TB0 and the bus never clocks.
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

/**
 * @brief 1: ADC12_B is usable on this board.
 *
 * External channels A2-A5 and A8-A15 reach the BoosterPack headers; A0 and A1
 * are the LED pins.  Channel 30 is the temperature sensor, 31 the battery
 * monitor.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1

/*---------------------------------------------------------------------------*/
/* 1-WIRE ON P1.2 (BOOSTERPACK J1 PIN 4)                                     */
/*---------------------------------------------------------------------------*/

/**
 * @name 1-Wire bus, bit-banged on P1.2
 * The bus needs an external 4.7 kohm pull-up to 3V3.  P1.2 is also ADC
 * channel A2, so a design cannot use both on this pin.
 * @{
 */
#define TIKU_BOARD_OW_AVAILABLE     1
#define TIKU_BOARD_OW_PORT          1
#define TIKU_BOARD_OW_PIN           2
#define TIKU_BOARD_OW_DIR           P1DIR
#define TIKU_BOARD_OW_OUT           P1OUT
#define TIKU_BOARD_OW_IN            P1IN
#define TIKU_BOARD_OW_SEL0          P1SEL0
#define TIKU_BOARD_OW_SEL1          P1SEL1
#define TIKU_BOARD_OW_BIT           BIT2
/** @} */

/*---------------------------------------------------------------------------*/
/* BIT-BANG PIN                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @name Default pin for tiku_bitbang transmitters: P1.4
 * P1.4 is on a BoosterPack header beside a ground pin, and no other macro in
 * this header uses it.  A -D on the command line overrides either macro.
 * @{
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       1
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        4
#endif
/** @} */

#endif /* TIKU_BOARD_FR5994_LAUNCHPAD_H_ */
