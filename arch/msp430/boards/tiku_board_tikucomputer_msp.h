/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_tikucomputer_msp.h - TikuComputer-nRF board, MSP430 side.
 *
 * Pins of the MSP430FR5994 on the TikuComputer-nRF board: one LED, one
 * button, the console on header J11, the sensor-port buses behind the three
 * bus switches, and the lines that power, reset and wake the nRF54L module.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_TIKUCOMPUTER_MSP_H_
#define TIKU_BOARD_TIKUCOMPUTER_MSP_H_

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** Human-readable board name. */
#define TIKU_BOARD_NAME             "TikuComputer-nRF (MSP430)"

/*---------------------------------------------------------------------------*/
/* LED - P1.0                                                                */
/*---------------------------------------------------------------------------*/

/** Number of on-board LEDs; interfaces/led indexes them from 0. */
#define TIKU_BOARD_LED_COUNT        1

/**
 * @name LED on P1.0, lit when the pin is high
 * INIT makes the pin an output and turns the LED off.
 * @{
 */
#define TIKU_BOARD_LED1_INIT()      do { P1DIR |= BIT0; P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED1_ON()        do { P1OUT |= BIT0; } while(0)
#define TIKU_BOARD_LED1_OFF()       do { P1OUT &= ~BIT0; } while(0)
#define TIKU_BOARD_LED1_TOGGLE()    do { P1OUT ^= BIT0; } while(0)
/** @} */

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART, picked by TIKU_BOARD_CONSOLE_SEL: 0 (default) is
 *        eUSCI_A0 on P2.0/P2.1, header J11; 1 is eUSCI_A1 on P2.5/P2.6, the
 *        UART to the nRF54L, which an nRF image relays (uart relay).
 * @note With 1, the nRF's supply goes on at boot, the link UART is not
 *       available, and the baud defaults to the link's.  Both are the pins'
 *       secondary function (PxSEL1 = 1, PxSEL0 = 0).  With 0, RX on J11 is
 *       pulled up, so a header with nothing plugged in reads idle.
 */
#ifndef TIKU_BOARD_CONSOLE_SEL
#define TIKU_BOARD_CONSOLE_SEL      0
#endif

/**
 * @brief Starting baud of the UART to the nRF54L, as the link UART and as
 *        the console on it; the nRF side's must equal it (make LINK_BAUD=...).
 * @note A DMA channel receives (tiku_uart_dma_arch.h), so no rate here
 *       loses a byte to a late interrupt.
 */
#ifndef TIKU_BOARD_LINK_BAUD
#define TIKU_BOARD_LINK_BAUD        9600
#endif

#if TIKU_BOARD_CONSOLE_SEL == 0
#define TIKU_BOARD_UART_PINS_INIT()                                            \
    do {                                                                       \
        P2DIR |= BIT0;                                                         \
        P2DIR &= (uint8_t)~BIT1;                                               \
        P2REN &= (uint8_t)~BIT0;                                               \
        P2OUT &= (uint8_t)~BIT0;                                               \
        P2OUT |= BIT1;                                                         \
        P2REN |= BIT1;                                                         \
        P2SEL1 |= BIT0 | BIT1;                                                 \
        P2SEL0 &= (uint8_t)~(BIT0 | BIT1);                                     \
    } while(0)
#elif TIKU_BOARD_CONSOLE_SEL == 1
#define TIKU_BOARD_UART_MODULE      1
#define TIKU_BOARD_UART_PINS_INIT()                                            \
    do {                                                                       \
        P2DIR |= BIT5;                                                         \
        P2DIR &= (uint8_t)~BIT6;                                               \
        P2REN &= (uint8_t)~(BIT5 | BIT6);                                      \
        P2OUT &= (uint8_t)~BIT5;                                               \
        P2SEL1 |= BIT5 | BIT6;                                                 \
        P2SEL0 &= (uint8_t)~(BIT5 | BIT6);                                     \
    } while(0)
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        TIKU_BOARD_LINK_BAUD
#endif
#else
#error "Unsupported TIKU_BOARD_CONSOLE_SEL (expected 0 or 1)"
#endif

/*
 * Baud-rate settings for an 8 MHz SMCLK, oversampled, from TI SLAU367 Table
 * 30-5.  TIKU_BOARD_UART_BAUD is 9600 unless the build sets it (make
 * UART_BAUD=...) or the console is on the nRF link (the link's baud); 19200,
 * 38400, 57600 and 115200 are the other rates here.
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
/* LINK UART TO THE nRF - TXD P2.5, RXD P2.6 (eUSCI_A1)                      */
/*---------------------------------------------------------------------------*/

