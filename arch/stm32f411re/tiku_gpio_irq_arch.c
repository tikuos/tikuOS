/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_gpio_irq_arch.c - STM32F411RE GPIO interrupt stubs
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>
#include <stdint.h>

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
