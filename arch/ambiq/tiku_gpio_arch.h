/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - Ambiq GPIO access (Apollo510 and Apollo4 Lite).
 *
 * Two layers: the (port, pin) API shared with the other ports, which maps port
 * N pin P to pad (N-1)*8 + P, and raw-pad helpers that take a pad number, used
 * by drivers and the board LED macros.
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
 * Maps port N, pin P to pad (N-1)*8 + P.
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
 * @brief Configure a GPIO pin as a push-pull output and drive it.
 *
 * @param port  Virtual port number (1-based).
 * @param pin   Pin index within the port (0..7).
 * @param val   Output level: 0 = low, non-zero = high.
 * @return 0 on success, negative error code if the port/pin is invalid.
 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/**
 * @brief Configure a GPIO pin as a push-pull output and invert its level.
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
 * @brief Configure a pad as a push-pull GPIO output, input buffer on.
 *
 * @p pad is not range-checked.
 *
 * @param pad  GPIO pad number.
 */
void tiku_ambiq_gpio_init_output(uint32_t pad);

/**
 * @brief Write @p cfg to a pad's PINCFG register under the PADKEY lock.
 *
 * For drivers that give pads to a peripheral (MSPI, SDIO): the caller
 * composes the whole PINCFG value. Out-of-range pad numbers are ignored:
 * 224 or more on Apollo510, 128 or more on Apollo4.
 */
void tiku_ambiq_gpio_pad_config(uint32_t pad, uint32_t cfg);

/**
 * @brief Drive a pad to a logic level through the WTS / WTC registers.
 *
 * @param pad    GPIO pad number.
 * @param value  Output level: 0 = low, non-zero = high.
 */
void tiku_ambiq_gpio_set(uint32_t pad, uint8_t value);

/**
 * @brief Invert a pad's output level by read-modify-write of its WT bank.
 *
 * @param pad  GPIO pad number.
 * @note Not atomic: a write to the same 32-pad bank between the read and the
 *       write is lost.
 */
void tiku_ambiq_gpio_toggle(uint32_t pad);

#endif /* TIKU_AMBIQ_GPIO_ARCH_H_ */
