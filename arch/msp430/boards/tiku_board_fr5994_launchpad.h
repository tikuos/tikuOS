/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_fr5994_launchpad.h - MSP430FR5994 LaunchPad board definitions
 *
 * This header defines the PCB-level GPIO pin assignments for the
 * MSP430FR5994 LaunchPad development board: LEDs, buttons, UART
 * back-channel routing, BoosterPack I2C / SPI / ADC / 1-Wire mappings,
 * and the bit-bang test pin used by tiku_bitbang demos.
 *
 * Board layout (per TI MSP-EXP430FR5994 schematic):
 *   - LED1 (Red)   -> P1.0
 *   - LED2 (Green) -> P1.1
 *   - Button S1    -> P5.6 (Active low)
 *   - Button S2    -> P5.5 (Active low)
 *   - UART back-channel: P2.0 = TXD, P2.1 = RXD (eUSCI_A0)
 *
 * Notes vs the FR5969 LaunchPad:
 *   - LEDs swapped to P1.0 / P1.1 (FR5969 has them on P4.6 / P1.0)
 *     so ADC channels A0 / A1 are NOT usable on the FR5994 board
 *     because the same pins drive the LEDs.
 *   - Buttons moved to P5.5 / P5.6.
 *   - Otherwise the BoosterPack pin standard matches: I2C on P1.6/P1.7,
 *     SPI on P2.5/P2.6/P2.7, OneWire on P1.2, bit-bang on P1.4.
 *   - 256 KB FRAM with HIFRAM at 0x10000+ — kernel + shell + every
 *     TikuKits library fits in `MEMORY_MODEL=large`.
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

/** UART baud-rate selection from 8 MHz SMCLK (oversampling).
 *  Values from TI SLAU367 Table 30-5 for eUSCI_A @ 8 MHz.
 *
 *  Override at compile time:
 *    make MCU=msp430fr5994 UART_BAUD=115200
 *
 *  Supported: 9600 (default), 19200, 38400, 57600, 115200.
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

/** Configure P1.6 and P1.7 for eUSCI_B0 I2C function (SEL1=1, SEL0=0). */
#define TIKU_BOARD_I2C_PINS_INIT() \
    do { P1SEL1 |= BIT6 | BIT7; P1SEL0 &= ~(BIT6 | BIT7); } while(0)

/** I2C clock prescaler for 100 kHz from 8 MHz SMCLK: 8000000/100000 = 80. */
#define TIKU_BOARD_I2C_BRW_100K     80

/** I2C clock prescaler for 400 kHz from 8 MHz SMCLK: 8000000/400000 = 20. */
#define TIKU_BOARD_I2C_BRW_400K     20

/*---------------------------------------------------------------------------*/
/* SPI on eUSCI_A1: P2.5 = CLK, P2.6 = SIMO, P2.7 = SOMI                    */
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

/**
 * ADC12_B is available on the FR5994 LaunchPad.
 * External channels A2-A5 (P1.2-P1.5) and A8-A15 (P4.0-P4.3, P3.0-P3.3)
 * are accessible on the BoosterPack headers.
 *
 * Note: A0 (P1.0) AND A1 (P1.1) are unavailable because they drive the
 * on-board LEDs. Internal channels: ch30 = temperature sensor,
 * ch31 = battery monitor.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1

/*---------------------------------------------------------------------------*/
/* 1-Wire on P1.2 (BoosterPack J1 pin 4)                                    */
/*---------------------------------------------------------------------------*/

/**
 * 1-Wire (Dallas/Maxim) bit-banged on P1.2.
 * Requires an external 4.7 kohm pull-up resistor from P1.2 to 3.3V.
 *
 * Note: P1.2 is also ADC channel A2. If using both 1-Wire and ADC,
 * choose a different pin for one of them.
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

/**
 * Default pin for tiku_bitbang transmitters on this board: P1.4.
 *
 * P1.4 is brought out on BoosterPack header J1.5 (with GND on J1.20)
 * which makes it a good logic-analyzer probe point. It does not
 * conflict with the LEDs (P1.0/P1.1), buttons (P5.5/P5.6), UART (P2.x),
 * I2C (P1.6/P1.7), SPI (P2.5-7), or 1-Wire (P1.2) pins declared above.
 * Override at compile time by passing
 * -DTIKU_BOARD_BSCAT_PORT=<n> -DTIKU_BOARD_BSCAT_PIN=<n>.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       1
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        4
#endif

#endif /* TIKU_BOARD_FR5994_LAUNCHPAD_H_ */
