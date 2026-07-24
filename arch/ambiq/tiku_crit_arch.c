/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - Apollo 510 critical-window IRQ masking (NVIC)
 *
 * Snapshots the NVIC enable state and clears every IRQ family not named
 * in preserve_mask, restoring on exit. Pure Cortex-M NVIC — no AmbiqSuite
 * dependency. SysTick (the system tick) is a core exception, not an NVIC
 * line, so it is never masked here and keeps advancing the clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>   /* TIKU_CRIT_PRESERVE_* */

/**
 * @defgroup NVIC_REGS NVIC enable-set/clear register arrays
 * @brief Cortex-M NVIC register base addresses. Apollo510 has 135
 *        external IRQs, so ceil(135/32) = 5 32-bit words are needed.
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
 * @brief IRQ indices mapped to the TIKU_CRIT_PRESERVE_* flags. Only
 *        the lines that tikuOS drivers may need to keep live during a
 *        critical window are listed here.
 * @{
 */
#define AMBIQ_IRQ_UART0         15   /**< UART0 console IRQ            */
#define AMBIQ_IRQ_STIMER_CMPR0  32   /**< STIMER Compare0 (htimer src) */
#define AMBIQ_IRQ_GPIO0_FIRST   56   /**< First GPIO N0 IRQ line       */
#define AMBIQ_IRQ_GPIO0_LAST    63   /**< Last GPIO N0 IRQ line        */
/** @} */

/** Saved NVIC ISER state, captured by tiku_crit_arch_mask_irqs() */
static uint32_t s_save[NVIC_WORDS];

/**
 * @brief Set a single IRQ bit in a keep-mask word array
 *
 * Converts an IRQ number to a word/bit index and ORs the bit into the
 * caller's keep array so that IRQ is preserved (not cleared) during a
 * critical window.
 *
 * @param keep  Per-word bitmask of IRQs to preserve (NVIC_WORDS elements)
 * @param irq   IRQ number (0-based, < NVIC_WORDS * 32)
 */
static inline void keep_set(uint32_t *keep, unsigned irq) {
    keep[irq >> 5] |= (1u << (irq & 31u));
}

/**
 * @brief Disable NVIC IRQs, preserving those named in preserve_mask
 *
 * Snapshots the current NVIC ISER state into s_save[], builds a
 * keep-mask from preserve_mask (mapping TIKU_CRIT_PRESERVE_* flags to
 * the Apollo510 IRQ numbers above), then clears every IRQ not in the
 * keep-mask via ICER. A DSB+ISB fence ensures the new mask is visible
 * before the caller's protected code runs.
 *
 * SysTick is a core exception (not an NVIC line) and is never affected;
 * the system tick keeps advancing during a critical window.
 *
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

    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep_set(keep, AMBIQ_IRQ_STIMER_CMPR0);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
        keep_set(keep, AMBIQ_IRQ_UART0);
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
