/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - STM32N6 independent watchdog (IWDG).
 *
 * Counts off the LSI, so it survives every change to the system clock; once
 * started nothing but a reset stops it, which is what shapes the API below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include "tiku_stm32n6_regs.h"

/** @brief Widest reload the 12-bit down-counter accepts. */
#define IWDG_RLR_MAX    4095UL

/** @brief Slowest prescaler code; the divider it selects is 4 << code. */
#define IWDG_PR_MAX     6U

/** @brief Last requested state, so queries answer consistently. */
static struct {
    uint8_t             running;
    uint8_t             paused;
    tiku_wdt_clk_t      src;
    tiku_wdt_interval_t interval;
} wdt_state;

/** @brief Wait for a pending register write to cross into the LSI domain. */
static void wdt_sync(void) {
    /* Bounded: a stopped LSI would otherwise hang here forever, and a
     * watchdog that cannot be programmed is not worth wedging the boot for. */
    for (unsigned long spins = 2000000UL; spins > 0UL; spins--) {
        if ((TIKU_REG32(STM32N6_IWDG_SR) & STM32N6_IWDG_SR_BUSY) == 0UL) {
            return;
        }
    }
}

void tiku_cpu_stm32n6_watchdog_off_arch(void) {
    /* There is no stop. The reference manual is explicit that only a reset
     * clears the IWDG, so the honest behaviour is to feed it once more and
     * record that the caller wanted it off: nothing resets by surprise, and a
     * later query does not claim a watchdog that is in fact still counting. */
    if (wdt_state.running) {
        TIKU_REG32(STM32N6_IWDG_KR) = STM32N6_IWDG_KR_FEED;
    }
    wdt_state.running = 0U;
    wdt_state.paused  = 0U;
}

void tiku_cpu_stm32n6_watchdog_on_arch(tiku_wdt_clk_t src,
                                       tiku_wdt_interval_t interval) {
    uint32_t ticks = (interval == 0U) ? 1UL : (uint32_t)interval;
    uint32_t div   = 4UL;
    uint8_t  pr    = 0U;

    /* Smallest prescaler the request still fits, so the timeout keeps as much
     * resolution as the 12-bit counter can give it. */
    while (((ticks / div) > IWDG_RLR_MAX) && (pr < IWDG_PR_MAX)) {
        div <<= 1;
        pr++;
    }
    uint32_t rlr = ticks / div;
    if (rlr == 0UL) {
        rlr = 1UL;
    }
    if (rlr > IWDG_RLR_MAX) {
        rlr = IWDG_RLR_MAX;
    }

    TIKU_REG32(STM32N6_IWDG_KR) = STM32N6_IWDG_KR_START;
    TIKU_REG32(STM32N6_IWDG_KR) = STM32N6_IWDG_KR_UNLOCK;
    TIKU_REG32(STM32N6_IWDG_PR)  = pr;
    TIKU_REG32(STM32N6_IWDG_RLR) = rlr;
    wdt_sync();
    TIKU_REG32(STM32N6_IWDG_KR) = STM32N6_IWDG_KR_FEED;

    /* The LSI is the only source wired to this block, so a request for the
     * peripheral clock is recorded but cannot change what feeds the counter. */
    wdt_state.src      = src;
    wdt_state.interval = interval;
    wdt_state.running  = 1U;
    wdt_state.paused   = 0U;
}

void tiku_cpu_stm32n6_watchdog_pause_arch(void) {
    /* Nothing halts the counter, so the closest a pause can get is a fresh
     * full interval: the section that follows has that long to finish. */
    if (wdt_state.running) {
        TIKU_REG32(STM32N6_IWDG_KR) = STM32N6_IWDG_KR_FEED;
    }
    wdt_state.paused = 1U;
}

void tiku_cpu_stm32n6_watchdog_resume_arch(int kick_on_resume) {
    if (wdt_state.running && kick_on_resume) {
        TIKU_REG32(STM32N6_IWDG_KR) = STM32N6_IWDG_KR_FEED;
    }
    wdt_state.paused = 0U;
}

void tiku_cpu_stm32n6_watchdog_kick_arch(void) {
    if (wdt_state.running) {
        TIKU_REG32(STM32N6_IWDG_KR) = STM32N6_IWDG_KR_FEED;
    }
}
