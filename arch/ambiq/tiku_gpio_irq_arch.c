/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - Apollo510 GPIO edge interrupts (stub)
 *
 * This port has no GPIO edge-interrupt driver: both calls return
 * TIKU_GPIO_IRQ_ERR_UNSUP and change no register.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>

int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge) {
    (void)port;
    (void)pin;
    (void)edge;
    /* Apollo510 has no GPIO edge-interrupt driver; no register is written. */
    return TIKU_GPIO_IRQ_ERR_UNSUP;
}

int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    (void)port;
    (void)pin;
    /* Apollo510 has no GPIO edge-interrupt driver; no register is written. */
    return TIKU_GPIO_IRQ_ERR_UNSUP;
}
