/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_cpu_arch.c - CPU0 atomic sections and shallow idle for P4 RAM tests.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <hal/tiku_cpu.h>
#include <hal/tiku_wake_hal.h>
#include "tiku_irq_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_systimer_arch.h"

static unsigned nesting;
static uint32_t outer_state;

void tiku_atomic_enter(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (nesting++ == 0) outer_state = state;
}

void tiku_atomic_exit(void)
{
    if (nesting == 0) tiku_p4_fatal("unbalanced atomic exit");
    if (--nesting == 0) TIKU_P4_IRQ_RESTORE(outer_state);
}

void tiku_cpu_irq_enable(void) { TIKU_P4_IRQ_RESTORE(8u); }
void tiku_cpu_irq_disable(void) { (void)TIKU_P4_IRQ_SAVE(); }

/** @brief Wait for a timer interrupt without disabling the USB PHY. */
static void idle(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode)
{
    return mode >= TIKU_CPU_IDLE_LIGHT && mode <= TIKU_CPU_IDLE_DEEPEST
               ? idle : NULL;
}

int tiku_cpu_idle_mode_wakes_on_tick(tiku_cpu_idle_mode_t mode)
{
    return tiku_cpu_idle_hook(mode) != NULL;
}

unsigned int tiku_cpu_idle_mode_wakes(tiku_cpu_idle_mode_t mode)
{
    return tiku_cpu_idle_hook(mode) ? TIKU_WAKE_SYSTICK | TIKU_WAKE_HTIMER : 0;
}

const char *tiku_cpu_idle_mode_name(tiku_cpu_idle_mode_t mode)
{
    return tiku_cpu_idle_hook(mode) ? "WFI" : "off";
}

const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t mode)
{
    return tiku_cpu_idle_hook(mode) ? "WFI (timer wake; USB powered)"
                                  : "off (busy-wait)";
}
