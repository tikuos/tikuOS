/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - RP2350 system tick (Cortex-M SysTick)
 *
 * Programs SysTick at TIKU_CLOCK_ARCH_SECOND Hz (default 128 Hz).
 * The ISR increments the tick counter, derives the seconds field,
 * and calls tiku_sched_notify() so expired software timers fire.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <kernel/scheduler/tiku_sched.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Monotonic tick counter, incremented every SysTick interrupt. */
static volatile tiku_clock_arch_time_t g_ticks    = 0UL;
/** @brief Seconds counter, derived from g_ticks at TIKU_CLOCK_ARCH_SECOND. */
static volatile unsigned long          g_seconds  = 0UL;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief SysTick reload for the current clk_sys: cycles per tick - 1,
 *        clamped to the 24-bit reload register.
 */
static uint32_t systick_reload(void) {
    uint32_t reload = tiku_cpu_rp2350_clock_get_hz() /
                      TIKU_CLOCK_ARCH_SECOND - 1U;
    if (reload > 0x00FFFFFFU) {
        reload = 0x00FFFFFFU;
    }
    return reload;
}

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

void tiku_clock_arch_init(void) {
    /* g_ticks and g_seconds are software counters that SysTick does not
     * reset, so every init restarts the elapsed time here. */
    g_ticks   = 0UL;
    g_seconds = 0UL;

    _RP2350_REG(RP2350_SYST_RVR) = systick_reload();
    _RP2350_REG(RP2350_SYST_CVR) = 0U;     /* clear current */
    _RP2350_REG(RP2350_SYST_CSR) =
        RP2350_SYST_CSR_ENABLE
        | RP2350_SYST_CSR_TICKINT
        | RP2350_SYST_CSR_CLKSRC_CPU;
}

void tiku_rp2350_systick_reclock(void) {
    /* Before tiku_clock_arch_init() SysTick is off, and init programs it. */
    if ((_RP2350_REG(RP2350_SYST_CSR) & RP2350_SYST_CSR_ENABLE) == 0U) {
        return;
    }
    _RP2350_REG(RP2350_SYST_RVR) = systick_reload();
    /* A write to CVR clears it, so the next period starts with the new
     * reload; the tick in progress loses the part already counted. */
    _RP2350_REG(RP2350_SYST_CVR) = 0U;
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
    uint32_t fine = (uint32_t)(((uint64_t)(rvr - cvr) * 0xFFFFU) / rvr);
    return (unsigned short)fine;
}

int tiku_clock_arch_fine_max(void) {
    return 0xFFFF;
}

unsigned char tiku_clock_arch_fault(void) {
    /* The tick runs from clk_sys at the rate tiku_clock_arch_init() reads, so
     * a clk_sys fallback at boot leaves the tick rate correct and this port
     * has no tick fault.  tiku_cpu_rp2350_clock_has_fault() reports clk_sys
     * faults. */
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SYSTICK ISR                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief SysTick interrupt handler — advances the kernel clock.
 *
 * Increments g_ticks on every SysTick wrap, and g_seconds on each multiple of
 * TIKU_CLOCK_ARCH_SECOND.  Calls tiku_sched_notify() so expired software
 * timers fire on the next scheduler iteration.
 */
void tiku_rp2350_systick_handler(void) {
    g_ticks++;
    if ((g_ticks % TIKU_CLOCK_ARCH_SECOND) == 0UL) {
        g_seconds++;
    }
    /* Poll the timer process so expired software timers fire on the
     * next scheduler iteration. */
    tiku_sched_notify();
}
