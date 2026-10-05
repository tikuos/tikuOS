/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo4p_evb.h - Ambiq Apollo4 Plus EVB board definitions.
 *
 * Pad assignments from the AmbiqSuite BSP: three user LEDs and the console on
 * UART0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_APOLLO4P_EVB_H_
#define TIKU_BOARD_APOLLO4P_EVB_H_

#include <stdint.h>
#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name. */
#define TIKU_BOARD_NAME             "Apollo4P EVB"

/*---------------------------------------------------------------------------*/
/* LEDS                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Number of user LEDs: LED0-LED2 on pads 12, 13 and 14.
 *
 * TIKU_BOARD_LEDn counts from 1 and backs /dev/led(n-1).  The macros treat
 * the LEDs as active-low: ON drives the pad low.
 */
#define TIKU_BOARD_LED_COUNT        3

/**
 * @name LED 1 (/dev/led0) on pad 12
 * INIT makes the pad a push-pull output; ON drives it low and OFF high.
 * @{
 */
#define TIKU_BOARD_LED1_PIN         12U
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)
/** @} */

/**
 * @name LED 2 (/dev/led1) on pad 13, driven as LED 1
 * @{
 */
#define TIKU_BOARD_LED2_PIN         13U
#define TIKU_BOARD_LED2_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED2_PIN)
/** @} */

/**
 * @name LED 3 (/dev/led2) on pad 14, driven as LED 1
 * @{
 */
#define TIKU_BOARD_LED3_PIN         14U
#define TIKU_BOARD_LED3_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED3_PIN)
/** @} */

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART pads: UART0 TX on pad 60, RX on pad 47.
 *
 * The build selects UART0 for this board with TIKU_CONSOLE_UART0.  The UART
 * driver muxes these pads at init and asserts their values at compile time.
 */
#define TIKU_BOARD_UART_TX_PIN      60U     /**< UART0 TX pad number. */
#define TIKU_BOARD_UART_RX_PIN      47U     /**< UART0 RX pad number. */
/** @brief Empty: the UART driver muxes the console pads itself. */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @name Buttons
 * Stubs: INIT does nothing and PRESSED returns 0.
 * @{
 */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)
/** @} */

/*---------------------------------------------------------------------------*/
/* BIT-BANG PIN                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default bit-bang pad for the tiku_bitbang demo: port 1 pin 5.
 *
 * The (port, pin) GPIO API maps to pad (port - 1) * 8 + pin, with port >= 1
 * and pin 0-7, so this is pad 5, clear of the console and LED pads.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       1U   /**< Port 1 -> pad base 0. */
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        5U   /**< pin 5 -> pad 5. */
#endif

/*---------------------------------------------------------------------------*/
/* BUS GATES                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * The portable ADC and I2C layers build when their gate macro is defined,
 * whatever its value.  This port's ADC driver works; its I2C and 1-Wire
 * drivers touch no hardware, and their init calls return -1.  No Ambiq code
 * reads the 1-Wire, I2C0 or SPI0 pad macros.
 */
/** @brief ADC gate: defined, so the ADC API builds; the value is not read. */
#define TIKU_BOARD_ADC_AVAILABLE    0
/** @brief I2C gate: defined, so the I2C bus API builds on the stub driver. */
#define TIKU_BOARD_I2C_BRW_100K     1
/** @brief No 1-Wire bus: /dev/sensors carries no ds18b20 node. */
#define TIKU_BOARD_OW_AVAILABLE     0
/** @brief 1-Wire pad (unused). */
#define TIKU_BOARD_OW_PIN           5U

/** @brief I2C0 SDA pad (unused). */
#define TIKU_BOARD_I2C0_SDA_PIN     0U
/** @brief I2C0 SCL pad (unused). */
#define TIKU_BOARD_I2C0_SCL_PIN     1U

/** @brief SPI0 MISO pad (unused). */
#define TIKU_BOARD_SPI0_MISO_PIN    2U
/** @brief SPI0 SCK pad (unused). */
#define TIKU_BOARD_SPI0_SCK_PIN     3U
/** @brief SPI0 MOSI pad (unused). */
#define TIKU_BOARD_SPI0_MOSI_PIN    4U

#endif /* TIKU_BOARD_APOLLO4P_EVB_H_ */
