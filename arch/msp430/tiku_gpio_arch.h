/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - MSP430 GPIO port access
 *
 * Provides runtime access to any GPIO port/pin by number.
 * Ports are 1-based (P1..P4, PJ on FR5969).  Pin is 0-7.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_ARCH_H_
#define TIKU_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Set a pin as digital output.
 * @return 0 on success, -1 if port/pin invalid
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/**
 * @brief Set a pin as digital input with pull-up resistor.
 * @return 0 on success, -1 if port/pin invalid
 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/**
 * @brief Write a pin value (0 or 1).  Sets direction to output.
 * @return 0 on success, -1 if port/pin invalid
 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/**
 * @brief Toggle a pin.  Sets direction to output if not already.
 * @return 0 on success, -1 if port/pin invalid
 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);

/**
 * @brief Read a pin's current input value.
 * @return 0 or 1 on success, -1 if port/pin invalid
 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);

/**
 * @brief Read the direction of a pin.
 * @return 1 = output, 0 = input, -1 if invalid
 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

#endif /* TIKU_GPIO_ARCH_H_ */
