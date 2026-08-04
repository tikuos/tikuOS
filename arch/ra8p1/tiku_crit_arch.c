/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - RA8P1 critical sections over the NVIC.
 *
 * The kernel tick is SysTick, a core exception with no NVIC line, so masking
 * the NVIC never silences it -- TIKU_CRIT_PRESERVE_HTIMER therefore has
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include <stdint.h>

#include <arch/ra8p1/tiku_device_select.h>
#include "tiku_ra8p1_regs.h"

/** @brief NVIC ISER/ICER words covering TIKU_RA8P1_NUM_EXT_IRQS lines. */
#define CRIT_NVIC_WORDS     ((TIKU_RA8P1_NUM_EXT_IRQS + 31U) / 32U)

/**
 * @brief NVIC enable state saved across a mask/unmask pair.
 */
static struct {
    uint32_t iser[CRIT_NVIC_WORDS];
} crit_state;

/**
 * @brief Disable NVIC interrupts, keeping the requested sources enabled.
 *
 * @param preserve_mask  OR of TIKU_CRIT_PRESERVE_* flags
 * @note DSB+ISB makes the disable architecturally visible before the section.
 */
void tiku_crit_arch_mask_irqs(uint8_t preserve_mask)
{
    (void)preserve_mask;    /* no NVIC-driven backend on this port yet */

    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        crit_state.iser[i] = TIKU_REG32(RA8P1_NVIC_ISER(i));
        if (crit_state.iser[i] != 0UL) {
            TIKU_REG32(RA8P1_NVIC_ICER(i)) = crit_state.iser[i];
        }
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

/**
 * @brief Restore the NVIC enable state saved by tiku_crit_arch_mask_irqs().
 */
void tiku_crit_arch_unmask_irqs(void)
{
    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        TIKU_REG32(RA8P1_NVIC_ISER(i)) = crit_state.iser[i];
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}
