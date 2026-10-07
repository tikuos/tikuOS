/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - RP2350 IRQ-mask backend for tiku_crit.
 *
 * Snapshots both NVIC enable words and restores them after the window.
 * SysTick is a system exception and is not masked by this backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include "tiku_rp2350_regs.h"
#include <stdint.h>

/** @brief NVIC enable words saved across a critical window. */
static struct {
    uint32_t enabled[2];
} crit_state;

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask)
{
    uint32_t keep[2] = { 0U, 0U };
    unsigned word;

    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep[0] |= 1U << RP2350_IRQ_TIMER0_0;
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
        keep[1] |= 1U << (RP2350_IRQ_UART0 - 32U);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_GPIO) {
        keep[0] |= 1U << RP2350_IRQ_IO_BANK0;
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_PIO) {
        keep[0] |= 1U << RP2350_IRQ_PIO0_0;
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_I2C) {
        keep[1] |= (1U << (RP2350_IRQ_I2C0 - 32U)) |
                   (1U << (RP2350_IRQ_I2C1 - 32U));
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_ADC) {
        keep[1] |= 1U << (RP2350_IRQ_ADC_FIFO - 32U);
    }
    for (word = 0U; word < 2U; word++) {
        crit_state.enabled[word] =
            _RP2350_REG(RP2350_NVIC_ISER0 + word * 4U);
        _RP2350_REG(RP2350_NVIC_ICER0 + word * 4U) =
            crit_state.enabled[word] & ~keep[word];
    }
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}

void tiku_crit_arch_unmask_irqs(void)
{
    unsigned word;

    for (word = 0U; word < 2U; word++) {
        _RP2350_REG(RP2350_NVIC_ISER0 + word * 4U) =
            crit_state.enabled[word];
    }
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}
