/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - STM32N6 kernel clock on LPTIM1.
 *
 * LPTIM1 is clocked from HSI through CLKP and reloads at the tick rate, so
 * time stays correct regardless of what clock the boot ROM left the CPU on.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include "tiku_stm32n6_regs.h"
#include <kernel/scheduler/tiku_sched.h>

#define LPTIM   STM32N6_LPTIM1_BASE

/* Owned by tiku_htimer_arch.c, which shares LPTIM1 and this interrupt. */
void tiku_stm32n6_htimer_on_compare(void);
void tiku_stm32n6_htimer_on_tick(void);

/** @brief Ticks since boot, incremented by the LPTIM1 autoreload interrupt. */
static volatile tiku_clock_arch_time_t g_ticks;

/** @brief Seconds since boot, carried from g_ticks by the same interrupt. */
static volatile unsigned long g_seconds;

/** @brief Sub-second tick accumulator feeding g_seconds. */
static volatile unsigned int g_subsec;

void tiku_clock_arch_init(void) {
    g_ticks   = 0UL;
    g_seconds = 0UL;
    g_subsec  = 0U;

    /* HSI first: it is the source under everything here. */
    TIKU_REG32(STM32N6_RCC_CR) |= STM32N6_RCC_CR_HSION;
    while ((TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_HSIRDY) == 0UL) {
        /* wait for the oscillator to settle */
    }

    /* Point CLKP at HSI, then LPTIM1 at CLKP. Both selects must be written
     * while the peripheral is disabled. */
    uint32_t ccipr7 = TIKU_REG32(STM32N6_RCC_CCIPR7);
    ccipr7 &= ~STM32N6_CCIPR7_PERSEL_MSK;
    ccipr7 |= STM32N6_CCIPR7_PERSEL_HSI;
    TIKU_REG32(STM32N6_RCC_CCIPR7) = ccipr7;

    uint32_t ccipr12 = TIKU_REG32(STM32N6_RCC_CCIPR12);
    ccipr12 &= ~STM32N6_CCIPR12_LPTIM1_MSK;
    ccipr12 |= STM32N6_CCIPR12_LPTIM1_CLKP;
    TIKU_REG32(STM32N6_RCC_CCIPR12) = ccipr12;

    TIKU_REG32(STM32N6_RCC_APB1LENR) |= STM32N6_RCC_APB1LENR_LPTIM1;
    (void)TIKU_REG32(STM32N6_RCC_APB1LENR);

    /* Configure with the timer disabled: CFGR is write-protected while
     * ENABLE is set. */
    TIKU_REG32(STM32N6_LPTIM_CR(LPTIM))   = 0UL;
    TIKU_REG32(STM32N6_LPTIM_CFGR(LPTIM)) =
        ((uint32_t)TIKU_STM32N6_LPTIM_PRESC_LOG2 << STM32N6_LPTIM_CFGR_PRESC_POS);

    /* ARR and DIER writes cross into the LPTIM kernel-clock domain and take
     * effect only when ARROK / DIEROK confirm the transfer, so each write is
     * confirmed before the counter starts -- otherwise CNTSTRT can run the
     * timer with the reset ARR and the interrupt enable never lands. Bounded:
     * a wedged sync then costs a beat, not the boot. */
    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))  = STM32N6_LPTIM_ICR_ARRMCF;
    TIKU_REG32(STM32N6_LPTIM_CR(LPTIM))   = STM32N6_LPTIM_CR_ENABLE;

    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))  = STM32N6_LPTIM_ICR_ARROKCF;
    TIKU_REG32(STM32N6_LPTIM_ARR(LPTIM))  = TIKU_CLOCK_ARCH_INTERVAL - 1UL;
    for (unsigned long spins = 100000UL; spins > 0UL; spins--) {
        if (TIKU_REG32(STM32N6_LPTIM_ISR(LPTIM)) & STM32N6_LPTIM_ISR_ARROK) {
            break;
        }
    }
    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))  = STM32N6_LPTIM_ICR_ARROKCF;

    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))  = STM32N6_LPTIM_ICR_DIEROKCF;
    TIKU_REG32(STM32N6_LPTIM_DIER(LPTIM)) = STM32N6_LPTIM_DIER_ARRMIE;
    for (unsigned long spins = 100000UL; spins > 0UL; spins--) {
        if (TIKU_REG32(STM32N6_LPTIM_ISR(LPTIM)) & STM32N6_LPTIM_ISR_DIEROK) {
            break;
        }
    }
    TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM))  = STM32N6_LPTIM_ICR_DIEROKCF;

    TIKU_REG32(STM32N6_NVIC_ICPR(STM32N6_IRQ_LPTIM1 / 32U)) =
        (1UL << (STM32N6_IRQ_LPTIM1 % 32U));
    TIKU_REG32(STM32N6_NVIC_ISER(STM32N6_IRQ_LPTIM1 / 32U)) =
        (1UL << (STM32N6_IRQ_LPTIM1 % 32U));

    TIKU_REG32(STM32N6_LPTIM_CR(LPTIM)) =
        STM32N6_LPTIM_CR_ENABLE | STM32N6_LPTIM_CR_CNTSTRT;
}

