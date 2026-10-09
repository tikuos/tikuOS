/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_gpio_arch.h - C5 GPIO bank interface with protected board pins.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_GPIO_ARCH_H_
#define TIKU_ESP32C5_GPIO_ARCH_H_
#include <stdint.h>
/** @brief Configure a free pin as output; return -1 for invalid or reserved pins. */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
/** @brief Disable the output driver and enable input on a free pin. */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
/** @brief Set a free pin's level before enabling its output driver. */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value);
/** @brief Toggle a free pin's output latch. */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
/** @brief Read a pad level; return -1 outside GPIO0..28. */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
/** @brief Read the output-enable bit; return -1 outside GPIO0..28. */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);
/** @brief Return 1 for a reserved/routed pad, 0 for GPIO, or -1 for invalid pins. */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);
/** @brief Make the board's RGB LED pad a low GPIO output. */
void tiku_c5_led_init(void);
/**
 * @brief Switch one colour of the board's WS2812-class RGB LED.
 * @param channel  0 red, 1 green, 2 blue
 * @param on       1 on, 0 off, -1 toggle; a lit channel shows 16 of 255
 * @note Masks interrupts for the 24-bit frame, about 30 us.
 */
void tiku_c5_led_set(uint8_t channel, int on);
#endif
