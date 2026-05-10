/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - RP2350 system tick (Cortex-M SysTick)
 *
 * Programs SysTick at TIKU_CLOCK_ARCH_SECOND Hz (default 128 Hz).
 * The ISR increments the tick counter, derives the seconds field,
 * and wakes the timer-poll process so software timers expire.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_common.h"
#include <kernel/scheduler/tiku_sched.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

static volatile tiku_clock_arch_time_t g_ticks    = 0UL;
static volatile unsigned long          g_seconds  = 0UL;

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

void tiku_clock_arch_init(void) {
    /* Reset the software tick / seconds accumulators. The MSP430 port
     * gets this for free because Timer A0 starts at zero, but here the
     * SysTick HW counter is independent of g_ticks — failing to reset
     * means tiku_clock_init() leaves the elapsed time intact across
     * re-init, which the test_clock_init_idempotent test fails on. */
    g_ticks   = 0UL;
    g_seconds = 0UL;

    /* SysTick reload value: CPU clock cycles per tick - 1. */
    uint32_t reload = (TIKU_CLOCK_ARCH_INTERVAL) - 1U;
    /* Clamp to the 24-bit reload register width. */
    if (reload > 0x00FFFFFFU) {
        reload = 0x00FFFFFFU;
    }
    _RP2350_REG(RP2350_SYST_RVR) = reload;
    _RP2350_REG(RP2350_SYST_CVR) = 0U;     /* clear current */
    _RP2350_REG(RP2350_SYST_CSR) =
        RP2350_SYST_CSR_ENABLE
        | RP2350_SYST_CSR_TICKINT
        | RP2350_SYST_CSR_CLKSRC_CPU;
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
    tiku_clock_arch_time_t target = g_ticks + t;
    while ((tiku_clock_arch_time_t)(target - g_ticks) > 0UL) {
        /* spin */
    }
}

void tiku_clock_arch_delay(unsigned int us) {
    tiku_cpu_rp2350_delay_us(us);
}

unsigned short tiku_clock_arch_fine(void) {
    /* SysTick CVR counts down from RVR. Express the position within
     * the current tick as a 16-bit value. */
    uint32_t cvr = _RP2350_REG(RP2350_SYST_CVR);
    uint32_t rvr = _RP2350_REG(RP2350_SYST_RVR);
    if (rvr == 0U) {
        return 0;
    }
    uint32_t fine = ((rvr - cvr) * 0xFFFFU) / rvr;
    return (unsigned short)fine;
}

int tiku_clock_arch_fine_max(void) {
    return 0xFFFF;
}

unsigned char tiku_clock_arch_fault(void) {
    return 0;   /* no clock fault tracking on this port */
}

/*---------------------------------------------------------------------------*/
/* SysTick ISR                                                               */
/*---------------------------------------------------------------------------*/

void tiku_rp2350_systick_handler(void) {
    g_ticks++;
    if ((g_ticks % TIKU_CLOCK_ARCH_SECOND) == 0UL) {
        g_seconds++;
    }
    /* Wake the timer-poll process so any expired etimers fire on
     * the next scheduler iteration. */
    tiku_sched_notify();
}
