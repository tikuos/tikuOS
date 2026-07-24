/*
 * Tiku Operating System v0.06
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

/**
 * @defgroup rp2350_crit_bits RP2350 NVIC bit-position aliases for crit section
 * @brief Maps TikuOS TIKU_CRIT_PRESERVE_* flags to NVIC ISER0 bit positions.
 *
 * SysTick is a system exception (not an NVIC source) so it is left alone;
 * its bit position is defined as 0 (no NVIC bit) to avoid accidental
 * disable. All other sources are direct NVIC IRQ lines.
 */
/** @brief Helper to form a single-bit mask. */
#define BIT(n) (1U << (n))

#define IRQ_BIT_TICK    0U   /* SysTick is a system exception, not NVIC,
                                so masking it requires touching the SCB
                                instead — for the first port we just
                                leave the system tick alone. */
#define IRQ_BIT_HTIMER  BIT(RP2350_IRQ_TIMER0_0)
#define IRQ_BIT_UART    BIT(RP2350_IRQ_UART0)
#define IRQ_BIT_GPIO    BIT(RP2350_IRQ_IO_BANK0)
#define IRQ_BIT_PIO     BIT(RP2350_IRQ_PIO0_0)

/**
 * @brief Critical-section state saved across mask/unmask pairs.
 *
 * Only ISER0 (IRQs 0..31) is saved; RP2350 uses far fewer than 32
 * external IRQs so one register is sufficient.
 */
static struct {
    uint32_t iser0_saved; /**< NVIC ISER0 snapshot taken at mask time */
} crit_state;

/**
 * @brief Mask NVIC IRQs, keeping only those listed in @p preserve_mask.
 *
 * Snapshots NVIC ISER0, builds a keep-set from the TIKU_CRIT_PRESERVE_*
 * bits, and writes the difference to NVIC ICER0. A DSB+ISB pair ensures
 * the disable is architecturally visible before the critical section body
 * runs. SysTick is not in the NVIC so it is always left enabled.
 *
 * @param preserve_mask  OR of TIKU_CRIT_PRESERVE_* flags for sources to
 *                       keep enabled (HTIMER, UART, GPIO, PIO)
 */
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

/**
 * @brief Restore NVIC IRQs to the state saved by tiku_crit_arch_mask_irqs().
 *
 * Writes the saved ISER0 back to NVIC ISER0, then issues DSB+ISB to
 * ensure the re-enable is architecturally visible before any subsequent
 * code runs.
 */
void tiku_crit_arch_unmask_irqs(void) {
    *(volatile uint32_t *)RP2350_NVIC_ISER0 = crit_state.iser0_saved;
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}
