/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.h - STM32N6 EXTI edge interrupts.
 *
 * Declares the per-line count of handler runs; the HAL entry points are in
 * hal/tiku_gpio_irq_hal.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_GPIO_IRQ_ARCH_H_
#define TIKU_STM32N6_GPIO_IRQ_ARCH_H_

#include <stdint.h>

/**
 * @brief How many times a line's handler has run since boot.
 *
 * @param line  EXTI line, which is the pin number
 * @return Delivery count, or 0 for an out-of-range line
 */
uint32_t tiku_stm32n6_exti_hits(uint8_t line);

#endif /* TIKU_STM32N6_GPIO_IRQ_ARCH_H_ */
