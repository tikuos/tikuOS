/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog_arch.c - S31 timer-group watchdog on the 40 MHz crystal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_watchdog_arch.h"
#include <hal/tiku_cpu.h>
#define REG_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG_WRITE(a,v) (REG_READ(a)=(v))
#define WDT_CONFIG 0x20580048u
#define WDT_FEED 0x20580060u
#define WDT_PROTECT 0x20580064u
#define WDT_KEY 0x50d83aa1u
#define WDT_ACTIVE ((1u << 31) | (3u << 29) | (7u << 18) | (7u << 15))
static uint8_t running, paused;
/** @brief Latch TG0 configuration inside its protection window. */
static void configure(uint32_t value)
{
    REG_WRITE(WDT_PROTECT, WDT_KEY);
    REG_WRITE(WDT_CONFIG, value);
    REG_WRITE(WDT_CONFIG, value | (1u << 22));
    REG_WRITE(WDT_PROTECT, 0);
}
void tiku_watchdog_arch_off(void)
{
    tiku_atomic_enter();
    REG_WRITE(0x20587114u, REG_READ(0x20587114u) | 1u);
    configure(0);
    running = paused = 0;
    tiku_atomic_exit();
}
void tiku_watchdog_arch_on(tiku_wdt_clk_t source, tiku_wdt_interval_t interval)
{
    tiku_atomic_enter();
    (void)source;
    REG_WRITE(0x20587114u, (REG_READ(0x20587114u) & ~((3u << 9) | 2u)) | (1u << 11) | 1u);
    configure(0);
    REG_WRITE(WDT_PROTECT, WDT_KEY);
    REG_WRITE(0x2058004cu, (20000u << 16) | 1u);
    uint32_t hold = ((uint32_t)interval * 2000u + 32767u) / 32768u;
    REG_WRITE(0x20580050u, hold ? hold : 1u);
    REG_WRITE(WDT_FEED, 1);
    REG_WRITE(WDT_PROTECT, 0);
    configure(WDT_ACTIVE);
    running = 1;
    paused = 0;
    tiku_atomic_exit();
}
void tiku_watchdog_arch_kick(void)
{
    tiku_atomic_enter();
    if (running) {
        REG_WRITE(WDT_PROTECT, WDT_KEY);
        REG_WRITE(WDT_FEED, 1);
        REG_WRITE(WDT_PROTECT, 0);
    }
    tiku_atomic_exit();
}
void tiku_watchdog_arch_pause(void)
{
    tiku_atomic_enter();
    if (running && !paused) {
        configure(0);
        paused = 1;
    }
    tiku_atomic_exit();
}
void tiku_watchdog_arch_resume(int kick)
{
    tiku_atomic_enter();
    if (running && paused) {
        if (kick)
            tiku_watchdog_arch_kick();
        configure(WDT_ACTIVE);
        paused = 0;
    }
    tiku_atomic_exit();
}
