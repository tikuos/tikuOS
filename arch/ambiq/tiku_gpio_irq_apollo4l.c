/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_apollo4l.c - Apollo4 Lite GPIO edge interrupts (stub)
 *
 * Mirrors arch/ambiq/tiku_gpio_irq_arch.c. Not yet supported; a real backend
 * configures the GPIO0 interrupt block (IRQ 56, pins 0-31) and posts
 * TIKU_EVENT_GPIO into the scheduler. The weak tiku_ambiq_gpio0_isr in the crt
 * stays at the default handler until that lands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>

/** @brief Enable a GPIO edge interrupt (stub -- returns -1, not supported). */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge) {
    (void)port;
    (void)pin;
    (void)edge;
    return -1;
}

/** @brief Disable a GPIO edge interrupt (stub -- returns -1). */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    (void)port;
    (void)pin;
    return -1;
}
