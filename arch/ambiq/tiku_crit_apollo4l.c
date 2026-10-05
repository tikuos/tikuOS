/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_apollo4l.c - Apollo4 critical-window IRQ masking (NVIC).
 *
 * Snapshots the NVIC enable state and clears every IRQ outside the families
 * named in preserve_mask, restoring the snapshot on exit.  The kernel tick
 * (STIMER compare B, IRQ 33) is among the IRQs cleared.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>   /* TIKU_CRIT_PRESERVE_* */

/** NVIC Interrupt Set-Enable Registers (ISER[0..2]) */
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
/** NVIC Interrupt Clear-Enable Registers (ICER[0..2]) */
#define NVIC_ICER ((volatile uint32_t *)0xE000E180UL)
/** Number of 32-bit NVIC words covering all 84 Apollo4 IRQs */
#define NVIC_WORDS 3

/*
 * The IRQs that TIKU_CRIT_PRESERVE_HTIMER, _UART and _GPIO keep enabled; the
 * other preserve flags keep nothing on this port.
 */
#define AMBIQ_IRQ_UART2         17   /**< UART2 (not UART0, IRQ 15)    */
#define AMBIQ_IRQ_STIMER_CMPR0  32   /**< STIMER Compare0 (htimer src) */
#define AMBIQ_IRQ_GPIO0_FIRST   56   /**< First GPIO0 IRQ line (pins 0-31) */
#define AMBIQ_IRQ_GPIO0_LAST    59   /**< Last GPIO0 IRQ line          */

/** Saved NVIC ISER state, captured by tiku_crit_arch_mask_irqs() */
static uint32_t s_save[NVIC_WORDS];

/** @brief OR an IRQ bit into a per-word keep-mask. */
static inline void keep_set(uint32_t *keep, unsigned irq) {
    keep[irq >> 5] |= (1u << (irq & 31u));
}

/**
 * @brief Disable NVIC IRQs, preserving those named in preserve_mask.
 *
 * Snapshots ISER into s_save[], builds a keep-mask from the
 * TIKU_CRIT_PRESERVE_* flags, then clears every IRQ not kept via ICER.
 *
 * @note The kernel tick, IRQ 33, is cleared, and tiku_clock_time() catches up
 *       when the window ends.
 * @param preserve_mask  Bitmask of TIKU_CRIT_PRESERVE_* flags
 */
void tiku_crit_arch_mask_irqs(uint8_t preserve_mask) {
    uint32_t keep[NVIC_WORDS] = { 0, 0, 0 };
    int i;
    unsigned g;

    for (i = 0; i < NVIC_WORDS; i++) {
        s_save[i] = NVIC_ISER[i];
    }

    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep_set(keep, AMBIQ_IRQ_STIMER_CMPR0);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
        keep_set(keep, AMBIQ_IRQ_UART2);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_GPIO) {
        for (g = AMBIQ_IRQ_GPIO0_FIRST; g <= AMBIQ_IRQ_GPIO0_LAST; g++) {
            keep_set(keep, g);
        }
    }

    for (i = 0; i < NVIC_WORDS; i++) {
        NVIC_ICER[i] = s_save[i] & ~keep[i];
    }
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
}

/** @brief Restore the NVIC IRQ enables saved by tiku_crit_arch_mask_irqs(). */
void tiku_crit_arch_unmask_irqs(void) {
    int i;
    for (i = 0; i < NVIC_WORDS; i++) {
        NVIC_ISER[i] = s_save[i];
    }
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
}
