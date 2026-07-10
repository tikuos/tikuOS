/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - nRF54L GPIO edge interrupts (stub — not yet wired)
 *
 * Honest placeholder for the GPIOTE-backed per-pin edge IRQ path.
 * enable() reports TIKU_GPIO_IRQ_ERR_UNSUP (no vector wired on this
 * device yet) and disable() is a safe no-op. A real GPIOTE backend
 * plus an ISR that broadcasts TIKU_EVENT_GPIO is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>

/**
 * @brief Enable an edge-triggered interrupt on a pin (stub).
 *
 * No GPIOTE channel is wired yet, so there is no honest way to arm an
 * edge interrupt. Reports the "no IRQ vector on this device" code
 * instead of pretending to succeed.
 *
 * @param port  Virtual port number (ignored).
 * @param pin   Pin index within the port (ignored).
 * @param edge  Edge polarity selector (ignored).
 * @return TIKU_GPIO_IRQ_ERR_UNSUP always (not supported yet).
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge)
{
    (void)port;
    (void)pin;
    (void)edge;
    return TIKU_GPIO_IRQ_ERR_UNSUP;
}

/**
 * @brief Mask a pin's interrupt and clear any pending flag (stub — no-op).
 *
 * Nothing was ever armed (see enable()), so there is nothing to mask;
 * the request is a safe no-op and reported as success.
 *
 * @param port  Virtual port number (ignored).
 * @param pin   Pin index within the port (ignored).
 * @return TIKU_GPIO_IRQ_OK always (nothing to disable).
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    (void)port;
    (void)pin;
    return TIKU_GPIO_IRQ_OK;
}
