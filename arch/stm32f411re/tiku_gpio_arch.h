/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_gpio_arch.h - STM32F411RE GPIO port access
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_GPIO_ARCH_H_
#define TIKU_STM32F411_GPIO_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Board-facing GPIO helpers                                                 */
/*---------------------------------------------------------------------------*/

/*
 * These helpers let board headers wire LEDs and similar fixed pins
 * directly to the STM32 GPIO backend without pulling in the generic
 * GPIO wrapper layer. They operate on the same numbered port/pin ABI
 * as the platform-agnostic GPIO interface.
 */
void tiku_stm32f411_gpio_init_output(uint8_t port, uint8_t pin);
void tiku_stm32f411_gpio_set(uint8_t port, uint8_t pin, uint8_t value);
void tiku_stm32f411_gpio_toggle(uint8_t port, uint8_t pin);

/*---------------------------------------------------------------------------*/
/* Platform-agnostic GPIO HAL entry points                                   */
/*---------------------------------------------------------------------------*/

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val);
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin);
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin);

#endif /* TIKU_STM32F411_GPIO_ARCH_H_ */
