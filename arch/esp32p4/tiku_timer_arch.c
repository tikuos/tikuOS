/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_timer_arch.c - CPU0 kernel clock, hardware timer and IRQ masks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_timer_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_alarm_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"
#include <kernel/timers/tiku_htimer.h>
#include <kernel/timers/tiku_crit.h>
#include <kernel/scheduler/tiku_sched.h>
#include <hal/tiku_crit_hal.h>
#include <hal/tiku_wake_hal.h>

static uint64_t epoch;
static unsigned long seconds_offset;
static uint32_t saved_enables;

/** @brief Read the counter or halt on a bounded snapshot failure. */
static uint64_t now(void)
{
    uint64_t value;
    if (tiku_p4_systimer_read(&value) != 0) tiku_p4_fatal("timer snapshot");
    return value;
}

void tiku_p4_tick_notify(void) { tiku_sched_notify(); }

void tiku_clock_arch_init(void)
{
    if (tiku_p4_systimer_init() != 0) tiku_p4_fatal("timer initialization");
    epoch = now();
    seconds_offset = 0;
}

tiku_clock_arch_time_t tiku_clock_arch_time(void)
{
    return tiku_p4_systimer_ticks();
}

unsigned long tiku_clock_arch_seconds(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    unsigned long seconds = seconds_offset +
        (unsigned long)(((now() - epoch) & TIKU_P4_SYSTIMER_MASK) / 16000000u);
    TIKU_P4_IRQ_RESTORE(state);
    return seconds;
}

void tiku_clock_arch_set_seconds(unsigned long seconds)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    epoch = now();
    seconds_offset = seconds;
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_clock_arch_delay(unsigned int us)
{
    uint64_t start = now(), duration = (uint64_t)us * 16u;
    while (((now() - start) & TIKU_P4_SYSTIMER_MASK) < duration) { }
}

void tiku_clock_arch_wait(tiku_clock_arch_time_t ticks)
{
    while (ticks--) tiku_clock_arch_delay(7813);
}

unsigned short tiku_clock_arch_fine(void)
{
    return (unsigned short)((now() % TIKU_P4_TICK_COUNTS) / 2u);
}

int tiku_clock_arch_fine_max(void) { return 62500; }
unsigned char tiku_clock_arch_fault(void) { return TIKU_CLOCK_ARCH_FAULT_NONE; }

void tiku_htimer_arch_init(void)
{
    if (tiku_p4_alarm_init(tiku_htimer_run_next) != 0)
        tiku_p4_fatal("hardware timer ownership");
}

tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    return (tiku_htimer_clock_t)(now() / 16u);
}

void tiku_htimer_arch_schedule(tiku_htimer_clock_t time)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    uint64_t current = now();
    int16_t delta = (int16_t)(time - (tiku_htimer_clock_t)(current / 16u));
    uint64_t deadline = (current + (delta > 4 ? (uint32_t)delta * 16u : 64u)) &
                        TIKU_P4_SYSTIMER_MASK;
    if (tiku_p4_alarm_schedule(deadline) != 0) tiku_p4_fatal("timer schedule");
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_crit_arch_mask_irqs(uint8_t preserve)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    unsigned line;
    saved_enables = 0;
    for (line = 0; line < 32; line++) {
        uintptr_t address = TIKU_P4_CLIC_CTRL(line + 16u);
        uint32_t value = TIKU_P4_REG_READ(address);
        if (value & 0x100u) saved_enables |= 1u << line;
        if ((line == 0 && (preserve & TIKU_CRIT_PRESERVE_TICK)) ||
            (line == 1 && (preserve & TIKU_CRIT_PRESERVE_HTIMER))) continue;
        TIKU_P4_REG_WRITE(address, value & ~0x100u);
    }
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_crit_arch_unmask_irqs(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    unsigned line;
    for (line = 0; line < 32; line++) {
        uintptr_t address = TIKU_P4_CLIC_CTRL(line + 16u);
        uint32_t value = TIKU_P4_REG_READ(address) & ~0x100u;
        if (saved_enables & (1u << line)) value |= 0x100u;
        TIKU_P4_REG_WRITE(address, value);
    }
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_wake_arch_query(tiku_wake_sources_t *out)
{
    unsigned i;
    uint32_t state;
    if (out == NULL) return;
    state = TIKU_P4_IRQ_SAVE();
    out->sources = 0;
    for (i = 0; i < TIKU_WAKE_MAX_GPIO_PORTS; i++) out->gpio_ie[i] = 0;
    if ((TIKU_P4_REG_READ(TIKU_P4_CLIC_CTRL(16)) & 0x100u) &&
        TIKU_P4_REG_READ(TIKU_P4_IRQ_MAP(53)) == 16 &&
        (TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_INT_ENA) & 1u) &&
        (TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CONF) & TIKU_P4_SYSTIMER_TARGET(0)))
        out->sources |= TIKU_WAKE_SYSTICK;
    if ((TIKU_P4_REG_READ(TIKU_P4_CLIC_CTRL(17)) & 0x100u) &&
        TIKU_P4_REG_READ(TIKU_P4_IRQ_MAP(54)) == 17 &&
        (TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_INT_ENA) & 2u) &&
        (TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CONF) & TIKU_P4_SYSTIMER_TARGET(1)))
        out->sources |= TIKU_WAKE_HTIMER;
    TIKU_P4_IRQ_RESTORE(state);
}
