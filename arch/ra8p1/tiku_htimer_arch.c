/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - RA8P1 high-resolution timer on GPT0.
 *
 * A 32-bit free-running counter on the synchronous PCLKD core clock, with a
 * compare event the ICU routes to the NVIC -- alarms dispatch when due, not
 * at the next kernel tick.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>

#include "tiku_htimer_config.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_ra8p1_regs.h"

/** @brief GPT channel and the NVIC slot its compare event is linked onto. */
#define HT_GPT          0U
#define HT_SLOT         RA8P1_ICU_SLOT_HTIMER

/** @brief Alarms taken, for localising "the ISR never fired" reports. */
volatile uint32_t tiku_htimer_arch_isr_count;

/** @brief GPT counts per microsecond; PCLKD-derived, so it follows the tree. */
static uint32_t ht_per_us = 1U;

/** @brief Whether an alarm is outstanding. */
static volatile uint8_t ht_armed;


/**
 * @brief Clear a slot's ICU status flag and make the clear stick.
 *
 * The read-back is what stops the NVIC re-pending a slot whose clear had not
 * retired at exception return -- the console's double-byte bug, generalised.
 *
 * @param slot  NVIC slot to acknowledge
 */
static void icu_ack(unsigned slot)
{
    TIKU_REG32(RA8P1_ICU_IELSR(slot)) &= ~RA8P1_ICU_IELSR_IR;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(slot));
    /* The NVIC pends independently of the ICU flag, and it pends even while
     * the line is DISABLED -- so clearing IR alone leaves a stale interrupt
     * that fires the instant the line is next unmasked, dispatching an alarm
     * that was armed microseconds ago.  Clear both, always. */
    TIKU_REG32(RA8P1_NVIC_ICPR(slot / 32U)) = (1UL << (slot % 32U));
    __asm__ volatile ("dsb" ::: "memory");
}

void tiku_htimer_arch_init(void)
{
    unsigned long pclkd = tiku_cpu_ra8p1_pclkd_get_hz();

    ht_armed = 0U;
    ht_per_us = (pclkd >= 1000000UL) ? (uint32_t)(pclkd / 1000000UL) : 1U;

    /* GTCLKCR strictly BEFORE the ungate: it is locked the moment MSTPE31
     * reads 0, and leaving it at its async reset default kills the block --
     * see the register header for the mechanism. */
    TIKU_REG32(RA8P1_MSTPCRE) |= RA8P1_MSTPE_GPT0;
    TIKU_REG32(RA8P1_GPT_GTCLKCR) = RA8P1_GPT_GTCLKCR_BPEN;
    TIKU_REG32(RA8P1_MSTPCRE) &= ~RA8P1_MSTPE_GPT0;
    (void)TIKU_REG32(RA8P1_MSTPCRE);

    TIKU_REG32(RA8P1_GPT_GTCR(HT_GPT))  = 0UL;           /* stop to program */
    TIKU_REG32(RA8P1_GPT_GTPR(HT_GPT))  = 0xFFFFFFFFUL;  /* free-run whole  */
    TIKU_REG32(RA8P1_GPT_GTCNT(HT_GPT)) = 0UL;
    TIKU_REG32(RA8P1_GPT_GTST(HT_GPT))  = 0UL;

    /* The compare event is linked but the NVIC line stays MASKED until an
     * alarm is armed: a free-running compare fires once per wrap, and an
     * unrequested dispatch is worse than none. */
    TIKU_REG32(RA8P1_ICU_IELSR(HT_SLOT)) = RA8P1_EVENT_GPT0_CCMPA;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(HT_SLOT));
    TIKU_REG32(RA8P1_NVIC_ICER(HT_SLOT / 32U)) = (1UL << (HT_SLOT % 32U));

    TIKU_REG32(RA8P1_GPT_GTCR(HT_GPT)) = RA8P1_GPT_GTCR_MD_SAW |
                                         RA8P1_GPT_GTCR_CST;
}

tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    return (tiku_htimer_clock_t)(TIKU_REG32(RA8P1_GPT_GTCNT(HT_GPT)) /
                                 ht_per_us);
}

void tiku_htimer_arch_schedule(tiku_htimer_clock_t t)
{
    uint32_t now = TIKU_REG32(RA8P1_GPT_GTCNT(HT_GPT));
    /* The kernel's clock is 16-bit microseconds; recover the signed delta and
     * project it onto the 32-bit counter. */
    int16_t delta_us = (int16_t)((uint16_t)t -
                                 (uint16_t)(now / ht_per_us));
    int32_t counts = (int32_t)delta_us * (int32_t)ht_per_us;

    if (counts < 64) {
        /* A compare armed at or barely ahead of a 240 MHz counter is already
         * behind it by the time the write lands; 64 counts is ~0.27 us of
         * margin, invisible at microsecond resolution. */
        counts = 64;
    }

    TIKU_REG32(RA8P1_GPT_GTST(HT_GPT)) = 0UL;
    TIKU_REG32(RA8P1_GPT_GTCCRA(HT_GPT)) = now + (uint32_t)counts;
    ht_armed = 1U;

    icu_ack(HT_SLOT);
    TIKU_REG32(RA8P1_NVIC_ISER(HT_SLOT / 32U)) = (1UL << (HT_SLOT % 32U));
}

/**
 * @brief GPT0 compare match: the alarm is due.
 *
 * Masks its own NVIC line before dispatching, so a callback that does not
 * reschedule cannot be re-entered by the counter's next lap.
 */
void tiku_ra8p1_gpt0_ccmpa_handler(void)
{
    TIKU_REG32(RA8P1_NVIC_ICER(HT_SLOT / 32U)) = (1UL << (HT_SLOT % 32U));
    TIKU_REG32(RA8P1_GPT_GTST(HT_GPT)) = 0UL;
    icu_ack(HT_SLOT);

    if (ht_armed) {
        ht_armed = 0U;
        tiku_htimer_arch_isr_count++;
        tiku_htimer_run_next();
    }
}
