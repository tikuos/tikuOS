/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - RA8P1 wake-source reporting.
 *
 * Reports the wake sources armed in hardware: SysTick from its control
 * register, the ICU-linked peripherals from their NVIC enable bits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <hal/tiku_wake_hal.h>

#include "tiku_ra8p1_regs.h"

/**
 * @brief Non-zero when NVIC line @p irqn is unmasked.
 *
 * On this part NVIC line n is ICU slot n, the slot an event is linked to.
 */
static uint32_t nvic_line_armed(uint32_t irqn)
{
    return TIKU_REG32(RA8P1_NVIC_ISER(irqn >> 5)) & (1UL << (irqn & 0x1FU));
}

/**
 * @brief Report which sources could wake the part.
 *
 * @param out  Receives the source set; untouched when NULL
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    uint32_t csr;
    unsigned i;

    if (out == NULL) {
        return;
    }
    out->sources = 0U;
    for (i = 0; i < TIKU_WAKE_MAX_GPIO_PORTS; i++) {
        out->gpio_ie[i] = 0U;
    }

    /* SysTick is a core exception with no ICU link and no NVIC line.  It is a
     * wake source only with both ENABLE and TICKINT set: ENABLE alone counts
     * but wakes nothing. */
    csr = TIKU_REG32(RA8P1_SYST_CSR);
    if ((csr & (RA8P1_SYST_CSR_ENABLE | RA8P1_SYST_CSR_TICKINT)) ==
        (RA8P1_SYST_CSR_ENABLE | RA8P1_SYST_CSR_TICKINT)) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }

    if (nvic_line_armed(RA8P1_ICU_SLOT_UART_RXI)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }

    /* The htimer slot stays masked until an alarm is armed, so the bit
     * shows a pending alarm. */
    if (nvic_line_armed(RA8P1_ICU_SLOT_HTIMER)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }

    /*
     * TIKU_WAKE_WDT and TIKU_WAKE_GPIO are never reported: the IWDT is set to
     * reset the part, not to raise an interrupt, and
     * tiku_gpio_irq_arch_enable() returns TIKU_GPIO_IRQ_ERR_UNSUP on this
     * port, so no pin can be armed.
     */
}
