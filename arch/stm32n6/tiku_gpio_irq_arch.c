/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - STM32N6 GPIO edge interrupts.
 *
 * The EXTI routing is not programmed yet, so requests are refused rather than
 * accepted into a line that would never fire.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/gpio/tiku_gpio.h>

/**
 * @brief Request an edge interrupt on a pin.
 *
 * @param port  Port index
 * @param pin   Pin number
 * @param edge  Edge selection
 * @return -1: no EXTI backend on this port yet
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, uint8_t edge) {
    (void)port; (void)pin; (void)edge;
    return -1;
}

/**
 * @brief Stop an edge interrupt on a pin.
 *
 * @param port  Port index
 * @param pin   Pin number
 * @return 0, since nothing was ever armed
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    (void)port; (void)pin;
    return 0;
}
