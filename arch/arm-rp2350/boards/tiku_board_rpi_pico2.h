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

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "Raspberry Pi Pico 2"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Number of on-board user LEDs.
 *
 * One user LED, on GP25. The Pico 2 W has to fake LED1 onto a different
 * pin because GP25 there is WL_CS — but on plain Pico 2 the LED is wired
 * straight to GP25 and can be used directly.
 */
#define TIKU_BOARD_LED_COUNT        1

/*---------------------------------------------------------------------------*/
/* GPIO LED helpers                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Forward declarations for GPIO arch helpers.
 *
 * Defined in arch/arm-rp2350/tiku_gpio_arch.c. Declared here so the
 * LED macros below do not pull the full GPIO header into the include chain.
 */
void tiku_rp2350_gpio_init_output(uint8_t pin);
void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value);
void tiku_rp2350_gpio_toggle(uint8_t pin);

/**
 * @brief LED 1 pin assignment and control macros (GP25).
 *
 * Drive the pin high to illuminate the LED. These macros are consumed
 * by interfaces/led/tiku_led.c to implement the indexed LED API.
 */
#define TIKU_BOARD_LED1_PIN         25U
#define TIKU_BOARD_LED1_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED1_PIN)

/*---------------------------------------------------------------------------*/
/* Backchannel UART - TX=GP0, RX=GP1 (UART0)                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief UART0 backchannel pin assignments.
 *
 * Function-2 on GP0 (TX) and GP1 (RX). The IO_BANK0 / PADS_BANK0 mux
 * setup is done in tiku_uart_arch.c; these macros exist for symmetry
 * with the MSP430 board headers.
 */
#define TIKU_BOARD_UART_TX_PIN      0U
#define TIKU_BOARD_UART_RX_PIN      1U
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Button macros (no-ops — BOOTSEL is on the QSPI bank).
 *
 * BOOTSEL is on the QSPI bank, not bank 0. Using it as a runtime
 * input requires temporarily disabling XIP and is not safe to expose
 * as a generic GPIO. Both button macros are no-ops.
 */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (tiku_bitbang demos / PIO backend / backscatter dev)         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bit-bang / backscatter output pin assignment.
 *
 * GP14 defaults are shared with the Pico 2 W port (no peripheral
 * conflict on either board). RP2350 has a single GPIO bank so port
 * is 0 by convention. Override at compile time via
 * -DTIKU_BOARD_BSCAT_PIN=<n>.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       0U
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        14U
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bus and peripheral availability flags.
 *
 * Platform-independent drivers self-gate to empty translation units when
 * these macros are absent. Declaring them here pulls in the RP2350 arch
 * implementations. I2C_BRW_100K is symbolic — the RP2350 I2C driver
 * speed is set in tiku_i2c_arch.c, not here.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic */
#define TIKU_BOARD_OW_AVAILABLE     1

/**
 * @brief 1-Wire data pin assignment.
 *
 * GP15 is free here (no peripheral default, no CYW43 reservation
 * since WL_CS is absent on the plain Pico 2). An external 4.7 kohm
 * pull-up to 3V3 is required on the data line.
 */
#define TIKU_BOARD_OW_PIN           15U

/**
 * @brief I2C0 pin assignment (GP4=SDA, GP5=SCL).
 *
 * Standard Pico mapping; function 3 on RP2350 IO_BANK0. External
 * pull-ups required on both lines.
 */
#define TIKU_BOARD_I2C0_SDA_PIN     4U
#define TIKU_BOARD_I2C0_SCL_PIN     5U

/**
 * @brief SPI0 pin assignment (GP16=MISO, GP18=SCK, GP19=MOSI).
 *
 * Standard Pico/Pico 2 mapping; function 1 on RP2350 IO_BANK0.
 * CS (SS) is left to the application — drive any free GPIO from
 * user code.
 */
#define TIKU_BOARD_SPI0_MISO_PIN    16U
#define TIKU_BOARD_SPI0_SCK_PIN     18U
#define TIKU_BOARD_SPI0_MOSI_PIN    19U

/* No CYW43 pinout on plain Pico 2 — those macros (CYW43_WL_REG_ON
 * etc.) are deliberately absent here. The driver's Makefile gate
 * (TIKU_DRV_WIFI_CYW43_ENABLE requires BOARD=pico2w) ensures the
 * driver isn't compiled into a Pico 2 build. */

#endif /* TIKU_BOARD_RPI_PICO2_H_ */
