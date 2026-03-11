/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr5969_launchpad.h - MSP430FR5969 LaunchPad board definitions
 *
 * This header defines the PCB-level GPIO pin assignments for the
 * MSP430FR5969 LaunchPad development board: LEDs, buttons, and
 * other board-specific peripherals.
 *
 * Board layout (per TI MSP-EXP430FR5969 schematic):
 *   - LED1 (Red)   -> P4.6
 *   - LED2 (Green) -> P1.0
 *   - Button S1    -> P4.5 (Active low)
 *   - Button S2    -> P1.1 (Active low)
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

#ifndef TIKU_BOARD_FR5969_LAUNCHPAD_H_
#define TIKU_BOARD_FR5969_LAUNCHPAD_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "MSP430FR5969 LaunchPad"

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

#define TIKU_BOARD_UART_PINS_INIT() do { P2SEL1 |= BIT0 | BIT1; P2SEL0 &= ~(BIT0 | BIT1); } while(0)

/** UART baud-rate config: 9600 baud from 8 MHz SMCLK (oversampling). */
#define TIKU_BOARD_UART_CLK_SEL     UCSSEL__SMCLK
#define TIKU_BOARD_UART_BRW         52
#define TIKU_BOARD_UART_MCTLW       ((0x49 << 8) | UCOS16 | (0x01 << 4))

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

#endif /* TIKU_BOARD_FR5969_LAUNCHPAD_H_ */
