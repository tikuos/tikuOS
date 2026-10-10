/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_watchdog_arch.c - C5 crystal-clocked MWDT0 system-reset stage.
 * Register definitions: ESP-IDF 4d59230 C5 timer_group_reg.h and pcr_reg.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_watchdog_arch.h"
#include "tiku_boot_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"
static uint8_t running, paused;

/** @brief Latch a watchdog configuration inside its write-protection window. */
static void configure(uint32_t config)
{
    TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_PROTECT, TIKU_C5_WDT_KEY);
    TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_CONFIG, config);
    TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_CONFIG, config | (1u << 22));
    TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_PROTECT, 0);
}
void tiku_watchdog_arch_off(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    tiku_esp32c5_boot_watchdogs_disable();
    configure(0);
    running = paused = 0;
    TIKU_C5_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_kick(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (running) {
        TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_PROTECT, TIKU_C5_WDT_KEY);
        TIKU_C5_REG_WRITE(0x60008060u, 1);
        TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_PROTECT, 0);
    }
    TIKU_C5_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_on(tiku_wdt_clk_t src, tiku_wdt_interval_t interval)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    uint32_t hold = ((uint32_t)interval * 2000u + 32767u) / 32768u;
    (void)src;
    TIKU_C5_REG_WRITE(0x60096054u, (TIKU_C5_REG_READ(0x60096054u) | 1u) & ~2u);
    TIKU_C5_REG_WRITE(0x6009605Cu,
                      (TIKU_C5_REG_READ(0x6009605Cu) & ~(3u << 20)) |
                          (1u << 22));
    configure(0);
    TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_PROTECT, TIKU_C5_WDT_KEY);
    TIKU_C5_REG_WRITE(0x6000804Cu, (24000u << 16) | 1u);
    TIKU_C5_REG_WRITE(0x60008050u, hold ? hold : 1u);
    TIKU_C5_REG_WRITE(0x60008060u, 1);
    TIKU_C5_REG_WRITE(TIKU_C5_TG0_WDT_PROTECT, 0);
    configure((1u << 31) | (3u << 29));
    running = 1;
    paused = 0;
    TIKU_C5_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_pause(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (running && !paused) {
        configure(0);
        paused = 1;
    }
    TIKU_C5_IRQ_RESTORE(state);
}
void tiku_watchdog_arch_resume(int kick)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (running && paused) {
        if (kick) {
            tiku_watchdog_arch_kick();
        }
        configure((1u << 31) | (3u << 29));
        paused = 0;
    }
    TIKU_C5_IRQ_RESTORE(state);
}
