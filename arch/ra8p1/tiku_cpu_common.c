/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - RA8P1 busy-wait delays.
 *
 * Millisecond waits ride the tick, which is a hardware timebase; the
 * calibrated spin loop is the fallback for before the tick exists.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_ra8p1_regs.h"

#include <stdint.h>

/** @brief Measured spin rate, 0 until the tick has been available to time it. */
static unsigned long spin_per_ms;

/**
 * @brief Spin for a given number of loop iterations.
 *
 * ONE definition: with the caches off this loop's speed depends on its fetch
 * alignment, so a copy elsewhere would not match this calibration.
 *
 * @param iters  Iterations to run; zero still costs one pass
 */
static void cpu_spin(unsigned long iters)
{
    if (iters == 0UL) { iters = 1UL; }
    __asm__ volatile (
        "1: subs %0, %0, #1\n"
        "   bne  1b\n"
        : "+r" (iters)
        :
        : "cc");
}

/**
 * @brief Spin a total iteration count in as few calls as the range allows.
 *
 * One long spin rather than N short ones, for the same reason: the call
 * overhead the measurement never saw would otherwise be spent N times.
 *
 * @param iters  Total iterations
 */
static void cpu_spin_total(unsigned long iters)
{
    while (iters > 0x08000000UL) {
        cpu_spin(0x08000000UL);
        iters -= 0x08000000UL;
    }
    cpu_spin(iters);
}

/**
 * @brief Report whether the tick CAN advance from here.
 *
 * Three register reads rather than watching the counter, which before the
 * tick exists would spend its whole budget on every call.
 *
 * @return 1 when a SysTick exception could be taken, 0 otherwise
 */
static int tick_can_advance(void)
{
    uint32_t primask, ipsr;

    if (!tiku_ra8p1_clock_arch_running()) {
        return 0;
    }
    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    if (primask != 0UL) {
        return 0;         /* caller holds interrupts off */
    }
    __asm__ volatile ("mrs %0, ipsr" : "=r" (ipsr));
    if (ipsr != 0UL) {
        return 0;         /* inside an exception; SysTick may not preempt */
    }
    return 1;
}

unsigned long tiku_cpu_ra8p1_spin_per_ms(void)
{
    if (spin_per_ms != 0UL) {
        return spin_per_ms;
    }

    if (tick_can_advance()) {
        unsigned long guess = TIKU_RA8P1_SPIN_ITERS_PER_MS;
        tiku_clock_arch_time_t t0, t1;
        unsigned long ticks;

        t0 = tiku_clock_arch_time();
        while (tiku_clock_arch_time() == t0) { }   /* align to a tick edge */
        t0 = tiku_clock_arch_time();
        cpu_spin_total(guess * 64UL);
        t1 = tiku_clock_arch_time();

        ticks = (unsigned long)(t1 - t0);
        if (ticks != 0UL) {
            spin_per_ms = (guess * 64UL *
                           (unsigned long)TIKU_CLOCK_ARCH_SECOND) /
                          (ticks * 1000UL);
        }
    }

    return (spin_per_ms != 0UL) ? spin_per_ms
                                : (unsigned long)TIKU_RA8P1_SPIN_ITERS_PER_MS;
}

void tiku_cpu_ra8p1_delay_us(unsigned int us)
{
    unsigned long per_ms = tiku_cpu_ra8p1_spin_per_ms();
    unsigned long whole  = (unsigned long)(us / 1000U);
    unsigned long frac   = (unsigned long)(us % 1000U);

    cpu_spin_total((whole * per_ms) + ((frac * per_ms) / 1000UL));
}

void tiku_cpu_ra8p1_delay_ms(unsigned int ms)
{
    unsigned long ticks;

    if (ms == 0U) {
        return;
    }

    /*
     * The tick first.  It is a real timebase; the spin loop is a calibrated
     * guess whose calibration this core does not reproduce across builds.
     * Whole ticks come from the tick, the sub-tick remainder from the spin,
     * so the error is bounded by one tick period rather than by the loop.
     */
    ticks = ((unsigned long)ms * (unsigned long)TIKU_CLOCK_ARCH_SECOND) /
            1000UL;
    if (ticks != 0UL && tick_can_advance()) {
        tiku_clock_arch_time_t target = tiku_clock_arch_time() +
                                        (tiku_clock_arch_time_t)ticks;
        unsigned long rem_ms = ms - (unsigned int)
            ((ticks * 1000UL) / (unsigned long)TIKU_CLOCK_ARCH_SECOND);

        /* Busy-wait, not WFI: a caller holding interrupts off has already
         * been sent down the spin path by tick_can_advance(). */
        while ((long)(target - tiku_clock_arch_time()) > 0) { }
        if (rem_ms != 0UL) {
            cpu_spin_total(rem_ms * tiku_cpu_ra8p1_spin_per_ms());
        }
        return;
    }

    cpu_spin_total((unsigned long)ms * tiku_cpu_ra8p1_spin_per_ms());
}

uint8_t tiku_cpu_ra8p1_unique_id(uint8_t *buf, uint8_t len)
{
    uint8_t n = 0U;

    if (buf == 0) { return 0U; }
    for (unsigned w = 0; w < 4U && n < len; w++) {
        uint32_t v = TIKU_REG32(RA8P1_UIDR(w));
        for (unsigned b = 0; b < 4U && n < len; b++) {
            buf[n++] = (uint8_t)(v >> (8U * b));
        }
    }
    return n;
}

uint16_t tiku_cpu_ra8p1_reset_reason(void)
{
    uint8_t  s0 = TIKU_REG8(RA8P1_RSTSR0);
    uint16_t s1 = TIKU_REG16(RA8P1_RSTSR1);
    uint16_t out = 0U;

    if (s0 & RA8P1_RSTSR0_PORF)    { out |= TIKU_RA8P1_RESET_POWER; }
    if (s0 & RA8P1_RSTSR0_DPSRSTF) { out |= TIKU_RA8P1_RESET_LOWPOWER; }
    if (s1 & RA8P1_RSTSR1_SWRF)    { out |= TIKU_RA8P1_RESET_SOFT; }
    if (s1 & (RA8P1_RSTSR1_IWDTRF | RA8P1_RSTSR1_WDT0RF)) {
        out |= TIKU_RA8P1_RESET_WATCHDOG;
    }
    /* No pin-reset flag exists here: RES# reports as a power-on reset, and
     * claiming TIKU_RA8P1_RESET_PIN would invent a distinction the silicon
     * does not make. */
    return out;
}
