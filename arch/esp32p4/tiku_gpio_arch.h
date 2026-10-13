/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_gpio_arch.h - Read-only P4 GPIO until the board revision is selected.
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