/**
 * @brief LPTIM1 autoreload interrupt: advance the tick and second counters.
 *
 * Overrides the weak stub the vector table starts with.
 */
void tiku_stm32n6_lptim1_isr(void) {
    uint32_t isr = TIKU_REG32(STM32N6_LPTIM_ISR(LPTIM));

    /* Channel 1 is the high-resolution alarm; the driver that owns it is in
     * tiku_htimer_arch.c, which shares this peripheral and this interrupt. */
    if ((isr & STM32N6_LPTIM_ISR_CC1IF) &&
        (TIKU_REG32(STM32N6_LPTIM_DIER(LPTIM)) & STM32N6_LPTIM_DIER_CC1IE)) {
        tiku_stm32n6_htimer_on_compare();
    }

    if (isr & STM32N6_LPTIM_ISR_ARRM) {
        TIKU_REG32(STM32N6_LPTIM_ICR(LPTIM)) = STM32N6_LPTIM_ICR_ARRMCF;

        g_ticks++;
        if (++g_subsec >= TIKU_CLOCK_ARCH_SECOND) {
            g_subsec = 0U;
            g_seconds++;
        }
        /* A new counter period opened, so an alarm that was too far out to
         * program may now fit. */
        tiku_stm32n6_htimer_on_tick();

        /* Wake the scheduler so expired software timers dispatch on the next
         * pass -- without this the shell's poll timer expires unseen and the
         * console never reads a byte. */
        tiku_sched_notify();
    }
}

tiku_clock_arch_time_t tiku_clock_arch_time(void) {
    return g_ticks;
}

unsigned long tiku_clock_arch_seconds(void) {
    return g_seconds;
}

void tiku_clock_arch_set_seconds(unsigned long sec) {
    g_seconds = sec;
}

void tiku_clock_arch_wait(tiku_clock_arch_time_t t) {
    while ((long)(g_ticks - t) < 0) {
        __asm__ volatile ("wfe");
    }
}

/**
 * @brief Read LPTIM1's counter, which lives in an asynchronous domain.
 *
 * Two consecutive reads must agree before the value is trusted, since the
 * register can be sampled mid-carry.
 *
 * @return Counts elapsed within the current tick
 */
static uint32_t lptim_count(void) {
    uint32_t a, b;
    do {
        a = TIKU_REG32(STM32N6_LPTIM_CNT(LPTIM));
        b = TIKU_REG32(STM32N6_LPTIM_CNT(LPTIM));
    } while (a != b);
    return a;
}

void tiku_clock_arch_delay(unsigned int us) {
    /* 500 kHz counter: one count is 2 us, so round the request up. */
    uint32_t want = ((uint32_t)us * (TIKU_STM32N6_LPTIM_HZ / 1000UL) + 999UL) / 1000UL;
    uint32_t last = lptim_count();
    uint32_t seen = 0UL;

    while (seen < want) {
        uint32_t now = lptim_count();
        /* The counter wraps at ARR, so a decrease means one reload passed. */
        seen += (now >= last) ? (now - last)
                             : (now + TIKU_CLOCK_ARCH_INTERVAL - last);
        last = now;
    }
}

unsigned short tiku_clock_arch_fine(void) {
    return (unsigned short)lptim_count();
}

int tiku_clock_arch_fine_max(void) {
    return (int)TIKU_CLOCK_ARCH_INTERVAL;
}
