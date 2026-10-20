/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_arch.c - CPU0 atomic sections and shallow idle on the P4.
 *
 * Atomic sections nest, restore the outermost entry's interrupt state and
 * halt on an unbalanced exit.  The light to deepest idle modes execute WFI,
 * which the tick or htimer ends, and leave the USB PHY powered.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <hal/tiku_cpu.h>
#include <hal/tiku_wake_hal.h>
#include "tiku_irq_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_sleep_arch.h"

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
    switch (mode) {
    case TIKU_CPU_IDLE_LIGHT:
        return idle;
    case TIKU_CPU_IDLE_DEEP:
    case TIKU_CPU_IDLE_DEEPEST:
#if defined(TIKU_P4_FIRMWARE)
        /* PMU light sleep until the next timer deadline. */
        return tiku_p4_light_idle;
#else
        return idle;                    /* the RAM test image has no sleep */
#endif
    case TIKU_CPU_IDLE_OFF:
    default:
        return NULL;
    }
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
    tiku_cpu_idle_enter_t hook = tiku_cpu_idle_hook(mode);

    if (hook == NULL) {
        return "off";
    }
    return hook == idle ? "WFI" : "light sleep";
}

const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t mode)
{
    tiku_cpu_idle_enter_t hook = tiku_cpu_idle_hook(mode);

    if (hook == NULL) {
        return "off (busy-wait)";
    }
    return hook == idle ? "WFI (timer wake; USB powered)"
                        : "light sleep to the next timer deadline";
}