/**
 * @name The UART to the nRF54L (its UARTE30), TIKU_BOARD_LINK_BAUD 8N1
 * arch/msp430/tiku_uart_link_arch.c drives it while the console is on J11.
 * Opening it is refused while the nRF is unpowered, since the idle-high TX
 * line would feed the unpowered chip; closing it parks TX low and RX pulled
 * down.
 * @{
 */
#if TIKU_BOARD_CONSOLE_SEL == 0
#define TIKU_BOARD_LINK_UART_MODULE 1
#endif
/**
 * @brief The link UART's speeds as X(baud, UCAxBRW, UCAxMCTLW) at the 8 MHz
 *        SMCLK, the console's rows (TI SLAU367 Table 30-5); it starts at
 *        TIKU_BOARD_LINK_BAUD, and tiku_uart_link_set_baud() picks another.
 */
#define TIKU_BOARD_LINK_UART_RATES(X)                                          \
    X(9600UL,    52u, ((0x49u << 8) | UCOS16 | (0x01u << 4)))                   \
    X(19200UL,   26u, ((0xB6u << 8) | UCOS16 | (0x00u << 4)))                   \
    X(38400UL,   13u, ((0x84u << 8) | UCOS16 | (0x00u << 4)))                   \
    X(115200UL,   4u, ((0x55u << 8) | UCOS16 | (0x05u << 4)))                   \
    X(230400UL,   2u, ((0xBBu << 8) | UCOS16 | (0x02u << 4)))                   \
    X(250000UL,   2u, (UCOS16))                                                 \
    X(460800UL,  17u, (0x4Au << 8))                                             \
    X(500000UL,   1u, (UCOS16))                                                 \
    X(921600UL,   8u, (0xD6u << 8))                                             \
    X(1000000UL,  8u, (0u))
#define TIKU_BOARD_LINK_UART_ALLOWED()  ((P3DIR & P3OUT & BIT4) != 0u)
#define TIKU_BOARD_LINK_UART_PINS_INIT()                                       \
    do {                                                                       \
        P2REN &= (uint8_t)~BIT6;                                               \
        P2SEL1 |= BIT5 | BIT6;                                                 \
        P2SEL0 &= (uint8_t)~(BIT5 | BIT6);                                     \
    } while(0)
#define TIKU_BOARD_LINK_UART_PINS_PARK()                                       \
    do {                                                                       \
        P2OUT &= (uint8_t)~(BIT5 | BIT6);                                      \
        P2SEL1 &= (uint8_t)~(BIT5 | BIT6);                                     \
        P2SEL0 &= (uint8_t)~(BIT5 | BIT6);                                     \
        P2DIR |= BIT5;                                                         \
        P2DIR &= (uint8_t)~BIT6;                                               \
        P2REN |= BIT6;                                                         \
    } while(0)
/** @} */

/*---------------------------------------------------------------------------*/
/* BUTTON - P5.6 (ACTIVE LOW)                                                */
/*---------------------------------------------------------------------------*/

/** Make P5.6 an input with its pull-up enabled; the button pulls it to GND. */
#define TIKU_BOARD_BTN1_INIT()      do { P5DIR &= ~BIT6; P5REN |= BIT6; P5OUT |= BIT6; } while(0)
/** Non-zero while the button is pressed (P5.6 reads low). */
#define TIKU_BOARD_BTN1_PRESSED()   (!(P5IN & BIT6))

/*---------------------------------------------------------------------------*/
/* SAFE STATE AT BOOT                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Board lines that differ from the boot default of output-low.
 *
 * Every pin is driven low first: rails and nRF supply off, link outputs low,
 * bus to the MSP430.  Then P2.6 (pulled down) and P3.6 become inputs, the
 * nRF reset P3.5 is released, and both chip selects go high.
 * @note P3.5 is only ever driven low or left an input.  With
 *       TIKU_BOARD_CONSOLE_SEL 1 the nRF's supply also goes on.
 */
#define TIKU_BOARD_PINS_SAFE_INIT()                                            \
    do {                                                                       \
        P2DIR &= (uint8_t)~BIT6;                                               \
        P2OUT &= (uint8_t)~BIT6;                                               \
        P2REN |= BIT6;                                                         \
        P3DIR &= (uint8_t)~(BIT5 | BIT6);                                      \
        P4OUT |= BIT0;                                                         \
        P5OUT |= BIT3;                                                         \
        TIKU_BOARD_CONSOLE_NRF_ON();                                           \
    } while(0)

