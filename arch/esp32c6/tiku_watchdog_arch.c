/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog_arch.c - C6 timer-group watchdog on the 40 MHz crystal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_watchdog_arch.h"
#include "tiku_io_arch.h"
#define WDT_CONFIG 0x60008048u
#define WDT_FEED 0x60008060u
#define WDT_PROTECT 0x60008064u
#define WDT_KEY 0x50d83aa1u
#define WDT_ACTIVE ((1u << 31) | (3u << 29) | (7u << 18) | (7u << 15))
static uint8_t running = 1, paused;
/** @brief Latch TG0 configuration inside its protection window. */
static void configure(uint32_t value)
{
    TIKU_C6_REG_WRITE(WDT_PROTECT, WDT_KEY);
    TIKU_C6_REG_WRITE(WDT_CONFIG, value);
    TIKU_C6_REG_WRITE(WDT_CONFIG, value | (1u << 22));
    TIKU_C6_REG_WRITE(WDT_PROTECT, 0);
}
void tiku_watchdog_arch_off(void)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    configure(0);
    running = paused = 0;
    TIKU_C6_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_on(tiku_wdt_clk_t source, tiku_wdt_interval_t interval)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    (void)source;
    TIKU_C6_REG_WRITE(0x6009603cu, (TIKU_C6_REG_READ(0x6009603cu) | 1u) & ~2u);
    TIKU_C6_REG_WRITE(0x60096044u, (TIKU_C6_REG_READ(0x60096044u) & ~(3u << 20)) | (1u << 22));
    configure(0);
    TIKU_C6_REG_WRITE(WDT_PROTECT, WDT_KEY);
    TIKU_C6_REG_WRITE(0x6000804cu, (20000u << 16) | 1u);
    uint32_t hold = ((uint32_t)interval * 2000u + 32767u) / 32768u;
    TIKU_C6_REG_WRITE(0x60008050u, hold ? hold : 1u);
    TIKU_C6_REG_WRITE(WDT_FEED, 1);
    TIKU_C6_REG_WRITE(WDT_PROTECT, 0);
    configure(WDT_ACTIVE);
    running = 1;
    paused = 0;
    TIKU_C6_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_kick(void)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (running) {
        TIKU_C6_REG_WRITE(WDT_PROTECT, WDT_KEY);
        TIKU_C6_REG_WRITE(WDT_FEED, 1);
        TIKU_C6_REG_WRITE(WDT_PROTECT, 0);
    }
    TIKU_C6_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_pause(void)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (running && !paused) {
        configure(0);
        paused = 1;
    }
    TIKU_C6_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_resume(int kick)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (running && paused) {
        if (kick)
            tiku_watchdog_arch_kick();
        configure(WDT_ACTIVE);
        paused = 0;
    }
    TIKU_C6_IRQ_RESTORE(state);
}
