/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - STM32N6 GPIO: port/pin direction, level and toggle.
 *
 * Ports are numbered A=0 through Q=16, matching the 0x400 register stride;
 * the part has A..H and N..Q.  The configuration calls enable a port's clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_GPIO_ARCH_H_
#define TIKU_STM32N6_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Enable the peripheral clock for one GPIO port.
 *
 * Enabling a running port again has no effect; an invalid port is ignored.
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
 * @brief Point a pin at one of the sixteen alternate functions, push-pull at
 *        high speed.
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

/* Kernel-facing GPIO contract.  Each call returns -1 for a port outside
 * A..H and N..Q or a pin above 15. */

/** @brief Make a pin a push-pull output, driven low. @return 0 or -1 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/** @brief Make a pin an input; its pull setting is kept. @return 0 or -1 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/** @brief Drive a pin to @p val as a push-pull output. @return 0 or -1 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/** @brief Invert an output pin. @return 0 or -1 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);

/** @brief Read a pin's input level. @return 0 or 1, or -1 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);

/** @brief Report a pin's mode. @return 1 for output, 0 for any other, or -1 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

#endif /* TIKU_STM32N6_GPIO_ARCH_H_ */
