/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - Apollo 510 GPIO access
 *
 * Two layers:
 *   1. The (port,pin)-indexed API the VFS/shell share with MSP430 and
 *      RP2350 (/dev/gpio/{1..N}/{0..7}). Apollo510 has >200 pads, far
 *      more than the 8-pin-per-port VFS view exposes; the low pads map
 *      port N pin p -> pad (N-1)*8 + p.
 *   2. Raw-pad helpers (tiku_ambiq_gpio_*) used by the board LED macros,
 *      because the EVB LEDs (165/89/92) sit well above the VFS range.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_GPIO_ARCH_H_
#define TIKU_AMBIQ_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Configure a GPIO pin as a push-pull output.
 *
 * Part of the shared (port, pin) API used by the VFS /dev/gpio nodes.
 * Maps port N, pin P to Apollo510 pad (N-1)*8 + P.
 *
 * @param port  Virtual port number (1-based, matching /dev/gpio/N).
 * @param pin   Pin index within the port (0..7).
 * @return 0 on success, negative error code if the port/pin is invalid.
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/**
 * @brief Configure a GPIO pin as a high-impedance input.
 *
 * @param port  Virtual port number (1-based).
 * @param pin   Pin index within the port (0..7).
 * @return 0 on success, negative error code if the port/pin is invalid.
 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/**
 * @brief Write a logic level to an output-configured GPIO pin.
 *
 * @param port  Virtual port number (1-based).
 * @param pin   Pin index within the port (0..7).
 * @param val   Output level: 0 = low, non-zero = high.
 * @return 0 on success, negative error code if the port/pin is invalid.
 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/**
 * @brief Toggle the output level of a GPIO pin.
 *
 * @param port  Virtual port number (1-based).
 * @param pin   Pin index within the port (0..7).
 * @return 0 on success, negative error code if the port/pin is invalid.
 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);

/**
 * @brief Read the current level of a GPIO pin.
 *
 * @param port  Virtual port number (1-based).
 * @param pin   Pin index within the port (0..7).
 * @return 0 or 1 reflecting the pad level, negative error code on error.
 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);

/**
 * @brief Read the current direction of a GPIO pin.
 *
 * @param port  Virtual port number (1-based).
 * @param pin   Pin index within the port (0..7).
 * @return 1 if the pin is an output, 0 if input, negative on error.
 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

/**
 * @brief Configure a raw Apollo510 pad as a push-pull output.
 *
 * Operates on the full pad number space (0..AM_HAL_GPIO_MAX_PADS-1).
 * Used by the board LED macros for high-numbered EVB pads (165, 89, 92)
 * that lie outside the 8-pin-per-port VFS window.
 *
 * @param pad  Apollo510 GPIO pad number.
 */
void tiku_ambiq_gpio_init_output(uint32_t pad);

/**
 * @brief Drive a raw Apollo510 pad to a logic level.
 *
 * @param pad    Apollo510 GPIO pad number.
 * @param value  Output level: 0 = low, non-zero = high.
 */
void tiku_ambiq_gpio_set(uint32_t pad, uint8_t value);

/**
 * @brief Toggle a raw Apollo510 pad output level.
 *
 * @param pad  Apollo510 GPIO pad number.
 */
void tiku_ambiq_gpio_toggle(uint32_t pad);

#endif /* TIKU_AMBIQ_GPIO_ARCH_H_ */
