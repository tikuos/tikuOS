/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - RA8P1 pin interrupts, unimplemented.
 *
 * Only a fixed set of pins can raise an IRQ here, chosen through the ICU's
 * IRQCR registers and PmnPFS.ISEL rather than one line per pin number.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>

/*
 * UNSUP, not OK.  Wiring these to the ICU means routing a pin to one of the
 * IRQn inputs (only some pins can reach any given one), setting PmnPFS.ISEL,
 * choosing the edge in IRQCRn and enabling the NVIC line -- a table of which
 * pin reaches which IRQn, which is real work rather than a rename of the
 * STM32's per-pin EXTI.  Returning OK meanwhile would leave a caller waiting
 * on an event that can never arrive, which is the failure this refuses to
 * create.
 */
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
