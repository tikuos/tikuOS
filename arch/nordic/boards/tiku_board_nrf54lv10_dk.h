/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_nrf54lv10_dk.h - Nordic nRF54LV10-DK (PCA10188) GPIO assignments.
 *
 * Pin assignments are from the board's Zephyr devicetree (nrf54lv10dk).
 * Helpers take a physical port matching the board silk.  The silk numbers
 * LEDs and buttons from 0 and TikuOS from 1, so silk "LED 0" is TikuOS LED1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_NRF54LV10_DK_H_
#define TIKU_BOARD_NRF54LV10_DK_H_

#include <arch/nordic/tiku_gpio_arch.h>   /* tiku_nordic_gpio_* helpers */

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "nRF54LV10-DK"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief On-board LED count and per-LED (port,pin).
 *
 * Board silk LED0..LED3 map to TikuOS 1-indexed LEDs:
 *   LED1 = P1.24   LED2 = P1.10   LED3 = P1.25   LED4 = P1.22
 * The LEDs are active-high.
 */
#define TIKU_BOARD_LED_COUNT        4
#define TIKU_BOARD_LED_ACTIVE_LOW   0

#if TIKU_BOARD_LED_ACTIVE_LOW
#define TIKU_BOARD_LED_ON_LVL       0u
#else
#define TIKU_BOARD_LED_ON_LVL       1u
#endif
#define TIKU_BOARD_LED_OFF_LVL      (1u - TIKU_BOARD_LED_ON_LVL)

/* LED1 -- P1.24 (silk LED 0) */
#define TIKU_BOARD_LED1_PORT        1u
#define TIKU_BOARD_LED1_PIN         24u
#define TIKU_BOARD_LED1_INIT()      tiku_nordic_gpio_init_output(1u, 24u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED1_ON()        tiku_nordic_gpio_set(1u, 24u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED1_OFF()       tiku_nordic_gpio_set(1u, 24u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_nordic_gpio_toggle(1u, 24u)

/* LED2 -- P1.10 (silk LED 1) */
#define TIKU_BOARD_LED2_PORT        1u
#define TIKU_BOARD_LED2_PIN         10u
#define TIKU_BOARD_LED2_INIT()      tiku_nordic_gpio_init_output(1u, 10u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED2_ON()        tiku_nordic_gpio_set(1u, 10u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED2_OFF()       tiku_nordic_gpio_set(1u, 10u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_nordic_gpio_toggle(1u, 10u)

/* LED3 -- P1.25 (silk LED 2) */
#define TIKU_BOARD_LED3_PORT        1u
#define TIKU_BOARD_LED3_PIN         25u
#define TIKU_BOARD_LED3_INIT()      tiku_nordic_gpio_init_output(1u, 25u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED3_ON()        tiku_nordic_gpio_set(1u, 25u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED3_OFF()       tiku_nordic_gpio_set(1u, 25u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_nordic_gpio_toggle(1u, 25u)

/* LED4 -- P1.22 (silk LED 3) */
#define TIKU_BOARD_LED4_PORT        1u
#define TIKU_BOARD_LED4_PIN         22u
#define TIKU_BOARD_LED4_INIT()      tiku_nordic_gpio_init_output(1u, 22u, \
                                        TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED4_ON()        tiku_nordic_gpio_set(1u, 22u, TIKU_BOARD_LED_ON_LVL)
#define TIKU_BOARD_LED4_OFF()       tiku_nordic_gpio_set(1u, 22u, TIKU_BOARD_LED_OFF_LVL)
#define TIKU_BOARD_LED4_TOGGLE()    tiku_nordic_gpio_toggle(1u, 22u)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief On-board buttons (active-low, internal pull-up enabled).
 *   BTN1 = P1.21 (silk Button 0)   BTN2 = P1.09 (silk Button 1)
 *   BTN3 = P1.08 (silk Button 2)   BTN4 = P0.04 (silk Button 3)
 * Pressed reads 0, so PRESSED() inverts the pin level.
 *
 * BTN4 is on P0, in the LP domain, so its edge interrupts come through
 * GPIOTE30 (tiku_gpio_irq_arch.c).
 */
/** @brief Buttons /dev/btn0..3 expose, BTN1..BTN4 in order. */
#define TIKU_BOARD_BTN_COUNT        4
#define TIKU_BOARD_BTN1_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 21u)
#define TIKU_BOARD_BTN1_PRESSED()   (tiku_nordic_gpio_read(1u, 21u) == 0u)
#define TIKU_BOARD_BTN2_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 9u)
#define TIKU_BOARD_BTN2_PRESSED()   (tiku_nordic_gpio_read(1u, 9u) == 0u)
#define TIKU_BOARD_BTN3_INIT()      tiku_nordic_gpio_init_input_pullup(1u, 8u)
#define TIKU_BOARD_BTN3_PRESSED()   (tiku_nordic_gpio_read(1u, 8u) == 0u)
#define TIKU_BOARD_BTN4_INIT()      tiku_nordic_gpio_init_input_pullup(0u, 4u)
#define TIKU_BOARD_BTN4_PRESSED()   (tiku_nordic_gpio_read(0u, 4u) == 0u)

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/*
 * Console UARTE.
 *
 * The part has three SERIAL instances: SERIAL20 and SERIAL21 in the PERI
 * domain, SERIAL30 in the LP domain.  The console is UARTE30 on P0.00 (TXD)
 * / P0.01 (RXD), the board's UART0, which the J-Link OB carries to a VCOM;
 * SERIAL20 is the I2C master and SERIAL21 the SPI master below.
 * TIKU_BOARD_CONSOLE_SEL accepts 30 only.
 */
#ifndef TIKU_BOARD_CONSOLE_SEL
#define TIKU_BOARD_CONSOLE_SEL      30
#endif

#if TIKU_BOARD_CONSOLE_SEL == 30
#define TIKU_BOARD_CONSOLE_UARTE    NRF_UARTE30_S
#define TIKU_BOARD_CONSOLE_UARTE_IRQN 260  /* SERIAL30_IRQn (MDK enum)        */
#define TIKU_BOARD_UART_TX_PORT     0u
#define TIKU_BOARD_UART_TX_PIN      0u
#define TIKU_BOARD_UART_RX_PORT     0u
#define TIKU_BOARD_UART_RX_PIN      1u
#else
#error "Unsupported TIKU_BOARD_CONSOLE_SEL (expected 30): SERIAL20 is the I2C master on this board"
#endif

/** @brief No-op: the UARTE driver routes the pins through its PSEL regs. */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* BUS INSTANCES                                                             */
/*---------------------------------------------------------------------------*/

/*
 * The part has no SERIAL22, the nRF54L15's I2C instance: I2C runs on TWIM20.
 * SPI keeps the driver default, SPIM21.  Both drivers' default pins are on
 * P1 (tiku_i2c_arch.c, tiku_spi_arch.c).
 */
#define TIKU_BOARD_I2C_TWIM         NRF_TWIM20_S

/*---------------------------------------------------------------------------*/
/* PERIPHERAL CAPABILITIES                                                   */
/*---------------------------------------------------------------------------*/

/*
 * The interface layers (interfaces/adc, interfaces/bus, interfaces/onewire)
 * are compiled for this board.  ADC (SAADC), I2C (TWIM) and SPI (SPIM) have
 * drivers; tiku_onewire_arch.c is a stub whose calls fail.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* compiles the tiku_i2c_bus.c API */
#define TIKU_BOARD_OW_AVAILABLE     1

#endif /* TIKU_BOARD_NRF54LV10_DK_H_ */
