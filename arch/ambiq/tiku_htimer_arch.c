/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - Apollo 510 hardware one-shot timer (STIMER)
 *
 * Backs the kernel htimer onto the Apollo510 System Timer (STIMER)
 * clocked from the 32.768 kHz crystal (TIKU_HTIMER_ARCH_SECOND). The
 * 16-bit htimer clock_t is the low 16 bits of the 32-bit STIMER counter;
 * compares are programmed as deltas. The compare-A interrupt (NVIC IRQ
 * 32) drives tiku_htimer_run_next().
 *
 * @ambiq-sdk: uses am_hal_stimer for counter/compare setup. The de-SDK
 * pass replaces these with direct STIMER register writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_htimer_config.h"
#include "kernel/timers/tiku_htimer.h"
#include "am_mcu_apollo.h"   /* @ambiq-sdk: am_hal_stimer_* */

#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define AMBIQ_IRQ_STIMER_CMPR0  32

void tiku_htimer_arch_init(void) {
    /* Free-run the STIMER from the 32.768 kHz crystal with compare A
     * enabled. @ambiq-sdk */
    am_hal_stimer_config(AM_HAL_STIMER_XTAL_32KHZ |
                         AM_HAL_STIMER_CFG_COMPARE_A_ENABLE);
    am_hal_stimer_int_clear(AM_HAL_STIMER_INT_COMPAREA);
    am_hal_stimer_int_enable(AM_HAL_STIMER_INT_COMPAREA);
    /* Enable STIMER compare0 in the NVIC (IRQ 32). */
    NVIC_ISER[AMBIQ_IRQ_STIMER_CMPR0 >> 5] = (1u << (AMBIQ_IRQ_STIMER_CMPR0 & 31u));
}

void tiku_htimer_arch_schedule(tiku_htimer_clock_t t) {
    uint16_t now   = (uint16_t)(am_hal_stimer_counter_get() & 0xFFFFu); /* @ambiq-sdk */
    uint16_t delta = (uint16_t)(t - now);
    if (delta == 0u) {
        delta = 1u;   /* never schedule a zero delta */
    }
    /* Program compare A = current counter + delta. @ambiq-sdk */
    am_hal_stimer_compare_delta_set(0, (uint32_t)delta);
    am_hal_stimer_int_enable(AM_HAL_STIMER_INT_COMPAREA);
}

tiku_htimer_clock_t tiku_htimer_arch_now(void) {
    return (tiku_htimer_clock_t)(am_hal_stimer_counter_get() & 0xFFFFu); /* @ambiq-sdk */
}

/* STIMER compare-0 ISR (vector slot 16+32 in tiku_crt_early.c). */
void tiku_ambiq_stimer_cmpr0_isr(void) {
    am_hal_stimer_int_clear(AM_HAL_STIMER_INT_COMPAREA);   /* @ambiq-sdk */
    tiku_htimer_run_next();
}
