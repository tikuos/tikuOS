/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_pinmux_arch.h - STM32F411RE pinmux and pad helpers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_PINMUX_ARCH_H_
#define TIKU_STM32F411_PINMUX_ARCH_H_

#include <stdint.h>

/**
 * Resolve the GPIO base address and RCC enable bit for a given STM32F411 port/pin.
 *
 * @param port    GPIO port index (A=0, B=1, ...).
 * @param pin     GPIO pin number (0-15).
 * @param gpio_base[out] Pointer that receives the GPIO register base address.
 * @param rcc_bit[out]   Pointer that receives the RCC peripheral enable bit.
 *
 * @return 0 on success, non-zero on failure.
 */
int tiku_stm32f411_pinmux_resolve(uint8_t port, uint8_t pin,
                                  uint32_t *gpio_base, uint32_t *rcc_bit);

/**
 * Configure an STM32F411 GPIO pin with mode, pull-up/pull-down, speed, and AF.
 *
 * @param port  GPIO port index (A=0, B=1, ...).
 * @param pin   GPIO pin number (0-15).
 * @param mode  GPIO mode value.
 * @param pupd  Pull-up/pull-down configuration.
 * @param speed Output speed configuration.
 * @param af    Alternate function number (0-15).
 *
 * @return 0 on success, non-zero on failure.
 */
int tiku_stm32f411_pinmux_config(uint8_t port, uint8_t pin,
                                 uint32_t mode, uint32_t pupd,
                                 uint32_t speed, uint8_t af);

/**
 * Initialize a GPIO pin as a push-pull output on STM32F411.
 *
 * @param port GPIO port index (A=0, B=1, ...).
 * @param pin  GPIO pin number (0-15).
 *
 * @return 0 on success, non-zero on failure.
 */
int tiku_stm32f411_pinmux_init_output(uint8_t port, uint8_t pin);

/**
 * Initialize a GPIO pin as an input on STM32F411 with optional pull-up/pull-down.
 *
 * @param port GPIO port index (A=0, B=1, ...).
 * @param pin  GPIO pin number (0-15).
 * @param pupd Pull-up/pull-down configuration.
 *
 * @return 0 on success, non-zero on failure.
 */
int tiku_stm32f411_pinmux_init_input(uint8_t port, uint8_t pin,
                                     uint32_t pupd);

#endif /* TIKU_STM32F411_PINMUX_ARCH_H_ */
