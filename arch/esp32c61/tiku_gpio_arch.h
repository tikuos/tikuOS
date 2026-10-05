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

/**
 * @brief Switch one colour channel of the LED on @p pin.
 *
 * @param channel  0 red, 1 green, 2 blue
 * @param on       1 lights it, 0 clears it, -1 toggles it
 */
void tiku_esp32c61_led_set(uint8_t pin, uint8_t channel, int on);

/* Kernel-facing GPIO contract: 1-based ports of eight pins over the one
 * bank.  Each call returns -1 for a pin the part lacks. */
/** @brief The GPIO number a kernel (port, pin) names, or -1. */
int    tiku_esp32c61_gpio_num(uint8_t port, uint8_t pin);

/** @brief Make the pin a GPIO output, driven low. @return 0, or -1 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/** @brief Make the pin a GPIO input, its output driver off. @return 0, or -1 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/** @brief Drive the pin to @p val, first making it an output if it is not
 *         one.  @return 0, or -1 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/** @brief Invert the pin's driven level, first making it an output if it is
 *         not one.  @return 0, or -1 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);

/** @brief The level on the pad. @return 1 high, 0 low, or -1 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);

/** @brief The pin's direction. @return 1 output, 0 input, or -1 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

/** @brief Whether a peripheral holds the pin. @return 1 a peripheral, 0 GPIO,
 *         or -1 */
int    tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);

#endif /* TIKU_ESP32C61_GPIO_ARCH_H_ */
