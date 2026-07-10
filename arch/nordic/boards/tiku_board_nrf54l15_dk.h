/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_nrf54l15_dk.h - Nordic nRF54L15-DK (PCA10156) GPIO assignments
 *
 * Pin assignments are from the nRF54L15-DK User Guide.  GPIO helpers take a
 * PHYSICAL port number (0/1/2 == P0/P1/P2) matching the board silk (e.g.
 * P2.09 -> port 2, pin 9); the arch GPIO layer maps that to the port base.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_NRF54L15_DK_H_
#define TIKU_BOARD_NRF54L15_DK_H_

#include <arch/nordic/tiku_gpio_arch.h>   /* tiku_nordic_gpio_* helpers */

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "nRF54L15-DK"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief On-board LED count and per-LED (port,pin).
 *
 * Board silk LED1..LED4 (User Guide) map to TikuOS 1-indexed LEDs:
 *   LED1 = P2.09   LED2 = P1.10   LED3 = P2.07   LED4 = P1.14
 * The DK LEDs are active-LOW (drive the pin low to light).  Verify on
 * hardware in Phase 0; flip TIKU_BOARD_LED_ACTIVE_LOW if inverted.
 */
#define TIKU_BOARD_LED_COUNT        4
#define TIKU_BOARD_LED_ACTIVE_LOW   1

#if TIKU_BOARD_LED_ACTIVE_LOW
#define TIKU_BOARD_LED_ON_LVL       0u
#else
#define TIKU_BOARD_LED_ON_LVL       1u
#endif
#define TIKU_BOARD_LED_OFF_LVL      (1u - TIKU_BOARD_LED_ON_LVL)

/* LED1 -- P2.09 */
#define TIKU_BOARD_LED1_PORT        2u
#define TIKU_BOARD_LED1_PIN         9u
#define TIKU_BOARD_LED1_INIT()      tiku_nordic_gpio_init_output(2u, 9u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED1_ON()        tiku_nordic_gpio_set(2u, 9u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED1_OFF()       tiku_nordic_gpio_set(2u, 9u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_nordic_gpio_toggle(2u, 9u)

/* LED2 -- P1.10 */
#define TIKU_BOARD_LED2_PORT        1u
#define TIKU_BOARD_LED2_PIN         10u
#define TIKU_BOARD_LED2_INIT()      tiku_nordic_gpio_init_output(1u, 10u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED2_ON()        tiku_nordic_gpio_set(1u, 10u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED2_OFF()       tiku_nordic_gpio_set(1u, 10u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_nordic_gpio_toggle(1u, 10u)

/* LED3 -- P2.07 */
#define TIKU_BOARD_LED3_PORT        2u
#define TIKU_BOARD_LED3_PIN         7u
#define TIKU_BOARD_LED3_INIT()      tiku_nordic_gpio_init_output(2u, 7u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED3_ON()        tiku_nordic_gpio_set(2u, 7u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED3_OFF()       tiku_nordic_gpio_set(2u, 7u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_nordic_gpio_toggle(2u, 7u)

/* LED4 -- P1.14 */
#define TIKU_BOARD_LED4_PORT        1u
#define TIKU_BOARD_LED4_PIN         14u
#define TIKU_BOARD_LED4_INIT()      tiku_nordic_gpio_init_output(1u, 14u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED4_ON()        tiku_nordic_gpio_set(1u, 14u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED4_OFF()       tiku_nordic_gpio_set(1u, 14u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED4_TOGGLE()    tiku_nordic_gpio_toggle(1u, 14u)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief On-board buttons (active-LOW, need internal pull-up).
 *   BTN1 = P1.13   BTN2 = P1.09   BTN3 = P1.08   BTN4 = P0.04
 * Pressed reads 0, so PRESSED() inverts the pin level.
 */
#define TIKU_BOARD_BTN1_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 13u)
#define TIKU_BOARD_BTN1_PRESSED()   (tiku_nordic_gpio_read(1u, 13u) == 0u)
#define TIKU_BOARD_BTN2_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 9u)
#define TIKU_BOARD_BTN2_PRESSED()   (tiku_nordic_gpio_read(1u, 9u) == 0u)
#define TIKU_BOARD_BTN3_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 8u)
#define TIKU_BOARD_BTN3_PRESSED()   (tiku_nordic_gpio_read(1u, 8u) == 0u)
#define TIKU_BOARD_BTN4_INIT()      tiku_nordic_gpio_init_input_pullup(0u, 4u)
#define TIKU_BOARD_BTN4_PRESSED()   (tiku_nordic_gpio_read(0u, 4u) == 0u)

/*---------------------------------------------------------------------------*/
/* CONSOLE UART  (SWAPPABLE -- verify VCOM routing on hardware in Phase 1)   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UARTE selection.
 *
 * The DK exposes two VCOM ports (two /dev/ttyACM* devices) via the on-board
 * interface MCU, routed through analog disconnect switches (schematic sheet 4:
 * UART0_nRF54 = P0.00-03, UART1_nRF54 = P1.04-07).  This is a single swappable
 * block:
 *
 *   SEL 20 -> UARTE20 on P1.04 (TXD) / P1.05 (RXD)   [PERI domain, board UART1]
 *   SEL 30 -> UARTE30 on P0.00 (TXD) / P0.01 (RXD)   [LP domain,   board UART0]
 *
 * Default is UARTE20/P1.  CONFIRMED on hardware: UARTE20 appears on the
 * SECOND VCOM (/dev/ttyACM1, vcom 1) at 115200 8N1.
 */
#ifndef TIKU_BOARD_CONSOLE_SEL
#define TIKU_BOARD_CONSOLE_SEL      20
#endif

#if TIKU_BOARD_CONSOLE_SEL == 20
#define TIKU_BOARD_CONSOLE_UARTE    NRF_UARTE20_S
#define TIKU_BOARD_CONSOLE_UARTE_IRQN 198  /* SERIAL20_IRQn (MDK enum)        */
#define TIKU_BOARD_UART_TX_PORT     1u
#define TIKU_BOARD_UART_TX_PIN      4u
#define TIKU_BOARD_UART_RX_PORT     1u
#define TIKU_BOARD_UART_RX_PIN      5u
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

/** @brief UART pin mux is programmed by the UARTE driver (PSEL regs). */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* PERIPHERAL CAPABILITIES                                                   */
/*---------------------------------------------------------------------------*/

/*
 * The nRF54L15 silicon has SAADC, TWIM (I2C), SPIM, and GPIO the 1-Wire glue
 * can bit-bang, so the interface layers (interfaces/adc, interfaces/bus,
 * interfaces/onewire) are compiled in.  The arch backends are honest stubs for
 * now (tiku_{adc,i2c,spi,onewire}_arch.c return "not implemented"), so the
 * /dev nodes and shell commands exist and report a clear error rather than
 * fabricating data -- real drivers are a later phase.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic (arch stub) */
#define TIKU_BOARD_OW_AVAILABLE     1

#endif /* TIKU_BOARD_NRF54L15_DK_H_ */
