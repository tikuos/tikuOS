/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - C6 SYSTIMER target0 hardware alarm.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_irq_arch.h"
#include "tiku_cpu_common.h"
#include <kernel/timers/tiku_htimer.h>
#define ALARM_LINE 19u
#define ALARM_SOURCE 57u
#define ALARM_BIT (1u << 24)
static uint8_t initialized;
/** @brief Disable target0 before dispatching its kernel callback. */
static void alarm_interrupt(void)
{
    TIKU_C6_REG_WRITE(C6_SYSTIMER, TIKU_C6_REG_READ(C6_SYSTIMER) & ~ALARM_BIT);
    TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x6c, 1);
    tiku_htimer_run_next();
}
void tiku_htimer_arch_init(void)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (!initialized) {
        if ((TIKU_C6_REG_READ(C6_SYSTIMER) & ALARM_BIT) ||
            (TIKU_C6_REG_READ(C6_SYSTIMER + 0x64) & 1u) ||
            tiku_c6_irq_attach(ALARM_LINE, ALARM_SOURCE, 1, alarm_interrupt))
            tiku_cpu_c6_restart();
        TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x34, 0);
        TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x6c, 1);
        TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x64, TIKU_C6_REG_READ(C6_SYSTIMER + 0x64) | 1u);
        if (tiku_c6_irq_enable(ALARM_LINE, 1))
            tiku_cpu_c6_restart();
        initialized = 1;
    }
    TIKU_C6_IRQ_RESTORE(state);
}
tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    uint64_t now;
    if (tiku_c6_systimer_read(&now))
        tiku_cpu_c6_restart();
    return (tiku_htimer_clock_t)(now / 16u);
}
void tiku_htimer_arch_schedule(tiku_htimer_clock_t time)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    uint64_t current;
    if (tiku_c6_systimer_read(&current))
        tiku_cpu_c6_restart();
    int16_t delta = (int16_t)(time - (tiku_htimer_clock_t)(current / 16u));
    uint64_t next = current + (delta > 4 ? (uint32_t)delta * 16u : 64u);
    TIKU_C6_REG_WRITE(C6_SYSTIMER, TIKU_C6_REG_READ(C6_SYSTIMER) & ~ALARM_BIT);
    TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x6c, 1);
    TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x1c, (uint32_t)(next >> 32) & 0xfffffu);
    TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x20, (uint32_t)next);
    TIKU_C6_REG_WRITE(C6_SYSTIMER + 0x50, 1);
    TIKU_C6_REG_WRITE(C6_SYSTIMER, TIKU_C6_REG_READ(C6_SYSTIMER) | ALARM_BIT);
    TIKU_C6_IRQ_RESTORE(state);
}
