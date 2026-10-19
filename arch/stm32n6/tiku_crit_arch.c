/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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

/* Five NVIC words cover IRQs 0 to 159, which hold every line this port
 * enables; LPTIM1 is IRQ 136. */
#define CRIT_NVIC_WORDS     5U

/**
 * @brief NVIC enable state saved across a mask/unmask pair.
 */
static struct {
    uint32_t iser[CRIT_NVIC_WORDS];
} crit_state;

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask) {
    uint32_t keep[CRIT_NVIC_WORDS] = {0};

    /* The tick and the htimer share LPTIM1 and one NVIC line: either
     * TIKU_CRIT_PRESERVE_HTIMER or TIKU_CRIT_PRESERVE_TICK keeps both.
     * Every other flag keeps nothing: the EXTI pin lines are masked, the
     * UART is polled, and there is no I2C or ADC driver. */
    if (preserve_mask & (TIKU_CRIT_PRESERVE_HTIMER |
                         TIKU_CRIT_PRESERVE_TICK)) {
        keep[STM32N6_IRQ_LPTIM1 / 32U] |= (1UL << (STM32N6_IRQ_LPTIM1 % 32U));
    }

    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        crit_state.iser[i] = TIKU_REG32(STM32N6_NVIC_ISER(i));
        uint32_t to_mask = crit_state.iser[i] & ~keep[i];
        if (to_mask != 0UL) {
            TIKU_REG32(STM32N6_NVIC_ICER(i)) = to_mask;
        }
    }
    /* DSB+ISB: the disable takes effect before the section runs. */
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

void tiku_crit_arch_unmask_irqs(void) {
    /* ISER sets bits only: the saved lines are enabled again, and a line
     * enabled inside the section stays enabled. */
    for (unsigned i = 0; i < CRIT_NVIC_WORDS; i++) {
        TIKU_REG32(STM32N6_NVIC_ISER(i)) = crit_state.iser[i];
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}
