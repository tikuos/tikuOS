/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - Read-only P4 GPIO; board pin ownership is unverified.
 *
 * GPIO0..54 appear as seven virtual eight-pin ports numbered 1..7.  Level
 * and direction reads work; every call that would change a pad returns -1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_GPIO_ARCH_H_
#define TIKU_P4_GPIO_ARCH_H_
#include <stdint.h>
/** @brief Refuse output configuration; board pin ownership is unverified. */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
/** @brief Refuse input reconfiguration; it can disconnect board peripherals. */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
/** @brief Refuse writes without changing pad routing, direction or level. */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value);
/** @brief Refuse toggling a board pin. */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
/** @brief Read GPIO0..54 in seven virtual eight-pin banks; -1 if invalid. */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
/** @brief Read the GPIO output-enable latch, without changing it. */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);
/** @brief Return 1 for reserved pads, -1 for invalid pins. */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);
#endif
