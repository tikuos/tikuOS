/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - RP2350 GPIO port access
 *
 * The RP2350 has a single bank of 30+ pins. To stay shell/VFS-compatible
 * with the MSP430 layout (/dev/gpio/{1..4}/{0..7}) we expose four
 * virtual ports of 8 pins each:
 *   port 1 -> GP0..GP7
 *   port 2 -> GP8..GP15
 *   port 3 -> GP16..GP23
 *   port 4 -> GP24..GP31
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_GPIO_ARCH_H_
#define TIKU_RP2350_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Configure a virtual-port pin as a digital output.
 *
 * Sets the SIO OE bit and pads the IO_BANK0 function to SIO (F5).
 * port 1 maps to GP0..GP7, port 2 to GP8..GP15, etc.
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @return 0 on success, negative on invalid port/pin.
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/**
 * @brief Configure a virtual-port pin as a digital input.
 *
 * Clears the SIO OE bit and sets the pad to input-enable. The pull
 * resistor state is left at its hardware default (none).
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @return 0 on success, negative on invalid port/pin.
 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/**
 * @brief Write a logic level to a virtual-port output pin.
 *
 * Uses SIO GPIO_OUT_SET / GPIO_OUT_CLR for atomic single-bit
 * updates. The pin must have been configured as an output first.
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @param val   0 to drive low, non-zero to drive high.
 * @return 0 on success, negative on invalid port/pin.
 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/**
 * @brief Toggle a virtual-port output pin.
 *
 * Uses SIO GPIO_OUT_XOR for an atomic read-modify-write toggle.
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @return 0 on success, negative on invalid port/pin.
 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);

/**
 * @brief Read the current logic level of a virtual-port pin.
 *
 * Samples SIO GPIO_IN. Works on both input and output pins
 * (an output pin reads back its own driven level).
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @return 0 or 1 for the logic level, negative on invalid port/pin.
 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);

/**
 * @brief Return the direction of a virtual-port pin.
 *
 * Reads the SIO GPIO_OE register for the corresponding absolute pin.
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @return 1 if configured as output, 0 if input, negative on error.
 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

/**
 * @brief Per-pin direct GPIO helpers used by the LED macros.
 *
 * These operate on the absolute RP2350 pin number (0..29) and bypass
 * the virtual-port layer. They are called directly from the board
 * LED macros (TIKU_BOARD_LEDn_INIT/ON/OFF/TOGGLE) and must not be
 * used for pins that are also managed via the port-based API.
 */

/**
 * @brief Configure an absolute GPIO pin as a push-pull output.
 *
 * Sets the IO_BANK0 function to SIO (F5), enables the output driver,
 * and sets the initial level to 0. Idempotent.
 *
 * @param pin  Absolute GPIO number (0..29).
 */
void tiku_rp2350_gpio_init_output(uint8_t pin);

/**
 * @brief Drive an absolute GPIO pin to a given logic level.
 *
 * Uses SIO GPIO_OUT_SET / GPIO_OUT_CLR for an atomic update.
 *
 * @param pin    Absolute GPIO number (0..29).
 * @param value  0 to drive low, non-zero to drive high.
 */
void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value);

/**
 * @brief Toggle an absolute GPIO output pin.
 *
 * Uses SIO GPIO_OUT_XOR for an atomic toggle.
 *
 * @param pin  Absolute GPIO number (0..29).
 */
void tiku_rp2350_gpio_toggle(uint8_t pin);

#endif /* TIKU_RP2350_GPIO_ARCH_H_ */
