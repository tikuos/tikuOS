/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr5994_launchpad.h - MSP430FR5994 LaunchPad board definitions
 *
 * This header defines the PCB-level GPIO pin assignments for the
 * MSP430FR5994 LaunchPad development board: LEDs, buttons, and
 * other board-specific peripherals.
 *
 * Board layout:
 *   - LED1 (Red)   -> P1.0
 *   - LED2 (Green) -> P1.1
 *   - Button S1    -> P5.6 (Active low)
 *   - Button S2    -> P5.5 (Active low)
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

#ifndef TIKU_BOARD_FR5994_LAUNCHPAD_H_
#define TIKU_BOARD_FR5994_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "MSP430FR5994 LaunchPad"

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
/* Backchannel UART - TXD P2.0, RXD P2.1                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_UART_PINS_INIT() do { P2SEL1 |= BIT0 | BIT1; } while(0)

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

#endif /* TIKU_BOARD_FR5994_LAUNCHPAD_H_ */
