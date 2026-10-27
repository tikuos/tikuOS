/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - C6 GPIO bank interface with protected SDIO and boot pins.
 *
 * Pins are GPIO0..30, addressed as one-based ports of eight; the board's
 * reserved pads are refused.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C6_GPIO_ARCH_H_
#define TIKU_ESP32C6_GPIO_ARCH_H_
#include <stdint.h>
/** @brief Configure a free pin as output; return -1 for invalid or reserved
 * pins. */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
/** @brief Disable the output driver and enable input on a free pin. */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
/** @brief Set a free pin's level before enabling its output driver. */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value);
/** @brief Toggle a free pin's output latch. */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
/** @brief Read a pad level; return -1 outside GPIO0..30. */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
/** @brief Read the output-enable bit; return -1 outside GPIO0..30. */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);
/** @brief Return 1 for a reserved/routed pad, 0 for GPIO, or -1 for invalid
 * pins. */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);
/** @brief Return one bit per GPIO with an armed edge interrupt. */
uint32_t tiku_c6_gpio_irq_armed(void);
#endif
