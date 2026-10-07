/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - Apollo510 critical-window IRQ masking (NVIC).
 *
 * Snapshots the NVIC enable state and clears every IRQ outside the families
 * named in preserve_mask, restoring the snapshot on exit.  The kernel tick
 * (STIMER compare B, IRQ 33) is kept by TIKU_CRIT_PRESERVE_TICK.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>   /* TIKU_CRIT_PRESERVE_* */

/**
 * @defgroup NVIC_REGS NVIC enable-set/clear register arrays
 * @brief Cortex-M NVIC register bases.  Apollo510's 135 external IRQs take
 *        ceil(135/32) = 5 32-bit words.
 * @{
 */
/** NVIC Interrupt Set-Enable Registers (ISER[0..4]) */
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
/** NVIC Interrupt Clear-Enable Registers (ICER[0..4]) */
#define NVIC_ICER ((volatile uint32_t *)0xE000E180UL)
/** Number of 32-bit NVIC words covering all 135 Apollo510 IRQs */
#define NVIC_WORDS 5
/** @} */

/**
 * @defgroup AMBIQ_IRQ Apollo510 IRQ numbers
 * @brief IRQs retained by the htimer, console and GPIO preserve flags.
 * @{
 */
#define AMBIQ_IRQ_UART0         15   /**< UART0 (not UART1, IRQ 16)    */
#define AMBIQ_IRQ_STIMER_CMPR0  32   /**< STIMER Compare0 (htimer src) */
#define AMBIQ_IRQ_GPIO0_FIRST   56   /**< First GPIO N0 IRQ line       */
#define AMBIQ_IRQ_GPIO0_LAST    63   /**< Last GPIO N0 IRQ line        */
/** @} */

/** Saved NVIC ISER state, captured by tiku_crit_arch_mask_irqs() */
static uint32_t s_save[NVIC_WORDS];

/**
 * @brief Set an IRQ's bit in a keep-mask word array.
 *
 * @param keep  Per-word bitmask of IRQs to keep enabled (NVIC_WORDS words)
 * @param irq   IRQ number (0-based, < NVIC_WORDS * 32)
 */
static inline void keep_set(uint32_t *keep, unsigned irq) {
    keep[irq >> 5] |= (1u << (irq & 31u));
}

/**
 * @brief Disable NVIC IRQs, preserving those named in preserve_mask
 *
 * Snapshots the current NVIC ISER state into s_save[], builds a keep-mask from
 * @p preserve_mask, then clears every IRQ outside it via ICER.  A DSB+ISB fence
 * makes the new mask visible before the caller's protected code runs.
 *
 * @note SysTick is outside the NVIC. TIKU_CRIT_PRESERVE_TICK keeps the
 *       STIMER kernel tick enabled; without it, time catches up on exit.
 * @param preserve_mask  Bitmask of TIKU_CRIT_PRESERVE_* flags naming
 *                       IRQ families that must remain enabled
 */
void tiku_crit_arch_mask_irqs(uint8_t preserve_mask) {
    uint32_t keep[NVIC_WORDS] = { 0, 0, 0, 0, 0 };
    int i;
    unsigned g;

    for (i = 0; i < NVIC_WORDS; i++) {
        s_save[i] = NVIC_ISER[i];
    }

    if (preserve_mask & TIKU_CRIT_PRESERVE_TICK) {
        keep_set(keep, 33u);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep_set(keep, AMBIQ_IRQ_STIMER_CMPR0);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
#if defined(TIKU_CONSOLE_UART1)
        keep_set(keep, 16u);
#else
        keep_set(keep, AMBIQ_IRQ_UART0);
#endif
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

/**
 * @brief Restore the NVIC IRQ enables saved by tiku_crit_arch_mask_irqs()
 *
 * Re-enables all IRQs that were active before the critical window by
 * writing s_save[] back to NVIC ISER. A DSB+ISB fence ensures the
 * restored mask is visible before the caller returns.
 */
void tiku_crit_arch_unmask_irqs(void) {
    int i;
    for (i = 0; i < NVIC_WORDS; i++) {
        NVIC_ISER[i] = s_save[i];
    }
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
}
