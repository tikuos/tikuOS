/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - Apollo510 GPIO edge interrupts (stub)
 *
 * This port has no GPIO edge-interrupt driver: both calls return -1
 * (TIKU_GPIO_IRQ_ERR_INVALID) for every pin and change no register.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>

/**
 * @brief Stub: enable a GPIO edge interrupt; always fails
 *
 * @param port  GPIO port index
 * @param pin   GPIO pin index within the port
 * @param edge  Edge polarity (rising, falling, or both)
 * @return -1 always
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge) {
    (void)port;
    (void)pin;
    (void)edge;
    return -1;
}

/**
 * @brief Stub: disable a GPIO edge interrupt; always fails
 *
 * @param port  GPIO port index
 * @param pin   GPIO pin index within the port
 * @return -1 always
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    (void)port;
    (void)pin;
    return -1;
}
