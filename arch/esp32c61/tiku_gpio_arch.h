/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - ESP32-C61 GPIO, and the board's addressable RGB LED.
 *
 * One bank, GPIO0..GPIO29, maps to the kernel's ports 1..4 in groups of eight.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_GPIO_ARCH_H_
#define TIKU_ESP32C61_GPIO_ARCH_H_

#include <stdint.h>

/** @brief Make @p pin a plain GPIO output, driven low. */
void tiku_esp32c61_gpio_init_output(uint8_t pin);

/** @brief Drive @p pin high (@p value non-zero) or low. */
void tiku_esp32c61_gpio_set(uint8_t pin, uint8_t value);

/** @brief Invert the level @p pin is driven to. */
void tiku_esp32c61_gpio_toggle(uint8_t pin);

/**
 * @brief Show one colour on a WS2812-class LED on @p pin.
 *
 * Bit times come from the measured core clock, with interrupts held off
 * for the 30 us the 24 bits take.
 */
void tiku_esp32c61_rgb_set(uint8_t pin, uint8_t r, uint8_t g, uint8_t b);

/** @brief One colour of that LED as a lamp: 1 on, 0 off, -1 toggles.
 *  @param channel  0 red, 1 green, 2 blue */
void tiku_esp32c61_led_set(uint8_t pin, uint8_t channel, int on);

/* Kernel-facing GPIO contract, shared with the other ports: 1-based ports of
 * eight pins over the one bank.  All return -1 for a pin the part lacks. */
/** @brief The GPIO number a kernel (port, pin) names, or -1. */
int    tiku_esp32c61_gpio_num(uint8_t port, uint8_t pin);

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);
int    tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);

#endif /* TIKU_ESP32C61_GPIO_ARCH_H_ */
