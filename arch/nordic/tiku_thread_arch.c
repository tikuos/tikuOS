/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - nRF54L worker-thread switcher shim.
 *
 * The Cortex-M33 uses the same generic switcher as the other parts; only the
 * PendSV symbol differs.  Cycle accounting rides TIMER00 rather than DWT, whose
 * CYCCNT only counts while a debugger session is up on this die.
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