/** @brief With the console relayed by the nRF (TIKU_BOARD_CONSOLE_SEL 1),
 *         its supply goes on at boot, before the console UART drives TX. */
#if TIKU_BOARD_CONSOLE_SEL == 1
#define TIKU_BOARD_CONSOLE_NRF_ON() do { P3OUT |= BIT4; } while(0)
#else
#define TIKU_BOARD_CONSOLE_NRF_ON() do { } while(0)
#endif

/*---------------------------------------------------------------------------*/
/* I2C ON eUSCI_B2 - P7.0 = SDA, P7.1 = SCL (SENSOR PORT, THROUGH U7)        */
/*---------------------------------------------------------------------------*/

/**
 * @brief eUSCI_B module that arch/msp430/tiku_i2c_arch.c drives: 2.
 *
 * The 4.7 kohm pull-ups sit on the sensor side of the bus switch, powered by
 * the sensor rail, so the bus idles high only with the rail on and the
 * switches set to the MSP430.
 */
#define TIKU_BOARD_I2C_MODULE       2

/**
 * @brief Hand P7.0 and P7.1 to eUSCI_B2 I2C, their primary function.
 *
 * PxSEL0 = 1, PxSEL1 = 0 (FR5994 datasheet, port P7 functions).
 */
#define TIKU_BOARD_I2C_PINS_INIT() \
    do { P7SEL0 |= BIT0 | BIT1; P7SEL1 &= ~(BIT0 | BIT1); } while(0)

/** I2C clock prescaler for 100 kHz from 8 MHz SMCLK: 8000000/100000 = 80. */
#define TIKU_BOARD_I2C_BRW_100K     80

/** I2C clock prescaler for 400 kHz from 8 MHz SMCLK: 8000000/400000 = 20. */
#define TIKU_BOARD_I2C_BRW_400K     20

/*---------------------------------------------------------------------------*/
/* SPI ON eUSCI_B1 - P5.0 = SIMO, P5.1 = SOMI, P5.2 = CLK (THROUGH U8)       */
/*---------------------------------------------------------------------------*/

/** @brief eUSCI module that arch/msp430/tiku_spi_arch.c drives: 1 = eUSCI_B1. */
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
 * The sensor port's AIN line is channel A12 (P3.0), through bus switch U7.
 * Channel 30 is the temperature sensor, 31 the battery monitor.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1

/*---------------------------------------------------------------------------*/
/* BOARD CONTROLS                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @name Words the board command uses for the two processors
 * tiku_board_tikucomputer_msp.c implements interfaces/board/tiku_board_ctl.h
 * on this board's control lines.
 * @{
 */
#define TIKU_BOARD_CTL_HOST_NAME    "msp"
#define TIKU_BOARD_CTL_COPROC_NAME  "nrf"
/** @} */

/*---------------------------------------------------------------------------*/
/* PEER                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @name The nRF54L as this side's peer (TIKU_PEER_ENABLE=1)
 * Its services ride the link UART, or the console line when the console is
 * on that UART (TIKU_BOARD_CONSOLE_SEL 1).
 * @{
 */
#define TIKU_BOARD_PEER_NAME        "nrf"
#if TIKU_BOARD_CONSOLE_SEL == 1
#define TIKU_BOARD_PEER_ON_CONSOLE  1
#endif
/** @} */

/*---------------------------------------------------------------------------*/
/* FRAM PARTS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @name The two FRAM parts behind the bus switches
 * The FM25V10 (U12, 128 KB) on SPI with its chip select on P4.0, and the
 * FM24V02A (U13, 32 KB) on I2C at 0x50.  Both are powered by the sensor rail
 * and reach this side only while the bus is switched to the MSP430.
 * @{
 */
#define TIKU_BOARD_FRAM_SPI_SIZE        131072UL
#define TIKU_BOARD_FRAM_SPI_ADDR_BYTES  3u
#define TIKU_BOARD_FRAM_SPI_CS_PORT     4u
#define TIKU_BOARD_FRAM_SPI_CS_PIN      0u
#define TIKU_BOARD_FRAM_I2C_SIZE        32768UL
#define TIKU_BOARD_FRAM_I2C_ADDR        0x50u
/** @} */

/*---------------------------------------------------------------------------*/
/* BIT-BANG PIN                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @name Default pin for tiku_bitbang transmitters: P1.4 on header J8
 * A -D on the command line overrides either macro.
 * @{
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       1
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        4
#endif
/** @} */

#endif /* TIKU_BOARD_TIKUCOMPUTER_MSP_H_ */
