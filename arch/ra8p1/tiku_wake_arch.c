/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - RA8P1 wake-source reporting.
 *
 * Reads what is actually armed rather than what the port intends: SysTick from
 * its own control register, the ICU-linked peripherals from their NVIC lines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <hal/tiku_wake_hal.h>

#include "tiku_ra8p1_regs.h"

/**
 * @brief Non-zero when NVIC line @p irqn is unmasked.
 *
 * On this part the line number IS the ICU slot number: the ICU links an event
 * onto slot n, and slot n is NVIC line n.
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

    /* SysTick is a core exception, so it never passes through the ICU and has
     * no NVIC line to test.  It counts only when running AND allowed to
     * interrupt: ENABLE without TICKINT counts but wakes nothing. */
    csr = TIKU_REG32(RA8P1_SYST_CSR);
    if ((csr & (RA8P1_SYST_CSR_ENABLE | RA8P1_SYST_CSR_TICKINT)) ==
        (RA8P1_SYST_CSR_ENABLE | RA8P1_SYST_CSR_TICKINT)) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }

    if (nvic_line_armed(RA8P1_ICU_SLOT_UART_RXI)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }

    /* The htimer slot stays masked until an alarm is armed, so this reports
     * the live state rather than "the driver exists". */
    if (nvic_line_armed(RA8P1_ICU_SLOT_HTIMER)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }

    /*
     * No TIKU_WAKE_WDT and no TIKU_WAKE_GPIO, and both are honest absences.
     * The IWDT is configured to reset rather than to raise an interval
     * interrupt, so it restarts the part instead of waking it; and
     * tiku_gpio_irq_arch_enable() still returns UNSUP here, so no pin can be
     * armed at all.  Reporting either would name a source the hardware would
     * not honour.
     */
}
