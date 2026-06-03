/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_rpi_pico2_w.h - Raspberry Pi Pico 2 W board definitions
 *
 * Board layout (per the Pico 2 W datasheet):
 *   - 12 MHz crystal on XIN/XOUT (XOSC)
 *   - 4 MB QSPI flash (W25Q32 family) at 0x10000000
 *   - CYW43439 wireless (SPI to GP23..GP25 + GP29) — out of scope for
 *     this first port; the wireless stack is intentionally stubbed.
 *   - LED is wired to the CYW43 chip's WL_GPIO0, NOT a CPU GPIO. To
 *     keep the LED demo working we expose GP25 as the "user LED" since
 *     it is broken out on the header and unused by anything else when
 *     the wireless chip is held in reset. The plain Pico 2 wires its
 *     LED to GP25 directly so existing examples just work there.
 *   - UART0 default backchannel: TX=GP0, RX=GP1 (matches the Pico SDK
 *     default, picotool, the Debug Probe, and OpenOCD documentation).
 *   - Button S1 (BOOTSEL): wired through QSPI bank — not usable as a
 *     plain GPIO without entering a special mode. Stub for now.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_RPI_PICO2_W_H_
#define TIKU_BOARD_RPI_PICO2_W_H_

#include <arch/arm-rp2350/tiku_rp2350_regs.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "Raspberry Pi Pico 2 W"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#define TIKU_BOARD_LED_COUNT        1
#else
#define TIKU_BOARD_LED_COUNT        2
#endif

/*---------------------------------------------------------------------------*/
/* GPIO LED helpers                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Forward-declare the GPIO arch helpers (defined in
 * arch/arm-rp2350/tiku_gpio_arch.c) so the LED macros below don't
 * pull the rest of the kernel's GPIO header into the include chain.
 */
void tiku_rp2350_gpio_init_output(uint8_t pin);
void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value);
void tiku_rp2350_gpio_toggle(uint8_t pin);

/* User LED:
 *   normal Pico-2-W bring-up builds: GP25 is kept as a compatibility
 *   "LED" GPIO because the actual board LED lives behind the CYW43.
 *   WiFi-driver builds: GP25 is CYW43 WL_CS, so LED1 moves to GP15
 *   and the VFS LED init path must not drive GP25 low before the
 *   radio reset strap is sampled. */
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#define TIKU_BOARD_LED1_PIN         15U
#else
#define TIKU_BOARD_LED1_PIN         25U
#endif
#define TIKU_BOARD_LED1_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED1_PIN)

/* Second user LED: GP15 in non-WiFi builds. In WiFi builds GP15 is
 * already LED1, so TIKU_BOARD_LED_COUNT hides LED2 from callers. */
#define TIKU_BOARD_LED2_PIN         15U
#define TIKU_BOARD_LED2_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED2_PIN)

/*---------------------------------------------------------------------------*/
/* Backchannel UART - TX=GP0, RX=GP1 (UART0)                                 */
/*---------------------------------------------------------------------------*/

/* Pin mux for UART0 on the RP2350: function-2 on GP0 (TX) and GP1
 * (RX). The IO_BANK0 / PADS_BANK0 setup is done inline in
 * tiku_uart_arch.c; these macros are kept only for symmetry with the
 * MSP430 boards. */
#define TIKU_BOARD_UART_TX_PIN      0U
#define TIKU_BOARD_UART_RX_PIN      1U
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons                                                                   */
/*---------------------------------------------------------------------------*/

/* The Pico 2 W only exposes one button (BOOTSEL) and it is on the
 * QSPI bank, not bank 0 — using it as a runtime input requires
 * temporarily disabling XIP and is not safe to expose as a generic
 * GPIO. For this port both button macros are no-ops. */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (tiku_bitbang demos / PIO backend / backscatter dev)         */
/*---------------------------------------------------------------------------*/

/* GP14 is exposed on the header (physical pin 19) and not claimed
 * by UART / SPI / I2C / LED / 1-Wire on this port, so a scope probe
 * can verify the test_bitbang output pattern. RP2350 has a single
 * GPIO bank so port is unused -- 0 by convention. Override at
 * compile time via -DTIKU_BOARD_BSCAT_PIN=<n>. */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       0U
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        14U
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

/*
 * The platform-independent bus drivers (interfaces/adc/, interfaces/bus/,
 * interfaces/onewire/) self-gate to empty translation units when these
 * macros are not defined. We declare them so the wrappers compile —
 * the underlying RP2350 arch implementations are stubs that return
 * "not supported" sentinels (see arch/arm-rp2350/tiku_*_arch.c).
 *
 * When the real ADC/I2C/SPI drivers are written, these gates already
 * pull the platform-independent layer in.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* unused on RP2350 — symbolic */
#define TIKU_BOARD_OW_AVAILABLE     1

/* 1-Wire data pin. GP15 is a free header pin with no peripheral
 * default function on Pico 2 W (clear of UART0 GP0/1, I2C0 GP4/5,
 * SPI0 GP16-19, ADC GP26-29, CYW43 GP23-25). External 4.7 kohm
 * pull-up to 3V3 is required on the data line. */
#define TIKU_BOARD_OW_PIN           15U

/* I2C0 pin assignment. RP2350 IO_BANK0 maps function 3 = I2C; on the
 * Pico 2 W header GP4/GP5 are the conventional pair (matches Pico SDK
 * default and the Adafruit/SparkFun breakouts). External pull-ups
 * required on both lines. */
#define TIKU_BOARD_I2C0_SDA_PIN     4U
#define TIKU_BOARD_I2C0_SCL_PIN     5U

/* SPI0 pin assignment. Function 1 = SPI on RP2350 IO_BANK0. Standard
 * Pico/Pico 2 mapping: GP16=MISO, GP18=SCK, GP19=MOSI. CS (SS) is
 * left to the application — drive any free GPIO from user code. */
#define TIKU_BOARD_SPI0_MISO_PIN    16U
#define TIKU_BOARD_SPI0_SCK_PIN     18U
#define TIKU_BOARD_SPI0_MOSI_PIN    19U

/*---------------------------------------------------------------------------*/
/* CYW43439 (WiFi/BT module) pinout                                          */
/*---------------------------------------------------------------------------*/
/*
 * Fixed by Raspberry Pi's Pico 2 W board design — not configurable.
 * The gSPI bus uses a single bidirectional DATA line (WL_DATA),
 * which is a Pico-W-family quirk. PIO is required because the
 * RP2350 SPI peripheral assumes separate MOSI/MISO pins.
 *
 * GP29 is shared with ADC channel 3 (VSYS-divide battery sense).
 * Activating the radio makes that ADC read unavailable.
 *
 * Used by tikudrivers/wifi/cyw43/ when
 * TIKU_DRV_WIFI_CYW43_ENABLE=1. Without that flag these defines
 * cost nothing — they're not referenced by core kernel code.
 */
#define TIKU_BOARD_CYW43_WL_REG_ON_PIN  23U  /* power-on enable */
#define TIKU_BOARD_CYW43_WL_DATA_PIN    24U  /* bidirectional DATA */
#define TIKU_BOARD_CYW43_WL_CS_PIN      25U  /* chip select */
#define TIKU_BOARD_CYW43_WL_CLOCK_PIN   29U  /* gSPI clock */

#endif /* TIKU_BOARD_RPI_PICO2_W_H_ */
