/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.h - RA8P1 GPIO: direction, level and toggle.
 *
 * Ports are numbered the way the manual and the schematic name them: PORT0..
 * PORT9 are 0..9 and PORTA..PORTD are 0xA..0xD, so P600 is port 6 pin 0 and
 * PA07 is port 0xA pin 7.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_GPIO_ARCH_H_
#define TIKU_RA8P1_GPIO_ARCH_H_

#include <stdint.h>

/**
 * @brief Drive a pin as a push-pull output, starting low.
 *
 * @param port  Port index, 0..9 then 0xA..0xD for PORTA..PORTD
 * @param pin   Pin number within the port, 0..15
 */
void tiku_ra8p1_gpio_init_output(uint8_t port, uint8_t pin);

/**
 * @brief Configure a pin as a general-purpose input.
 *
 * @param port  Port index, 0..9 then 0xA..0xD
 * @param pin   Pin number within the port, 0..15
 */
void tiku_ra8p1_gpio_init_input(uint8_t port, uint8_t pin);

/**
 * @brief Point a pin at a peripheral function.
 *
 * @param port  Port index, 0..9 then 0xA..0xD
 * @param pin   Pin number within the port, 0..15
 * @param psel  Peripheral select code from the manual's port tables, 0..31
 */
void tiku_ra8p1_gpio_init_peripheral(uint8_t port, uint8_t pin, uint8_t psel);

/**
 * @brief Set or clear an output pin.
 *
 * @param port   Port index, 0..9 then 0xA..0xD
 * @param pin    Pin number within the port, 0..15
 * @param value  Non-zero drives high
 */
void tiku_ra8p1_gpio_set(uint8_t port, uint8_t pin, uint8_t value);

/**
 * @brief Invert an output pin.
 *
 * @param port  Port index, 0..9 then 0xA..0xD
 * @param pin   Pin number within the port, 0..15
 */
void tiku_ra8p1_gpio_toggle(uint8_t port, uint8_t pin);

/* Kernel-facing GPIO contract, shared with the other ports.  Every call
 * returns -1 for a port or pin out of range. */

/** @brief Make a pin a push-pull output, driving low. @return 0 or -1 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);

/** @brief Make a pin a general-purpose input. @return 0 or -1 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);

/** @brief Drive a pin; an input pin becomes an output. @return 0 or -1 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);

/**
 * @brief Invert a pin's output level; an input pin becomes an output.
 * @return 0 or -1
 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);

/** @brief Read a pin's level from PIDR. @return 0 or 1, or -1 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);

/** @brief Read a pin's direction. @return 1 output, 0 input, or -1 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

#endif /* TIKU_RA8P1_GPIO_ARCH_H_ */
