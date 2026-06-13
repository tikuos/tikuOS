/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - Apollo 510 GPIO edge interrupts (stub)
 *
 * Not yet supported. A real implementation configures am_hal_gpio
 * interrupts and posts TIKU_EVENT_GPIO into the scheduler (mirroring the
 * RP2350 "GPIO-edge notify" path).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>

/**
 * @brief Enable a GPIO edge interrupt on the specified pin (stub)
 *
 * Not yet implemented. A real backend configures am_hal_gpio interrupts
 * and posts TIKU_EVENT_GPIO into the scheduler, mirroring the RP2350
 * GPIO-edge notify path.
 *
 * @param port  GPIO port index
 * @param pin   GPIO pin index within the port
 * @param edge  Edge polarity (rising, falling, or both)
 * @return -1 always (not supported yet)
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge) {
    (void)port;
    (void)pin;
    (void)edge;
    return -1;   /* not supported yet */
}

/**
 * @brief Disable a GPIO edge interrupt on the specified pin (stub)
 *
 * Not yet implemented.
 *
 * @param port  GPIO port index
 * @param pin   GPIO pin index within the port
 * @return -1 always (not supported yet)
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    (void)port;
    (void)pin;
    return -1;
}
