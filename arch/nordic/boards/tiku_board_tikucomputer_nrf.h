/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_tikucomputer_nrf.h - TikuComputer-nRF board, nRF54L side.
 *
 * Pins of the Minew ME54BS01 module (nRF54L10 or nRF54L15) on the
 * TikuComputer-nRF board: one LED, no buttons, the log UART on header J5 and
 * the sensor-port buses on J10.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_TIKUCOMPUTER_NRF_H_
#define TIKU_BOARD_TIKUCOMPUTER_NRF_H_

#include <arch/nordic/tiku_gpio_arch.h>   /* tiku_nordic_gpio_* helpers */

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "TikuComputer-nRF"

/*---------------------------------------------------------------------------*/
/* LED                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief The green LED D1 on P1.02, active high.
 *
 * P1.02 and P1.03 are the NFC pads: the init macro makes both plain GPIO
 * (NFCT.PADCONFIG = 0), and every macro drives P1.03, which is not connected,
 * to the level of P1.02, since the pads leak between them at unequal levels.
 */
#define TIKU_BOARD_LED_COUNT        1
#define TIKU_BOARD_LED_ACTIVE_LOW   0
#define TIKU_BOARD_LED_ON_LVL       1u
#define TIKU_BOARD_LED_OFF_LVL      0u

#define TIKU_BOARD_LED1_PORT        1u
#define TIKU_BOARD_LED1_PIN         2u
#define TIKU_BOARD_LED1_INIT()      do {                                      \
        NRF_NFCT_S->PADCONFIG = 0UL;                                          \
        tiku_nordic_gpio_init_output(1u, 2u, TIKU_BOARD_LED_OFF_LVL);         \
        tiku_nordic_gpio_init_output(1u, 3u, TIKU_BOARD_LED_OFF_LVL);         \
    } while (0)
#define TIKU_BOARD_LED1_ON()        do {                                      \
        tiku_nordic_gpio_set(1u, 2u, TIKU_BOARD_LED_ON_LVL);                  \
        tiku_nordic_gpio_set(1u, 3u, TIKU_BOARD_LED_ON_LVL);                  \
    } while (0)
#define TIKU_BOARD_LED1_OFF()       do {                                      \
        tiku_nordic_gpio_set(1u, 2u, TIKU_BOARD_LED_OFF_LVL);                 \
        tiku_nordic_gpio_set(1u, 3u, TIKU_BOARD_LED_OFF_LVL);                 \
    } while (0)
#define TIKU_BOARD_LED1_TOGGLE()    do {                                      \
        tiku_nordic_gpio_toggle(1u, 2u);                                      \
        tiku_nordic_gpio_toggle(1u, 3u);                                      \
    } while (0)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The board's only nRF-side button is RESET, on nRESET. */
#define TIKU_BOARD_BTN_COUNT        0

/*---------------------------------------------------------------------------*/
/* WAKE LINE                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief P0.02, from MSP430 P3.7 through 1 kOhm: a high level wakes the part
 *        from System OFF, which tiku_nordic_system_off() arms.
 */
#define TIKU_BOARD_WAKE_PORT        0u
#define TIKU_BOARD_WAKE_PIN         2u
#define TIKU_BOARD_WAKE_HIGH        1u

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UARTE, 115200 8N1, picked by TIKU_BOARD_CONSOLE_SEL.
 *
 * 20 (default) is UARTE20 on P1.09 (TXD) and P1.10 (RXD), header J5; 30 is
 * UARTE30 on P0.00 (TXD) and P0.01 (RXD), the board's UART to its MSP430.
 */
#ifndef TIKU_BOARD_CONSOLE_SEL
#define TIKU_BOARD_CONSOLE_SEL      20
#endif

#if TIKU_BOARD_CONSOLE_SEL == 20
#define TIKU_BOARD_CONSOLE_UARTE    NRF_UARTE20_S
#define TIKU_BOARD_CONSOLE_UARTE_IRQN 198  /* SERIAL20_IRQn (MDK enum)        */
#define TIKU_BOARD_UART_TX_PORT     1u
#define TIKU_BOARD_UART_TX_PIN      9u
#define TIKU_BOARD_UART_RX_PORT     1u
#define TIKU_BOARD_UART_RX_PIN      10u
#elif TIKU_BOARD_CONSOLE_SEL == 30
#define TIKU_BOARD_CONSOLE_UARTE    NRF_UARTE30_S
#define TIKU_BOARD_CONSOLE_UARTE_IRQN 260  /* SERIAL30_IRQn (MDK enum)        */
#define TIKU_BOARD_UART_TX_PORT     0u
#define TIKU_BOARD_UART_TX_PIN      0u
#define TIKU_BOARD_UART_RX_PORT     0u
#define TIKU_BOARD_UART_RX_PIN      1u
#else
#error "Unsupported TIKU_BOARD_CONSOLE_SEL (expected 20 or 30)"
#endif

/** @brief No-op: the UARTE driver routes the pins through its PSEL regs. */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* SENSOR-PORT BUSES                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief I2C (TWIM22) and SPI (SPIM21) pins of the sensor port.
 *
 * The buses reach J10, the Qwiic connector and the two FRAMs through the
 * board's bus switches, which the MSP430 turns to this side (MUX_SEL high,
 * MUX_EN_N low).  Ports are physical: 1 = P1.
 */
#define TIKU_BOARD_I2C0_SCL_PORT    1u
#define TIKU_BOARD_I2C0_SCL_PIN     11u
#define TIKU_BOARD_I2C0_SDA_PORT    1u
#define TIKU_BOARD_I2C0_SDA_PIN     12u
#define TIKU_BOARD_SPI0_SCK_PORT    1u
#define TIKU_BOARD_SPI0_SCK_PIN     4u
#define TIKU_BOARD_SPI0_MOSI_PORT   1u
#define TIKU_BOARD_SPI0_MOSI_PIN    5u
#define TIKU_BOARD_SPI0_MISO_PORT   1u
#define TIKU_BOARD_SPI0_MISO_PIN    6u

/*---------------------------------------------------------------------------*/
/* PERIPHERAL CAPABILITIES                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Interface layers compiled for this board.
 *
 * ADC (SAADC; the sensor port's analog input is P1.13, AIN6), I2C and SPI
 * have drivers.  The sensor port has no 1-Wire pin.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic: not read on this port */
#define TIKU_BOARD_OW_AVAILABLE     0

/*---------------------------------------------------------------------------*/
/* FRAM PARTS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @name The two FRAM parts behind the bus switches
 * The FM25V10 (U12, 128 KB) on SPI with its chip select on P2.07 (GPIO
 * interface port 3), and the FM24V02A (U13, 32 KB) on I2C at 0x50.  Both are
 * powered by the sensor rail and reach this side only while the MSP430 has
 * switched the bus to it.
 * @{
 */
#define TIKU_BOARD_FRAM_SPI_SIZE        131072UL
#define TIKU_BOARD_FRAM_SPI_ADDR_BYTES  3u
#define TIKU_BOARD_FRAM_SPI_CS_PORT     3u
#define TIKU_BOARD_FRAM_SPI_CS_PIN      7u
#define TIKU_BOARD_FRAM_I2C_SIZE        32768UL
#define TIKU_BOARD_FRAM_I2C_ADDR        0x50u
/** @} */

#endif /* TIKU_BOARD_TIKUCOMPUTER_NRF_H_ */
