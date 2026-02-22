/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr2433_launchpad.h - MSP430FR2433 LaunchPad board definitions
 *
 * This header defines the PCB-level GPIO pin assignments for the
 * MSP-EXP430FR2433 LaunchPad development board: LEDs, buttons, and
 * other board-specific peripherals.
 *
 * Board layout (per TI MSP-EXP430FR2433 schematic):
 *   - LED1 (Green) -> P1.0
 *   - LED2 (Green) -> P1.1
 *   - Button S1    -> P2.3 (Active low)
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

#ifndef TIKU_BOARD_FR2433_LAUNCHPAD_H_
#define TIKU_BOARD_FR2433_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "MSP430FR2433 LaunchPad"

/*---------------------------------------------------------------------------*/
/* LED1 (Green) - P1.0                                                       */
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
/* Backchannel UART - TXD P1.4, RXD P1.5                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_UART_PINS_INIT() do { P1SEL0 |= BIT4 | BIT5; P1SEL1 &= ~(BIT4 | BIT5); } while(0)

/** UART baud-rate config: 9600 baud from 5 MHz MODCLK (MODOSC, ±0.5%).
 *  N = 5000000/9600 = 520.83 → oversampling: BRW=32, BRF=9, BRS=0x00. */
#define TIKU_BOARD_UART_CLK_SEL     UCSSEL__MODCLK
#define TIKU_BOARD_UART_BRW         32
#define TIKU_BOARD_UART_MCTLW       (UCOS16 | (0x09 << 4))

/*---------------------------------------------------------------------------*/
/* Button S1 - P2.3 (Active low)                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN1_INIT()      do { P2DIR &= ~BIT3; P2REN |= BIT3; P2OUT |= BIT3; } while(0)
#define TIKU_BOARD_BTN1_PRESSED()   (!(P2IN & BIT3))

/*---------------------------------------------------------------------------*/
/* Button S2 - Not available on MSP-EXP430FR2433                             */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN2_INIT()      do { } while(0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

#endif /* TIKU_BOARD_FR2433_LAUNCHPAD_H_ */
