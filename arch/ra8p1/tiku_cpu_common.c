/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - RA8P1 delays, unique ID and reset cause.
 *
 * Millisecond waits count kernel ticks when the tick can advance; otherwise,
 * and for microsecond waits, a spin loop calibrated against the tick runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_ra8p1_regs.h"

#include <stdint.h>

/** @brief Measured spin rate; 0 until measured, and after an invalidate. */
static unsigned long spin_per_ms;

/**
 * @brief Spin for a given number of loop iterations.
 *
 * With the caches off the loop's speed depends on its fetch alignment, so
 * the calibration holds for this copy of the loop only.
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
 * @brief Spin a 64-bit iteration count in calls of at most 2^27 iterations.
 *
 * Each call adds overhead that the calibrated rate leaves out.  The count is
 * 64-bit: a few seconds at 1 GHz overflow 32 bits.
 *
 * @param iters  Total iterations
 */
static void cpu_spin_total(unsigned long long iters)
{
    while (iters > 0x08000000ULL) {
        cpu_spin(0x08000000UL);
        iters -= 0x08000000ULL;
    }
    cpu_spin((unsigned long)iters);
}

/**
 * @brief Report whether the tick can advance from here: the kernel clock
 *        runs, PRIMASK is clear and no exception is active.
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

void tiku_cpu_ra8p1_spin_invalidate(void)
{
    spin_per_ms = 0UL;
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

    cpu_spin_total(((unsigned long long)whole * per_ms) +
                   ((frac * per_ms) / 1000UL));
}

void tiku_cpu_ra8p1_delay_ms(unsigned int ms)
{
    unsigned long ticks;

    if (ms == 0U) {
        return;
    }

    /*
     * Whole ticks are counted on the tick and the sub-tick remainder is spun,
     * so the error is at most one tick period.
     */
    ticks = ((unsigned long)ms * (unsigned long)TIKU_CLOCK_ARCH_SECOND) /
            1000UL;
    if (ticks != 0UL && tick_can_advance()) {
        tiku_clock_arch_time_t target = tiku_clock_arch_time() +
                                        (tiku_clock_arch_time_t)ticks;
        unsigned long rem_ms = ms - (unsigned int)
            ((ticks * 1000UL) / (unsigned long)TIKU_CLOCK_ARCH_SECOND);

        /* Busy-wait; tick_can_advance() found interrupts enabled, so the
         * tick advances meanwhile. */
        while ((long)(target - tiku_clock_arch_time()) > 0) { }
        if (rem_ms != 0UL) {
            cpu_spin_total(rem_ms * tiku_cpu_ra8p1_spin_per_ms());
        }
        return;
    }

    cpu_spin_total((unsigned long long)ms * tiku_cpu_ra8p1_spin_per_ms());
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
    static uint16_t captured;
    static uint8_t  captured_valid;
    uint8_t  s0;
    uint16_t s1;

    if (captured_valid) {
        return captured;
    }

    s0 = TIKU_REG8(RA8P1_RSTSR0);
    s1 = TIKU_REG16(RA8P1_RSTSR1);

    /* Clear the flags, which are sticky, so the next boot reads only its own
     * cause. */
    TIKU_REG8(RA8P1_RSTSR0)  = 0U;
    TIKU_REG16(RA8P1_RSTSR1) = 0U;

    /*
     * MSP430 SYSRSTIV-style codes, the only ones /sys/boot/reason renders;
     * any other value prints as "unknown".  Most specific first: a watchdog
     * reset also sets the power-on flag.
     */
    if (s1 & (RA8P1_RSTSR1_IWDTRF | RA8P1_RSTSR1_WDT0RF)) {
        captured = 0x16U;       /* wdt-timeout */
    } else if (s1 & RA8P1_RSTSR1_SWRF) {
        captured = 0x06U;       /* sw-bor: SCB SYSRESETREQ / reboot */
    } else if (s0 & RA8P1_RSTSR0_DPSRSTF) {
        captured = 0x08U;       /* lpm5-wake: deep software standby */
    } else if (s0 & RA8P1_RSTSR0_PORF) {
        /* A RES# pin reset also sets PORF and reports as power-on. */
        captured = 0x00U;       /* none: cold / power-on */
    } else {
        captured = 0x00U;
    }
    captured_valid = 1U;
    return captured;
}
