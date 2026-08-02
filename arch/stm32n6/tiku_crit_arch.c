/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - STM32N6 critical sections over the NVIC.
 *
 * Snapshots the enabled-IRQ words, disables all but the sources the caller
 * asks to preserve, and restores the snapshot on exit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include <stdint.h>

#include "tiku_stm32n6_regs.h"

/* STM32N6 has more than 32 external IRQs -- LPTIM1 alone is 136 -- so the
 * snapshot spans five NVIC words rather than the single one a smaller part
 * needs. */
#define CRIT_NVIC_WORDS     5U

/**
 * @brief NVIC enable state saved across a mask/unmask pair.
 */
static struct {
    uint32_t iser[CRIT_NVIC_WORDS];
} crit_state;

/**
 * @brief Disable NVIC interrupts, keeping the requested sources enabled.
 *
 * The kernel tick runs on LPTIM1, so TIKU_CRIT_PRESERVE_HTIMER keeps that one
 * line alive; the other preserve flags have no interrupt-driven backend on
 * this port yet and so select nothing.
 *
 * @param preserve_mask  OR of TIKU_CRIT_PRESERVE_* flags
 * @note DSB+ISB makes the disable architecturally visible before the section.
 */
void tiku_crit_arch_mask_irqs(uint8_t preserve_mask) {
    uint32_t keep[CRIT_NVIC_WORDS] = {0};

    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep[STM32N6_IRQ_LPTIM1 / 32U] |= (1UL << (STM32N6_IRQ_LPTIM1 % 32U));
    }

    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        crit_state.iser[i] = TIKU_REG32(STM32N6_NVIC_ISER(i));
        uint32_t to_mask = crit_state.iser[i] & ~keep[i];
        if (to_mask != 0UL) {
            TIKU_REG32(STM32N6_NVIC_ICER(i)) = to_mask;
        }
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

/**
 * @brief Restore the NVIC enable state saved by tiku_crit_arch_mask_irqs().
 */
void tiku_crit_arch_unmask_irqs(void) {
    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        TIKU_REG32(STM32N6_NVIC_ISER(i)) = crit_state.iser[i];
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}
