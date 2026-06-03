/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_rpi_pico2.h - Raspberry Pi Pico 2 board definitions
 *
 * Plain Pi Pico 2 (no CYW43439). The board is electrically simpler
 * than the Pico 2 W: no wireless module, no WL_REG_ON / WL_DATA /
 * WL_CS / WL_CLOCK lines, the on-board user LED is a real GPIO
 * (GP25) instead of being routed through the wireless chip's
 * WL_GPIO0.
 *
 * Board layout (per the Pico 2 datasheet):
 *   - 12 MHz crystal on XIN/XOUT (XOSC)
 *   - 4 MB QSPI flash (W25Q32 family) at 0x10000000
 *   - User LED on GP25 (drive high to illuminate)
 *   - UART0 default backchannel: TX=GP0, RX=GP1
 *   - Button S1 (BOOTSEL): wired through QSPI bank — not usable as
 *     a plain GPIO without entering a special mode. Stub for now.
 *
 * The non-LED / non-wireless pin assignments match the Pico 2 W
 * verbatim: same I2C0 / SPI0 / 1-Wire / bit-bang pin choices, same
 * ADC channels, same UART. Code that runs on Pico 2 W and doesn't
 * touch the CYW43 should run unchanged here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_RPI_PICO2_H_
#define TIKU_BOARD_RPI_PICO2_H_

#include <arch/arm-rp2350/tiku_rp2350_regs.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "Raspberry Pi Pico 2"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

/* One user LED, on GP25. The Pico 2 W has to fake LED1 onto a
 * different pin because GP25 there is WL_CS — but on plain Pico 2
 * the LED is wired straight to GP25 and we can use it directly. */
#define TIKU_BOARD_LED_COUNT        1

/*---------------------------------------------------------------------------*/
/* GPIO LED helpers                                                          */
/*---------------------------------------------------------------------------*/

void tiku_rp2350_gpio_init_output(uint8_t pin);
void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value);
void tiku_rp2350_gpio_toggle(uint8_t pin);

#define TIKU_BOARD_LED1_PIN         25U
#define TIKU_BOARD_LED1_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED1_PIN)

/*---------------------------------------------------------------------------*/
/* Backchannel UART - TX=GP0, RX=GP1 (UART0)                                 */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_UART_TX_PIN      0U
#define TIKU_BOARD_UART_RX_PIN      1U
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons                                                                   */
/*---------------------------------------------------------------------------*/

/* BOOTSEL is on the QSPI bank, not bank 0 — using it as a runtime
 * input requires temporarily disabling XIP and is not safe to expose
 * as a generic GPIO. Both button macros are no-ops. */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (tiku_bitbang demos / PIO backend / backscatter dev)         */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       0U
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        14U
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic */
#define TIKU_BOARD_OW_AVAILABLE     1

/* 1-Wire data pin. GP15 is free here (no peripheral default,
 * no CYW43 reservation since the W's WL_CS is not present). */
#define TIKU_BOARD_OW_PIN           15U

/* I2C0: GP4=SDA, GP5=SCL (standard Pico mapping). */
#define TIKU_BOARD_I2C0_SDA_PIN     4U
#define TIKU_BOARD_I2C0_SCL_PIN     5U

/* SPI0: GP16=MISO, GP18=SCK, GP19=MOSI (standard Pico mapping). */
#define TIKU_BOARD_SPI0_MISO_PIN    16U
#define TIKU_BOARD_SPI0_SCK_PIN     18U
#define TIKU_BOARD_SPI0_MOSI_PIN    19U

/* No CYW43 pinout on plain Pico 2 — those macros (CYW43_WL_REG_ON
 * etc.) are deliberately absent here. The driver's Makefile gate
 * (TIKU_DRV_WIFI_CYW43_ENABLE requires BOARD=pico2w) ensures the
 * driver isn't compiled into a Pico 2 build. */

#endif /* TIKU_BOARD_RPI_PICO2_H_ */
