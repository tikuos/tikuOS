/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - STM32N6 GPIO: port/pin direction, level and toggle.
 *
 * Ports are numbered A=0 through Q=16, matching the 0x400 register stride.
 * The clock for a port is enabled on first use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_GPIO_ARCH_H_
#define TIKU_STM32N6_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Enable the peripheral clock for one GPIO port.
 *
 * Idempotent, so callers need not track which ports are already running.
 *
 * @param port  Port index, A=0 .. Q=16
 */
void tiku_stm32n6_gpio_clock_enable(uint8_t port);

/**
 * @brief Drive a pin as a push-pull output, starting low.
 *
 * @param port  Port index, A=0 .. Q=16
 * @param pin   Pin number within the port, 0..15
 */
void tiku_stm32n6_gpio_init_output(uint8_t port, uint8_t pin);

/**
 * @brief Point a pin at one of the sixteen alternate functions.
 *
 * @param port  Port index, A=0 .. Q=16
 * @param pin   Pin number within the port, 0..15
 * @param af    Alternate function number, 0..15
 */
void tiku_stm32n6_gpio_init_alt(uint8_t port, uint8_t pin, uint8_t af);

/**
 * @brief Set or clear an output pin through BSRR.
 *
 * @param port   Port index, A=0 .. Q=16
 * @param pin    Pin number within the port, 0..15
 * @param value  Non-zero drives high
 */
void tiku_stm32n6_gpio_set(uint8_t port, uint8_t pin, uint8_t value);

/**
 * @brief Invert an output pin.
 *
 * @param port  Port index, A=0 .. Q=16
 * @param pin   Pin number within the port, 0..15
 */
void tiku_stm32n6_gpio_toggle(uint8_t port, uint8_t pin);

/* Kernel-facing GPIO contract, shared with the other ports. All return -1 when
 * the port or pin is out of range; the setters return 0 on success, read()
 * returns the pin level and get_dir() returns 1 for an output pin, 0 for any
 * other mode. */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

#endif /* TIKU_STM32N6_GPIO_ARCH_H_ */
