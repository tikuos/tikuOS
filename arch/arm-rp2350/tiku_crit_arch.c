/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - RP2350 IRQ-mask backend for tiku_crit.
 *
 * Masking snapshots NVIC ISER0, disables every enabled IRQ outside the preserve
 * set and later writes the snapshot back.  The GPIO bank has one IRQ for all
 * its pins, so it is kept or masked whole.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include "tiku_rp2350_regs.h"
#include <stdint.h>

/**
 * @defgroup rp2350_crit_bits RP2350 NVIC bit-position aliases for crit section
 * @brief Maps TikuOS TIKU_CRIT_PRESERVE_* flags to NVIC ISER0 bit masks.
 *
 * SysTick is a system exception, not an NVIC IRQ, so IRQ_BIT_TICK is 0 and
 * no mask covers it.  The other sources are NVIC IRQ lines.
 */
/** @brief Helper to form a single-bit mask. */
#define BIT(n) (1U << (n))

#define IRQ_BIT_TICK    0U   /* SysTick is not an NVIC IRQ: no bit */
#define IRQ_BIT_HTIMER  BIT(RP2350_IRQ_TIMER0_0)
#define IRQ_BIT_UART    BIT(RP2350_IRQ_UART0)   /* IRQ 33: not in ISER0 */
#define IRQ_BIT_GPIO    BIT(RP2350_IRQ_IO_BANK0)
#define IRQ_BIT_PIO     BIT(RP2350_IRQ_PIO0_0)

/**
 * @brief Critical-section state saved across mask/unmask pairs.
 *
 * Only ISER0, IRQs 0..31, is saved and masked.  IRQs 32 and above, UART0
 * (33) among them, are outside this register.
 */
static struct {
    uint32_t iser0_saved; /**< NVIC ISER0 snapshot taken at mask time */
} crit_state;

/**
 * @brief Mask NVIC IRQs, keeping only those listed in @p preserve_mask.
 *
 * Snapshots NVIC ISER0, builds a keep-set from the TIKU_CRIT_PRESERVE_* bits
 * and writes the difference to ICER0.  A DSB+ISB pair makes the disable
 * architecturally visible before the critical section body runs.
 *
 * @note SysTick and IRQs 32 and above (UART0, ADC, I2C) stay enabled.
 * @param preserve_mask  OR of TIKU_CRIT_PRESERVE_* flags for sources to
 *                       keep enabled (HTIMER, UART, GPIO, PIO)
 */
void tiku_crit_arch_mask_irqs(uint8_t preserve_mask) {
    /* Snapshot the enabled IRQs. */
    crit_state.iser0_saved = *(volatile uint32_t *)RP2350_NVIC_ISER0;

    /* Compute the keep-mask: bits that should remain enabled. */
    uint32_t keep = 0U;
    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) keep |= IRQ_BIT_HTIMER;
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART)   keep |= IRQ_BIT_UART;
    if (preserve_mask & TIKU_CRIT_PRESERVE_GPIO)   keep |= IRQ_BIT_GPIO;
    if (preserve_mask & TIKU_CRIT_PRESERVE_PIO)    keep |= IRQ_BIT_PIO;
    /* TICK, I2C, ADC and WDT have no mask here: SysTick is not an NVIC
     * IRQ, the I2C and ADC IRQs are above 31 and the watchdog raises
     * none, so they stay enabled whatever preserve_mask says. */

    uint32_t to_mask = crit_state.iser0_saved & ~keep;
    if (to_mask != 0U) {
        *(volatile uint32_t *)RP2350_NVIC_ICER0 = to_mask;
        /* DSB+ISB so the NVIC disable takes effect before any caller
         * code runs; without them the CPU can still take a masked IRQ
         * until the next barrier or pipeline flush. */
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
