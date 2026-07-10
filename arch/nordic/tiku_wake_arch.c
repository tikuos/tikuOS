/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - nRF54L backend for the wake-source HAL
 *
 * Reports which wake sources are armed.  At this stage the kernel tick runs on
 * TIMER10 (NVIC IRQ 134) -- mapped to the generic TIKU_WAKE_SYSTICK bit -- and
 * the console UART is polled, so no UART/GPIO/htimer wake IRQs are armed yet.
 * Mirrors arch/arm-rp2350/tiku_wake_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_wake_hal.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <string.h>

#define TIKU_NORDIC_TIMER10_IRQN  134

void tiku_wake_arch_query(tiku_wake_sources_t *out)
{
    uint32_t irqn;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    /* The kernel tick lives on TIMER10 here (not the Cortex SysTick); report
     * it under the generic system-tick wake bit when its NVIC line is armed. */
    irqn = (uint32_t)TIKU_NORDIC_TIMER10_IRQN;
    if (TIKU_NVIC->ISER[irqn >> 5] & (1UL << (irqn & 0x1Fu))) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }

    /* UART is polled and the htimer/GPIO IRQs are stubbed, so no other wake
     * sources are armed yet.  gpio_ie[] stays zeroed (memset above). */
}
