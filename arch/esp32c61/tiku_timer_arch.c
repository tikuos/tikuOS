/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - ESP32-C61 system tick on SYSTIMER alarm 0.
 *
 * Each tick re-arms the alarm, and the interrupt counts every tick that fell
 * due, so a long masked window loses none and a tickless stretch is exact:
 * the alarm moves to the next deadline and the wake credits what elapsed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <hal/tiku_clock_hal.h>

#include "tiku_timer_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_esp32c61_regs.h"

#ifndef TIKU_MINIMAL
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/timers/tiku_clock.h>
#endif

/* 125000 counts per tick halve to fit the unsigned short fine value. */
#define FINE_SHIFT  1U

static volatile tiku_clock_arch_time_t g_ticks;
static volatile unsigned long g_seconds;
static volatile unsigned int g_subsec;
static volatile uint64_t g_due;         /* the count the next tick falls at */
static volatile uint8_t g_running;
static volatile uint8_t g_stretched;    /* the alarm sits past g_due */

void tiku_esp32c61_alarm_arm(unsigned n, uint64_t at) {
    uint32_t s = tiku_esp32c61_mie_off();

    TIKU_REG32(ESP32C61_SYSTIMER_CONF) &= ~ESP32C61_SYSTIMER_ALARM_EN(n);
    /* A flag left from the last alarm must not fire this one. */
    TIKU_REG32(ESP32C61_SYSTIMER_INT_CLR) = ESP32C61_SYSTIMER_INT(n);
    TIKU_REG32(ESP32C61_SYSTIMER_TARGET_CONF(n)) = 0UL;  /* one-shot, unit 0 */
    TIKU_REG32(ESP32C61_SYSTIMER_TARGET_HI(n)) = (uint32_t)(at >> 32) & 0xFFFFFUL;
    TIKU_REG32(ESP32C61_SYSTIMER_TARGET_LO(n)) = (uint32_t)at;
    TIKU_REG32(ESP32C61_SYSTIMER_COMP_LOAD(n)) = 1UL;
    TIKU_REG32(ESP32C61_SYSTIMER_CONF) |= ESP32C61_SYSTIMER_ALARM_EN(n);
    TIKU_REG32(ESP32C61_SYSTIMER_INT_ENA) |= ESP32C61_SYSTIMER_INT(n);
    tiku_esp32c61_mie_restore(s);
}

void tiku_esp32c61_alarm_disarm(unsigned n) {
    uint32_t s = tiku_esp32c61_mie_off();

    TIKU_REG32(ESP32C61_SYSTIMER_INT_ENA) &= ~ESP32C61_SYSTIMER_INT(n);
    TIKU_REG32(ESP32C61_SYSTIMER_CONF) &= ~ESP32C61_SYSTIMER_ALARM_EN(n);
    TIKU_REG32(ESP32C61_SYSTIMER_INT_CLR) = ESP32C61_SYSTIMER_INT(n);
    (void)TIKU_REG32(ESP32C61_SYSTIMER_INT_ST);
    tiku_esp32c61_mie_restore(s);
}

uint64_t tiku_esp32c61_alarm_due(uint64_t now) {
    uint32_t conf = TIKU_REG32(ESP32C61_SYSTIMER_CONF);
    uint32_t ena = TIKU_REG32(ESP32C61_SYSTIMER_INT_ENA);
    uint32_t raw = TIKU_REG32(ESP32C61_SYSTIMER_INT_RAW);
    uint64_t due = ~0ULL, at;

    for (unsigned n = TIKU_ESP32C61_ALARM_TICK;
         n <= TIKU_ESP32C61_ALARM_DRIVER; n++) {
        if ((conf & ESP32C61_SYSTIMER_ALARM_EN(n)) == 0UL ||
            (ena & ESP32C61_SYSTIMER_INT(n)) == 0UL) {
            continue;
        }
        if ((raw & ESP32C61_SYSTIMER_INT(n)) != 0UL) {
            return now;                 /* fired, not yet taken */
        }
        at = ((uint64_t)(TIKU_REG32(ESP32C61_SYSTIMER_TARGET_HI(n)) &
                         0xFFFFFUL) << 32) |
             TIKU_REG32(ESP32C61_SYSTIMER_TARGET_LO(n));
        if (at > now && at < due) {     /* a past one-shot is spent */
            due = at;
        }
    }
    return due;
}

/**
 * @brief Count every tick due by @p now; how many were counted.
 */
static unsigned long tick_account(uint64_t now) {
    unsigned long n = 0UL;

    while (now >= g_due) {
        g_ticks++;
        if (++g_subsec >= TIKU_CLOCK_ARCH_SECOND) {
            g_subsec = 0U;
            g_seconds++;
        }
        g_due += TIKU_CLOCK_ARCH_INTERVAL;
        n++;
    }
    return n;
}

/**
 * @brief Alarm 0: count every tick now due, arm the next, wake the scheduler.
 *
 * The notify is not optional: without it expired timers never dispatch and
 * the failure looks like a dead console rather than a dead timer.
 */
