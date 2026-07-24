/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_apollo4l.c - Apollo4 Lite critical-window IRQ masking (NVIC)
 *
 * Mirrors arch/ambiq/tiku_crit_arch.c (Apollo510). Snapshots the NVIC enable
 * state and clears every IRQ not named in preserve_mask, restoring on exit.
 * Pure Cortex-M NVIC. Apollo4 Lite has 84 external IRQs (ceil(84/32) = 3 words)
 * and a different IRQ map: UART2=17, STIMER_CMPR0=32, GPIO0 pins0-31=56.
 * SysTick is a core exception, never masked here.
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
/** Number of 32-bit NVIC words covering all 84 Apollo4 Lite IRQs */
#define NVIC_WORDS 3

/** Apollo4 Lite IRQ numbers for the preservable lines. */
#define AMBIQ_IRQ_UART2         17   /**< UART2 console IRQ            */
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
