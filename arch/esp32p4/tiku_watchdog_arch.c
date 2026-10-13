/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_watchdog_arch.c - P4 MWDT0 system reset on the 40 MHz crystal.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_watchdog_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"

static uint8_t running, paused;
#define WDT_RUN ((1u << 31) | (3u << 29) | (7u << 18) | (7u << 15))

/** @brief Latch the watchdog configuration within its write-protection window. */
static void watchdog_configure(uint32_t config)
{
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_PROTECT, TIKU_P4_WDT_KEY);
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_CONFIG, config);
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_CONFIG, config | (1u << 22));
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_PROTECT, 0);
}

void tiku_watchdog_arch_on(tiku_wdt_clk_t src, tiku_wdt_interval_t interval)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    uint32_t hold = ((uint32_t)interval * 2000u + 32767u) / 32768u;
    (void)src;
    TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK2,
                      TIKU_P4_REG_READ(TIKU_P4_SOC_CLK2) | (1u << 21));
    TIKU_P4_REG_WRITE(TIKU_P4_HP_RESET1,
                      TIKU_P4_REG_READ(TIKU_P4_HP_RESET1) & ~(1u << 6));
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_CLOCK,
                      (TIKU_P4_REG_READ(TIKU_P4_TG0_CLOCK) & ~(3u << 28)) |
                      (1u << 30));
    watchdog_configure(0);
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_PROTECT, TIKU_P4_WDT_KEY);
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_PRESCALE, (20000u << 16) | 1u);
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_HOLD, hold ? hold : 1u);
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_FEED, 1u);
    TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_PROTECT, 0);
    watchdog_configure(WDT_RUN);
    running = 1;
    paused = 0;
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_watchdog_arch_off(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    watchdog_configure(0);
    running = paused = 0;
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_watchdog_arch_kick(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (running) {
        TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_PROTECT, TIKU_P4_WDT_KEY);
        TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_FEED, 1u);
        TIKU_P4_REG_WRITE(TIKU_P4_TG0_WDT_PROTECT, 0);
    }
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_watchdog_arch_pause(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (running && !paused) {
        watchdog_configure(0);
        paused = 1;
    }
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_watchdog_arch_resume(int kick)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (running && paused) {
        if (kick) tiku_watchdog_arch_kick();
        watchdog_configure(WDT_RUN);
        paused = 0;
    }
    TIKU_P4_IRQ_RESTORE(state);
}
