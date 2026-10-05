/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - RA8P1 pin interrupts: a stub.
 *
 * Both calls return TIKU_GPIO_IRQ_ERR_UNSUP.  On this part a pin raises an
 * interrupt only through one of the ICU's IRQn inputs (PmnPFS.ISEL, IRQCRn).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>

/* No pin interrupt is armed: both calls return TIKU_GPIO_IRQ_ERR_UNSUP. */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge)
{
    (void)port;
    (void)pin;
    (void)edge;
    return TIKU_GPIO_IRQ_ERR_UNSUP;
}

int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    (void)port;
    (void)pin;
    return TIKU_GPIO_IRQ_ERR_UNSUP;
}
