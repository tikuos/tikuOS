/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - RP2350 GPIO port access.
 *
 * Bank 0's GP0-GP29 are presented as four virtual ports of eight pins, the
 * layout the shell and VFS use: port 1 is GP0-GP7, port 2 GP8-GP15, port 3
 * GP16-GP23 and port 4 GP24-GP29.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_GPIO_ARCH_H_
#define TIKU_RP2350_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Configure a virtual-port pin as a digital output.
 *
 * Routes the pin to SIO (function 5), sets its pad to 4 mA with the input
 * buffer on, and enables the output, driven low.
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @return 0 on success, negative on invalid port/pin.
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/**
 * @brief Configure a virtual-port pin as a digital input.
 *
 * Routes the pin to SIO, sets its pad to input-enable with the pull-up and
 * the Schmitt trigger on, and clears the SIO OE bit.
 *
 * @param port  Virtual port number (1..4).
 * @param pin   Pin within the port (0..7).
 * @return 0 on success, negative on invalid port/pin.
 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/**
 * @brief Write a logic level to a virtual-port pin, making it an output.
 *
 * Configures the pin as an output, which drives it low, then sets the level
 * with SIO GPIO_OUT_SET / GPIO_OUT_CLR.
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
 * Makes the pin an output, driven low, if it is not one, then flips it with
 * an atomic SIO GPIO_OUT_XOR write.
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

/*
 * Per-pin GPIO helpers for the board LED macros.  They take the absolute
 * RP2350 pin number (0..29), bypass the virtual-port layer and ignore pins
 * above 29.  They must not be used on a pin the port-based API also drives.
 */

/**
 * @brief Configure an absolute GPIO pin as a push-pull output.
 *
 * Sets the IO_BANK0 function to SIO (function 5), enables the output driver
 * and drives the pin low.  Repeated calls leave the same state.
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
