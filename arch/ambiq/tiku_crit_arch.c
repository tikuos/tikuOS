/*
 * Tiku Operating System v0.05
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

/* NVIC enable set/clear arrays (Cortex-M). Apollo510 has 135 external
 * IRQs -> ceil(135/32) = 5 words. */
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define NVIC_ICER ((volatile uint32_t *)0xE000E180UL)
#define NVIC_WORDS 5

/* Apollo510 IRQ numbers we map the preserve flags onto. */
#define AMBIQ_IRQ_UART0         15
#define AMBIQ_IRQ_STIMER_CMPR0  32   /* htimer source */
#define AMBIQ_IRQ_GPIO0_FIRST   56
#define AMBIQ_IRQ_GPIO0_LAST    63

static uint32_t s_save[NVIC_WORDS];

static inline void keep_set(uint32_t *keep, unsigned irq) {
    keep[irq >> 5] |= (1u << (irq & 31u));
}

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

void tiku_crit_arch_unmask_irqs(void) {
    int i;
    for (i = 0; i < NVIC_WORDS; i++) {
        NVIC_ISER[i] = s_save[i];
    }
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
}
