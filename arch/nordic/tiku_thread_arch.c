/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - nRF54L15 worker-thread switcher shim
 *
 * The nRF54L15's Cortex-M33 (ARMv8-M) uses the identical generic
 * Cortex-M switcher as the Apollo and RP2350 parts; this build's vector
 * table (tiku_crt_early.c) names slot 14 as the weak alias
 * tiku_nordic_pendsv_handler, so naming the strong handler the same and
 * including the shared body overrides that vector entry.
 *
 * CYCLE SOURCE -- the one real divergence from the other Cortex-M
 * parts: this die's DWT CYCCNT only counts while a debugger session is
 * up (the CoreSight trace clock is off standalone -- the same silicon
 * behaviour that broke Phase-0 DWT busy-delays), so per-thread cycle
 * accounting cannot ride the DWT.  Instead TIMER00 -- the one 128 MHz
 * timer instance on this part, otherwise unused by the port (tick=GRTC,
 * htimer=TIMER20) -- free-runs in 32-bit mode at PRESCALER=0, giving a
 * counter in exact 1:1 CPU cycles (boot locks the CPU PLL to CK128M).
 * Reads latch the live count into CC[0] via TASKS_CAPTURE.  A read
 * preempted between capture and load returns the interrupting
 * context's (slightly newer) capture -- a sub-microsecond, monotonic
 * accounting error, harmless for tenure charging.
 *
 * The timer starts only when threading starts (first tiku_thread_start),
 * so non-threaded builds/boots pay nothing for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_device_select.h>   /* MDK types + NRF_TIMER00_S */

#define TIKU_THREAD_ARCH_PENDSV        tiku_nordic_pendsv_handler
#define TIKU_THREAD_ARCH_CUSTOM_CYCLES 1

#define THREAD_CYCLE_TIMER   NRF_TIMER00_S
#define THREAD_CYCLE_CC      0u

/** @brief Start TIMER00 free-running at 128 MHz (1:1 CPU cycles). */
static void thread_cycles_init(void)
{
    THREAD_CYCLE_TIMER->TASKS_STOP  = 1UL;
    THREAD_CYCLE_TIMER->INTENCLR    = 0xFFFFFFFFUL;
    THREAD_CYCLE_TIMER->MODE        = 0UL;   /* TIMER_MODE_MODE_Timer       */
    THREAD_CYCLE_TIMER->BITMODE     = 3UL;   /* TIMER_BITMODE_BITMODE_32Bit */
    THREAD_CYCLE_TIMER->PRESCALER   = 0UL;   /* 128 MHz base, undivided     */
    THREAD_CYCLE_TIMER->TASKS_CLEAR = 1UL;
    THREAD_CYCLE_TIMER->TASKS_START = 1UL;   /* free-running from here      */
}

/** @brief Free-running CPU cycle counter (per-thread accounting). */
uint32_t tiku_thread_arch_cycles(void)
{
    THREAD_CYCLE_TIMER->TASKS_CAPTURE[THREAD_CYCLE_CC] = 1UL;
    return THREAD_CYCLE_TIMER->CC[THREAD_CYCLE_CC];
}

#include "kernel/threads/tiku_thread_cortexm.inl"
