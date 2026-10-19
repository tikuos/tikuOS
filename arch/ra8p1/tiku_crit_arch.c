/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - RA8P1 critical sections over the NVIC.
 *
 * The kernel tick is SysTick, a core exception with no NVIC line, so it runs
 * through a masked section and TIKU_CRIT_PRESERVE_TICK needs no handling.
 * TIKU_CRIT_PRESERVE_HTIMER keeps the GPT compare slot enabled.
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

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask)
{
    uint32_t keep[CRIT_NVIC_WORDS] = {0};

    /* Masks NVIC lines.  HTIMER keeps the GPT compare slot; UART keeps the
     * receive and error slots. */
    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep[RA8P1_ICU_SLOT_HTIMER / 32U] |=
            (1UL << (RA8P1_ICU_SLOT_HTIMER % 32U));
    }

    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
        keep[RA8P1_ICU_SLOT_UART_RXI / 32U] |=
            1UL << (RA8P1_ICU_SLOT_UART_RXI % 32U);
        keep[RA8P1_ICU_SLOT_UART_ERI / 32U] |=
            1UL << (RA8P1_ICU_SLOT_UART_ERI % 32U);
    }

    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        uint32_t to_mask;

        crit_state.iser[i] = TIKU_REG32(RA8P1_NVIC_ISER(i));
        to_mask = crit_state.iser[i] & ~keep[i];
        if (to_mask != 0UL) {
            TIKU_REG32(RA8P1_NVIC_ICER(i)) = to_mask;
        }
    }
    /* DSB+ISB: the disable takes effect before return. */
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

void tiku_crit_arch_unmask_irqs(void)
{
    /* ISER sets bits only: the saved lines are enabled again, and a line
     * enabled inside the section stays enabled. */
    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        TIKU_REG32(RA8P1_NVIC_ISER(i)) = crit_state.iser[i];
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}
