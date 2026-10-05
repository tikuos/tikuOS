/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr2433_launchpad.h - MSP430FR2433 LaunchPad board definitions.
 *
 * PCB-level GPIO assignments for the MSP-EXP430FR2433: LEDs, button and the
 * backchannel UART, per the TI schematic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_FR2433_LAUNCHPAD_H_
#define TIKU_BOARD_FR2433_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** Human-readable board name. */
#define TIKU_BOARD_NAME             "MSP430FR2433 LaunchPad"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

/** Number of on-board LEDs; interfaces/led indexes them from 0. */
#define TIKU_BOARD_LED_COUNT        2

/*---------------------------------------------------------------------------*/
/* LED1 (GREEN) - P1.0                                                       */
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
/* BACKCHANNEL UART - TXD P1.4, RXD P1.5                                     */
/*---------------------------------------------------------------------------*/

/** Route P1.4 (TXD) and P1.5 (RXD) to eUSCI_A0 (PxSEL0 = 1, PxSEL1 = 0). */
#define TIKU_BOARD_UART_PINS_INIT() do { P1SEL0 |= BIT4 | BIT5; P1SEL1 &= ~(BIT4 | BIT5); } while(0)

/**
 * @name UART clock: 9600 baud from the 5 MHz MODCLK (MODOSC, +/-0.5%)
 * N = 5000000 / 9600 = 520.83, oversampled: BRW = 32, BRF = 9, BRS = 0x00.
 * @{
 */
#define TIKU_BOARD_UART_CLK_SEL     UCSSEL__MODCLK
#define TIKU_BOARD_UART_BRW         32
#define TIKU_BOARD_UART_MCTLW       (UCOS16 | (0x09 << 4))
/** @} */

/*---------------------------------------------------------------------------*/
/* BUTTON S1 - P2.3 (ACTIVE LOW)                                             */
/*---------------------------------------------------------------------------*/

/** Make P2.3 an input with its pull-up enabled. */
#define TIKU_BOARD_BTN1_INIT()      do { P2DIR &= ~BIT3; P2REN |= BIT3; P2OUT |= BIT3; } while(0)
/** Non-zero while S1 is pressed (P2.3 reads low). */
#define TIKU_BOARD_BTN1_PRESSED()   (!(P2IN & BIT3))

/*---------------------------------------------------------------------------*/
/* BUTTON S2 - NOT MAPPED                                                    */
/*---------------------------------------------------------------------------*/

/** This header maps no second button: INIT does nothing. */
#define TIKU_BOARD_BTN2_INIT()      do { } while(0)
/** Always 0: this header maps no second button. */
#define TIKU_BOARD_BTN2_PRESSED()   (0)

#endif /* TIKU_BOARD_FR2433_LAUNCHPAD_H_ */
