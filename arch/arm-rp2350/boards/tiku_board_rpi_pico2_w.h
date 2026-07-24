/*
 * Tiku Operating System v0.06
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

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "Raspberry Pi Pico 2 W"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Number of on-board user LEDs visible to the kernel.
 *
 * WiFi-driver builds (TIKU_DRV_WIFI_CYW43_ENABLE=1): GP25 is WL_CS and
 * cannot be used as LED; only LED1 (GP15) is exposed. Non-WiFi builds:
 * GP25 and GP15 are both free, so two LEDs are reported.
 */
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#define TIKU_BOARD_LED_COUNT        1
#else
#define TIKU_BOARD_LED_COUNT        2
#endif

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

/**
 * @brief Drive an absolute GPIO pin to a logic level.
 *
 * Writes the SIO GPIO_OUT_SET / GPIO_OUT_CLR registers, so the update
 * is atomic and leaves every other pin alone. (SIO ignores the generic
 * +0x2000 / +0x3000 atomic aliases; its own adjacent SET/CLR/XOR
 * registers are the only correct way to do this.) The pin must already
 * be an output; pins above GP29 are silently ignored.
 *
 * @param pin    Absolute GP pin number (0..29).
 * @param value  0 drives the pin low; non-zero drives it high.
 */
void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value);

/**
 * @brief Toggle an absolute GPIO output pin.
 *
 * Writes the SIO GPIO_OUT_XOR register: one atomic flip that does not
 * disturb other pins. Pins above GP29 are silently ignored.
 *
 * @param pin  Absolute GP pin number (0..29).
 */
void tiku_rp2350_gpio_toggle(uint8_t pin);

/**
 * @brief LED 1 pin assignment and control macros.
 *
 * In WiFi-driver builds GP25 is CYW43 WL_CS, so LED1 moves to GP15
 * and the VFS LED init path must not drive GP25 low before the radio
 * reset strap is sampled. In non-WiFi (plain bring-up) builds GP25 is
 * kept as a compatibility GPIO because the actual board LED lives behind
 * the CYW43.
 */
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#define TIKU_BOARD_LED1_PIN         15U
#else
#define TIKU_BOARD_LED1_PIN         25U
#endif
#define TIKU_BOARD_LED1_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED1_PIN)

/**
 * @brief LED 2 pin assignment and control macros (GP15, non-WiFi builds).
 *
 * In WiFi builds GP15 is already LED1, so TIKU_BOARD_LED_COUNT hides
 * LED2 from callers and these macros are never referenced.
 */
#define TIKU_BOARD_LED2_PIN         15U
#define TIKU_BOARD_LED2_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED2_PIN)

/*---------------------------------------------------------------------------*/
/* Backchannel UART - TX=GP0, RX=GP1 (UART0)                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief UART0 backchannel pin assignments.
 *
 * Pin mux for UART0 on the RP2350: function-2 on GP0 (TX) and GP1 (RX).
 * The IO_BANK0 / PADS_BANK0 setup is done inline in tiku_uart_arch.c;
 * these macros are kept only for symmetry with the MSP430 boards.
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
 * The Pico 2 W only exposes one button (BOOTSEL) and it is on the QSPI
 * bank, not bank 0. Using it as a runtime input requires temporarily
 * disabling XIP and is not safe to expose as a generic GPIO. Both
 * button macros are no-ops.
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
 * GP14 is exposed on the header (physical pin 19) and not claimed by
 * UART / SPI / I2C / LED / 1-Wire on this port, so a scope probe can
 * verify the test_bitbang output pattern. RP2350 has a single GPIO bank
 * so port is 0 by convention. Override at compile time via
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
 * Platform-independent bus drivers (interfaces/adc/, interfaces/bus/,
 * interfaces/onewire/) self-gate to empty translation units when these
 * macros are absent. Declaring them here ensures the wrappers compile.
 * The underlying RP2350 arch implementations are stubs that return
 * "not supported" sentinels until the real drivers are written. When
 * the real ADC/I2C/SPI drivers are implemented, these gates already
 * pull the platform-independent layer in. I2C_BRW_100K is symbolic
 * on RP2350 — speed is configured in tiku_i2c_arch.c.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* unused on RP2350 — symbolic */
#define TIKU_BOARD_OW_AVAILABLE     1

/**
 * @brief 1-Wire data pin assignment (GP15).
 *
 * GP15 is a free header pin with no peripheral default function on
 * Pico 2 W (clear of UART0 GP0/1, I2C0 GP4/5, SPI0 GP16-19, ADC
 * GP26-29, CYW43 GP23-25). External 4.7 kohm pull-up to 3V3 is
 * required on the data line.
 */
#define TIKU_BOARD_OW_PIN           15U

/**
 * @brief I2C0 pin assignment (GP4=SDA, GP5=SCL).
 *
 * RP2350 IO_BANK0 maps function 3 = I2C. GP4/GP5 are the conventional
 * pair (matches Pico SDK default and the Adafruit/SparkFun breakouts).
 * External pull-ups required on both lines.
 */
#define TIKU_BOARD_I2C0_SDA_PIN     4U
#define TIKU_BOARD_I2C0_SCL_PIN     5U

/**
 * @brief SPI0 pin assignment (GP16=MISO, GP18=SCK, GP19=MOSI).
 *
 * Function 1 = SPI on RP2350 IO_BANK0. Standard Pico/Pico 2 mapping.
 * CS (SS) is left to the application — drive any free GPIO from user
 * code.
 */
#define TIKU_BOARD_SPI0_MISO_PIN    16U
#define TIKU_BOARD_SPI0_SCK_PIN     18U
#define TIKU_BOARD_SPI0_MOSI_PIN    19U

/*---------------------------------------------------------------------------*/
/* CYW43439 (WiFi/BT module) pinout                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief CYW43439 gSPI bus pin assignments.
 *
 * Fixed by Raspberry Pi's Pico 2 W board design — not configurable.
 * The gSPI bus uses a single bidirectional DATA line (WL_DATA), which
 * is a Pico-W-family quirk; PIO is required because the RP2350 SPI
 * peripheral assumes separate MOSI/MISO pins.
 *
 * GP29 is shared with ADC channel 3 (VSYS-divide battery sense).
 * Activating the radio makes that ADC read unavailable.
 *
 * Used by tikudrivers/wifi/cyw43/ when TIKU_DRV_WIFI_CYW43_ENABLE=1.
 * Without that flag these defines cost nothing — they are not referenced
 * by core kernel code.
 */
#define TIKU_BOARD_CYW43_WL_REG_ON_PIN  23U  /**< Power-on enable */
#define TIKU_BOARD_CYW43_WL_DATA_PIN    24U  /**< Bidirectional gSPI DATA */
#define TIKU_BOARD_CYW43_WL_CS_PIN      25U  /**< Chip select */
#define TIKU_BOARD_CYW43_WL_CLOCK_PIN   29U  /**< gSPI clock */

#endif /* TIKU_BOARD_RPI_PICO2_W_H_ */