static void tick_isr(void) {
    uint64_t now;

    TIKU_REG32(ESP32C61_SYSTIMER_INT_CLR) =
        ESP32C61_SYSTIMER_INT(TIKU_ESP32C61_ALARM_TICK);
    now = tiku_cpu_esp32c61_systimer();
    if (now == 0ULL) {
        now = g_due;            /* a failed read still counts its own tick */
    }
    (void)tick_account(now);
    g_stretched = 0U;
    tiku_esp32c61_alarm_arm(TIKU_ESP32C61_ALARM_TICK, g_due);
#ifndef TIKU_MINIMAL
    tiku_sched_notify();
#endif
}

void tiku_clock_arch_init(void) {
    g_ticks   = 0UL;
    g_seconds = 0UL;
    g_subsec  = 0U;

    TIKU_REG32(ESP32C61_SYSTIMER_CONF) |=
        ESP32C61_SYSTIMER_CLK_EN | ESP32C61_SYSTIMER_UNIT0_EN;
    g_due = tiku_cpu_esp32c61_systimer() + TIKU_CLOCK_ARCH_INTERVAL;
    tiku_esp32c61_irq_attach(TIKU_ESP32C61_LINE_TICK,
                             ESP32C61_SRC_SYSTIMER(TIKU_ESP32C61_ALARM_TICK),
                             TIKU_ESP32C61_LEVEL_TIMER, tick_isr);
    tiku_esp32c61_alarm_arm(TIKU_ESP32C61_ALARM_TICK, g_due);
    tiku_esp32c61_irq_enable(TIKU_ESP32C61_LINE_TICK);
    g_running = 1U;
}

int tiku_esp32c61_clock_running(void) {
    return g_running;
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

    /* A tick that cannot run cannot end the wait, yet its pending line still
     * ends every wfi: count the time on SYSTIMER instead. */
    if (!g_running ||
        (ESP32C61_CSR_READ(mstatus) & ESP32C61_MSTATUS_MIE) == 0UL ||
        (tiku_esp32c61_irq_enabled() &
         (1UL << TIKU_ESP32C61_LINE_TICK)) == 0UL) {
        uint64_t t0 = tiku_cpu_esp32c61_systimer();
        uint64_t span = (uint64_t)t * TIKU_CLOCK_ARCH_INTERVAL;

        while (tiku_cpu_esp32c61_systimer() - t0 < span) {
        }
        return;
    }
    while ((long)(target - g_ticks) > 0) {
        __asm__ volatile ("wfi");
    }
}

void tiku_clock_arch_delay(unsigned int us) {
    uint64_t t0 = tiku_cpu_esp32c61_systimer();
    uint64_t span = (uint64_t)us * (ESP32C61_SYSTIMER_HZ / 1000000UL);

    while (tiku_cpu_esp32c61_systimer() - t0 < span) {
    }
}

unsigned short tiku_clock_arch_fine(void) {
    tiku_clock_arch_time_t t;
    uint64_t due, now;
    int64_t into;

    /* g_due moves only with g_ticks, so an unchanged count means an untorn
     * 64-bit read. */
    do {
        t   = g_ticks;
        due = g_due;
    } while (t != g_ticks);
    now  = tiku_cpu_esp32c61_systimer();
    into = (int64_t)(now - (due - TIKU_CLOCK_ARCH_INTERVAL));
    if (into < 0) {
        into = 0;
    } else if (into >= (int64_t)TIKU_CLOCK_ARCH_INTERVAL) {
        into = (int64_t)TIKU_CLOCK_ARCH_INTERVAL - 1;
    }
    return (unsigned short)((uint32_t)into >> FINE_SHIFT);
}

int tiku_clock_arch_fine_max(void) {
    return (int)(TIKU_CLOCK_ARCH_INTERVAL >> FINE_SHIFT);
}

unsigned char tiku_clock_arch_fault(void) {
    return TIKU_CLOCK_ARCH_FAULT_NONE;
}

#ifndef TIKU_MINIMAL
/**
 * @brief Move the tick alarm out to the deadline @p ticks_ahead ticks on.
 *
 * Interrupts are masked by the caller.  g_due is the next boundary, so the
 * deadline is ticks_ahead - 1 boundaries past it; nothing is credited here.
 */
int tiku_clock_tickless_begin(tiku_clock_time_t ticks_ahead) {
    if (!g_running || ticks_ahead < 2U) {
        return 0;
    }
    tiku_esp32c61_alarm_arm(TIKU_ESP32C61_ALARM_TICK,
                            g_due + (uint64_t)(ticks_ahead - 1U) *
                                    TIKU_CLOCK_ARCH_INTERVAL);
    g_stretched = 1U;
    return 1;
}

/**
 * @brief Credit the ticks a stretch covered and bring the alarm back in.
 *
 * Re-arming at the next boundary cancels a far alarm an early wake left
 * behind.  A failed read credits nothing: the next tick catches up.
 */
void tiku_clock_tickless_end(void) {
    uint64_t now;

    if (!g_stretched) {
        return;
    }
    g_stretched = 0U;
    now = tiku_cpu_esp32c61_systimer();
    if (now != 0ULL && tick_account(now) != 0UL) {
        tiku_sched_notify();
    }
    tiku_esp32c61_alarm_arm(TIKU_ESP32C61_ALARM_TICK, g_due);
}

int tiku_clock_tickless_available(void) {
    return 1;
}
#endif
