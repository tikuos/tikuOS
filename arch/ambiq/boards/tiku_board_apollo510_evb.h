/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo510_evb.h - Ambiq Apollo 510 EVB board definitions
 *
 * Pin assignments from the AmbiqSuite apollo510_evb BSP:
 *   - User LEDs: LED0 = pad 165, LED1 = pad 89, LED2 = pad 92
 *     (open-drain / active-low in the BSP).
 *   - Console UART (COM): TX = pad 30, RX = pad 55.
 *   - SWO (default console transport): pad 28.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_APOLLO510_EVB_H_
#define TIKU_BOARD_APOLLO510_EVB_H_

#include <stdint.h>
#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "Apollo510 EVB"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/* One primary LED for now (LED0 / pad 165). LED1 (89) and LED2 (92) are
 * also on the board and can be promoted to TIKU_BOARD_LED2/3 later. The
 * EVB LEDs are active-low, so ON drives the pad low. */
#define TIKU_BOARD_LED_COUNT        1

#define TIKU_BOARD_LED1_PIN         165U
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)

/*---------------------------------------------------------------------------*/
/* Console UART pins (TX=30, RX=55)                                          */
/*---------------------------------------------------------------------------*/

/* Default console is SWO/ITM; the COM-UART pins are recorded for when a
 * wire-UART backend is selected. Pin mux is handled by am_hal at init, so
 * the board-level init is a no-op. */
#define TIKU_BOARD_UART_TX_PIN      30U
#define TIKU_BOARD_UART_RX_PIN      55U
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons (none wired as plain GPIO yet)                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (placeholder — tiku_bitbang demos)                           */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       0U
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        13U
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

/* ADC / 1-Wire arch drivers are stubs at this milestone, so the buses are
 * marked unavailable. I2C/SPI pin macros are placeholders (TODO: real IOM
 * pads) — defined so the interface layer compiles. */
#define TIKU_BOARD_ADC_AVAILABLE    0
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic */
#define TIKU_BOARD_OW_AVAILABLE     0
#define TIKU_BOARD_OW_PIN           13U

#define TIKU_BOARD_I2C0_SDA_PIN     0U
#define TIKU_BOARD_I2C0_SCL_PIN     1U

#define TIKU_BOARD_SPI0_MISO_PIN    2U
#define TIKU_BOARD_SPI0_SCK_PIN     3U
#define TIKU_BOARD_SPI0_MOSI_PIN    4U

#endif /* TIKU_BOARD_APOLLO510_EVB_H_ */
