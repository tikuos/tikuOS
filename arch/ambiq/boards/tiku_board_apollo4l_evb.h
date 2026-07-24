/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo4l_evb.h - Ambiq Apollo4 Lite EVB board definitions
 *
 * Pin assignments from the AmbiqSuite R4.5.0 apollo4l_evb BSP:
 *   - User LEDs: LED0 = pad 12, LED1 = pad 13, LED2 = pad 14.
 *   - Console UART (COM, instance 2): TX = pad 54, RX = pad 11.
 *   - Default console transport is SWO/ITM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_APOLLO4L_EVB_H_
#define TIKU_BOARD_APOLLO4L_EVB_H_

#include <stdint.h>
#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string exposed via /sys/device. */
#define TIKU_BOARD_NAME             "Apollo4L EVB"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief LED definitions for the Apollo4 Lite EVB.
 *
 * Three EVB user LEDs: LED0 = pad 12, LED1 = pad 13, LED2 = pad 14 (the
 * TIKU_BOARD_LED1/LED2/LED3 macros are 1-indexed and surface as /dev/led0..2).
 * Assumed active-low (ON drives the pad low), matching the Apollo510 EVB
 * convention; flip the ON/OFF macros if first-light shows inverted polarity.
 */
#define TIKU_BOARD_LED_COUNT        3

/** @brief GPIO pad number for LED 1 (active-low, pad 12). */
#define TIKU_BOARD_LED1_PIN         12U
/** @brief Configure LED 1 pad as a push-pull output. */
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
/** @brief Drive LED 1 on (output low -- active-low LED). */
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
/** @brief Drive LED 1 off (output high). */
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
/** @brief Toggle LED 1 output state. */
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)

/** @brief LED 2 (-> /dev/led1): active-low, pad 13. */
#define TIKU_BOARD_LED2_PIN         13U
#define TIKU_BOARD_LED2_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED2_PIN)

/** @brief LED 3 (-> /dev/led2): active-low, pad 14. */
#define TIKU_BOARD_LED3_PIN         14U
#define TIKU_BOARD_LED3_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED3_PIN)

/*---------------------------------------------------------------------------*/
/* Console UART pins (UART2: TX=54, RX=11)                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART pin assignments.
 *
 * The Apollo4 Lite EVB routes its COM/console to UART instance 2 on pads
 * TX=54, RX=11. The default console transport is SWO/ITM; these pins are used
 * when a wire-UART backend is selected. Pin mux is performed by the UART
 * backend at init time, so the board-level init macro is a no-op.
 */
#define TIKU_BOARD_UART_TX_PIN      54U     /**< UART2 TX pad number. */
#define TIKU_BOARD_UART_RX_PIN      11U     /**< UART2 RX pad number. */
/** @brief Board-level UART pin mux init (no-op; handled by the UART backend). */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons (none wired as plain GPIO yet)                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
/** @brief Read button 1 state (always 0 -- no button wired). */
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
/** @brief Read button 2 state (always 0 -- no button wired). */
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (placeholder -- tiku_bitbang demos)                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bit-bang / backscatter port and pin defaults.
 *
 * The tiku_gpio (port,pin) API encodes a pad as (port-1)*8 + pin, with
 * port >= 1 and pin in 0..7. The pair below selects pad 5 -- a plain GPIO
 * clear of the console UART (54/11) and LED (12/13/14) lines.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       1U   /**< Port 1 -> pad base 0. */
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        5U   /**< pin 5 -> pad 5. */
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

/** @brief ADC not available at this milestone (stub driver). */
#define TIKU_BOARD_ADC_AVAILABLE    0
/** @brief I2C bus rate gate (symbolic -- 100 kHz capable). */
#define TIKU_BOARD_I2C_BRW_100K     1
/** @brief 1-Wire not available at this milestone (stub driver). */
#define TIKU_BOARD_OW_AVAILABLE     0
/** @brief 1-Wire GPIO pad (placeholder). */
#define TIKU_BOARD_OW_PIN           5U

/** @brief I2C0 SDA pad (placeholder -- real IOM pad TBD). */
#define TIKU_BOARD_I2C0_SDA_PIN     0U
/** @brief I2C0 SCL pad (placeholder -- real IOM pad TBD). */
#define TIKU_BOARD_I2C0_SCL_PIN     1U

/** @brief SPI0 MISO pad (placeholder -- real IOM pad TBD). */
#define TIKU_BOARD_SPI0_MISO_PIN    2U
/** @brief SPI0 SCK pad (placeholder -- real IOM pad TBD). */
#define TIKU_BOARD_SPI0_SCK_PIN     3U
/** @brief SPI0 MOSI pad (placeholder -- real IOM pad TBD). */
#define TIKU_BOARD_SPI0_MOSI_PIN    4U

#endif /* TIKU_BOARD_APOLLO4L_EVB_H_ */
