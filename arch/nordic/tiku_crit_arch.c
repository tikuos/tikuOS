/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - nRF54L selective NVIC-mask backend for tiku_crit
 *
 * Critical execution windows mask interrupt sources at the NVIC, keeping the
 * lines named in the preserve_mask alive -- the same model as the rp2350 and
 * ambiq backends.  (The first cut of this port used PRIMASK and ignored the
 * preserve_mask, which was defensible while the console was polled and the
 * htimer/GPIO IRQs were stubs; with those now live, a preserved htimer must
 * keep firing through a window, so the selective form is required.)
 *
 * nRF54L external IRQs span nine NVIC ISER/ICER words (0..271), so the
 * snapshot covers all nine.  The preserve flags map to the MDK IRQn enum:
 *
 *   TIKU_CRIT_PRESERVE_TICK   -> GRTC 226 (+ TIMER10 133 fallback tick)
 *   TIKU_CRIT_PRESERVE_HTIMER -> TIMER20 202
 *   TIKU_CRIT_PRESERVE_UART   -> SERIAL20 198 + SERIAL30 260
 *   TIKU_CRIT_PRESERVE_GPIO   -> GPIOTE20 218 + GPIOTE30 268
 *
 * I2C/ADC/SPI are polled (no NVIC lines) and the WDT runs in reset mode, so
 * their flags have nothing to keep.  Not nesting-safe (single saved snapshot),
 * matching the rp2350 backend -- the kernel brackets short, non-nested
 * critical sections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <stdint.h>

/** @brief NVIC ISER/ICER words covering external IRQs 0..271. */
#define TIKU_NORDIC_NVIC_WORDS  9u

/** @brief ISER snapshot taken at mask time, restored on unmask. */
static uint32_t tiku_nordic_crit_iser[TIKU_NORDIC_NVIC_WORDS];

/** @brief Set IRQ @p irqn's bit in a keep-mask word array. */
static void keep_set(uint32_t *keep, uint32_t irqn)
{
    keep[irqn >> 5] |= (1UL << (irqn & 0x1Fu));
}

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask)
{
    uint32_t keep[TIKU_NORDIC_NVIC_WORDS] = { 0 };
    uint32_t w;

    if (preserve_mask & TIKU_CRIT_PRESERVE_TICK) {
        keep_set(keep, 226u);              /* GRTC_0 (default tick)      */
        keep_set(keep, 133u);              /* TIMER10 (fallback tick)    */
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        keep_set(keep, 202u);              /* TIMER20 (htimer one-shot)  */
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
        keep_set(keep, 198u);              /* SERIAL20 (UARTE20 console) */
        keep_set(keep, 260u);              /* SERIAL30 (UARTE30 console) */
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_GPIO) {
        keep_set(keep, 218u);              /* GPIOTE20 line 0 (P1/P2)    */
        keep_set(keep, 268u);              /* GPIOTE30 line 0 (P0)       */
    }
    /* I2C / ADC / SPI are polled and the WDT is reset-mode: no lines. */

    for (w = 0u; w < TIKU_NORDIC_NVIC_WORDS; w++) {
        uint32_t to_mask;

        tiku_nordic_crit_iser[w] = TIKU_NVIC->ISER[w];
        to_mask = tiku_nordic_crit_iser[w] & ~keep[w];
        if (to_mask != 0UL) {
            TIKU_NVIC->ICER[w] = to_mask;
        }
    }

    /* DSB+ISB so the NVIC disables take architectural effect before the
     * critical-section body runs. */
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}

void tiku_crit_arch_unmask_irqs(void)
{
    uint32_t w;

    for (w = 0u; w < TIKU_NORDIC_NVIC_WORDS; w++) {
        if (tiku_nordic_crit_iser[w] != 0UL) {
            TIKU_NVIC->ISER[w] = tiku_nordic_crit_iser[w];
        }
    }
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}
