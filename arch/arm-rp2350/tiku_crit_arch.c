/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - RP2350 IRQ-mask backend for tiku_crit
 *
 * The Cortex-M NVIC has per-source enable/disable registers and no
 * MSP430-style "IE family" abstraction. We implement
 * tiku_crit_arch_mask_irqs() by snapshotting the NVIC ISER0 register
 * (covers IRQs 0..31, which is more than the RP2350 actually
 * exposes), masking everything not in the preserve set, and then
 * restoring on unmask.
 *
 * The tick / htimer / UART IRQs each have a single NVIC line so the
 * preserve mapping is straightforward; the GPIO bank is a single
 * source for all 30+ pins so we either keep them all or kill them
 * all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include "tiku_rp2350_regs.h"
#include <stdint.h>

/* Per-source preserve mask: list which NVIC bits stay enabled when
 * the matching TIKU_CRIT_PRESERVE_* flag is set. */
#define BIT(n) (1U << (n))

#define IRQ_BIT_TICK    0U   /* SysTick is a system exception, not NVIC,
                                so masking it requires touching the SCB
                                instead — for the first port we just
                                leave the system tick alone. */
#define IRQ_BIT_HTIMER  BIT(RP2350_IRQ_TIMER0_0)
#define IRQ_BIT_UART    BIT(RP2350_IRQ_UART0)
#define IRQ_BIT_GPIO    BIT(RP2350_IRQ_IO_BANK0)
#define IRQ_BIT_PIO     BIT(RP2350_IRQ_PIO0_0)

static struct {
    uint32_t iser0_saved;
} crit_state;

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask) {
    /* Snapshot what's enabled today. */
    crit_state.iser0_saved = *(volatile uint32_t *)RP2350_NVIC_ISER0;

    /* Compute the keep-mask: bits that should remain enabled. */
    uint32_t keep = 0U;
    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) keep |= IRQ_BIT_HTIMER;
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART)   keep |= IRQ_BIT_UART;
    if (preserve_mask & TIKU_CRIT_PRESERVE_GPIO)   keep |= IRQ_BIT_GPIO;
    if (preserve_mask & TIKU_CRIT_PRESERVE_PIO)    keep |= IRQ_BIT_PIO;
    /* TICK / I2C / ADC / WDT not represented yet — kept across the
     * window unconditionally because the SysTick is not in the
     * NVIC and the I2C/ADC drivers are stubs. */

    uint32_t to_mask = crit_state.iser0_saved & ~keep;
    if (to_mask != 0U) {
        *(volatile uint32_t *)RP2350_NVIC_ICER0 = to_mask;
        /* DSB+ISB so the NVIC disable takes architectural effect
         * before any caller code runs. Without this the NVIC write
         * may not be visible to the CPU's interrupt-acceptance
         * logic until the next memory barrier or pipeline flush. */
        __asm__ volatile ("dsb" ::: "memory");
        __asm__ volatile ("isb" ::: "memory");
    }
}

void tiku_crit_arch_unmask_irqs(void) {
    *(volatile uint32_t *)RP2350_NVIC_ISER0 = crit_state.iser0_saved;
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}
