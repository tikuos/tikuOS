/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_nrf54lm20_dk.h - Nordic nRF54LM20-DK (PCA10184) GPIO assignments.
 *
 * Pin assignments are from the DK hardware user guide.  Helpers take a
 * physical port matching the board silk.  The silk numbers LEDs and buttons
 * from 0 and TikuOS from 1, so silk "LED 0" is TikuOS LED1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_NRF54LM20_DK_H_
#define TIKU_BOARD_NRF54LM20_DK_H_

#include <arch/nordic/tiku_gpio_arch.h>   /* tiku_nordic_gpio_* helpers */

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "nRF54LM20-DK"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief On-board LED count and per-LED (port,pin).
 *
 * Board silk LED0..LED3 (User Guide Table 8) map to TikuOS 1-indexed LEDs:
 *   LED1 = P1.22   LED2 = P1.25   LED3 = P1.27   LED4 = P1.28
 * The LEDs are active-high (transistor-buffered).
 */
#define TIKU_BOARD_LED_COUNT        4
#define TIKU_BOARD_LED_ACTIVE_LOW   0

#if TIKU_BOARD_LED_ACTIVE_LOW
#define TIKU_BOARD_LED_ON_LVL       0u
#else
#define TIKU_BOARD_LED_ON_LVL       1u
#endif
#define TIKU_BOARD_LED_OFF_LVL      (1u - TIKU_BOARD_LED_ON_LVL)

/* LED1 -- P1.22 (silk LED 0) */
#define TIKU_BOARD_LED1_PORT        1u
#define TIKU_BOARD_LED1_PIN         22u
#define TIKU_BOARD_LED1_INIT()      tiku_nordic_gpio_init_output(1u, 22u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED1_ON()        tiku_nordic_gpio_set(1u, 22u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED1_OFF()       tiku_nordic_gpio_set(1u, 22u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_nordic_gpio_toggle(1u, 22u)

/* LED2 -- P1.25 (silk LED 1) */
#define TIKU_BOARD_LED2_PORT        1u
#define TIKU_BOARD_LED2_PIN         25u
#define TIKU_BOARD_LED2_INIT()      tiku_nordic_gpio_init_output(1u, 25u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED2_ON()        tiku_nordic_gpio_set(1u, 25u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED2_OFF()       tiku_nordic_gpio_set(1u, 25u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_nordic_gpio_toggle(1u, 25u)

/* LED3 -- P1.27 (silk LED 2) */
#define TIKU_BOARD_LED3_PORT        1u
#define TIKU_BOARD_LED3_PIN         27u
#define TIKU_BOARD_LED3_INIT()      tiku_nordic_gpio_init_output(1u, 27u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED3_ON()        tiku_nordic_gpio_set(1u, 27u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED3_OFF()       tiku_nordic_gpio_set(1u, 27u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_nordic_gpio_toggle(1u, 27u)

/* LED4 -- P1.28 (silk LED 3) */
#define TIKU_BOARD_LED4_PORT        1u
#define TIKU_BOARD_LED4_PIN         28u
#define TIKU_BOARD_LED4_INIT()      tiku_nordic_gpio_init_output(1u, 28u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED4_ON()        tiku_nordic_gpio_set(1u, 28u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED4_OFF()       tiku_nordic_gpio_set(1u, 28u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED4_TOGGLE()    tiku_nordic_gpio_toggle(1u, 28u)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief On-board buttons (active-low; the DK fits no external pull-up, so
 *        the internal one is enabled; User Guide Table 8).
 *   BTN1 = P1.26 (silk Button 0)   BTN2 = P1.09 (silk Button 1)
 *   BTN3 = P1.08 (silk Button 2)   BTN4 = P0.05 (silk Button 3)
 * Pressed reads 0, so PRESSED() inverts the pin level.
 *
 * BTN4 is on P0, in the LP domain, so its edge interrupts come through
 * GPIOTE30 (tiku_gpio_irq_arch.c).
 */
#define TIKU_BOARD_BTN1_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 26u)
#define TIKU_BOARD_BTN1_PRESSED()   (tiku_nordic_gpio_read(1u, 26u) == 0u)
#define TIKU_BOARD_BTN2_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 9u)
#define TIKU_BOARD_BTN2_PRESSED()   (tiku_nordic_gpio_read(1u, 9u) == 0u)
#define TIKU_BOARD_BTN3_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 8u)
#define TIKU_BOARD_BTN3_PRESSED()   (tiku_nordic_gpio_read(1u, 8u) == 0u)
#define TIKU_BOARD_BTN4_INIT()      tiku_nordic_gpio_init_input_pullup(0u, 5u)
#define TIKU_BOARD_BTN4_PRESSED()   (tiku_nordic_gpio_read(0u, 5u) == 0u)

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/*
 * Console UARTE selection.
 *
 * The DK exposes two VCOM ports through the on-board J-Link OB, routed
 * through analog disconnect switches (User Guide Table 10).
 * TIKU_BOARD_CONSOLE_SEL picks one:
 *
 *   SEL 20 -> UARTE20 on P1.16 (TXD) / P1.17 (RXD)   [PERI domain, board UART1]
 *   SEL 30 -> UARTE30 on P0.06 (TXD) / P0.07 (RXD)   [LP domain,   board UART0]
 *
 * The default, UARTE30 on P0 (board Serial Port 0), is the debugger's first
 * VCOM.  In the board controller's default state UARTE20 reaches no VCOM;
 * Board Configurator connects it.
 */
#ifndef TIKU_BOARD_CONSOLE_SEL
#define TIKU_BOARD_CONSOLE_SEL      30
#endif

#if TIKU_BOARD_CONSOLE_SEL == 20
#define TIKU_BOARD_CONSOLE_UARTE    NRF_UARTE20_S
#define TIKU_BOARD_CONSOLE_UARTE_IRQN 198  /* SERIAL20_IRQn (MDK enum)        */
#define TIKU_BOARD_UART_TX_PORT     1u
#define TIKU_BOARD_UART_TX_PIN      16u
#define TIKU_BOARD_UART_RX_PORT     1u
#define TIKU_BOARD_UART_RX_PIN      17u
#elif TIKU_BOARD_CONSOLE_SEL == 30
#define TIKU_BOARD_CONSOLE_UARTE    NRF_UARTE30_S
#define TIKU_BOARD_CONSOLE_UARTE_IRQN 260  /* SERIAL30_IRQn (MDK enum)        */
#define TIKU_BOARD_UART_TX_PORT     0u
#define TIKU_BOARD_UART_TX_PIN      6u
#define TIKU_BOARD_UART_RX_PORT     0u
#define TIKU_BOARD_UART_RX_PIN      7u
#else
#error "Unsupported TIKU_BOARD_CONSOLE_SEL (expected 20 or 30)"
#endif

/** @brief No-op: the UARTE driver routes the pins through its PSEL regs. */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* PERIPHERAL CAPABILITIES                                                   */
/*---------------------------------------------------------------------------*/

/*
 * The interface layers (interfaces/adc, interfaces/bus, interfaces/onewire)
 * are compiled for this board, over the arch drivers it shares with the
 * nRF54L15: ADC (SAADC), I2C (TWIM) and SPI (SPIM) drive the hardware, and
 * tiku_onewire_arch.c is a stub whose init and reset return -1.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic: not read on this port */
#define TIKU_BOARD_OW_AVAILABLE     1

#endif /* TIKU_BOARD_NRF54LM20_DK_H_ */
