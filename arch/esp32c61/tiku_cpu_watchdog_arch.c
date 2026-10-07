/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - ESP32-C61 watchdog on TIMG0's main watchdog.
 *
 * The crystal through a 20000 prescaler gives 2 kHz, so stage 0 holds a
 * timeout in half-milliseconds and resets the system when it expires.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include "tiku_esp32c61_regs.h"

#define WDT         ESP32C61_TIMG0_BASE
#define WDT_PRESCALE 20000UL            /* 40 MHz / 20000 = 2000 Hz */
#define WDT_HZ      2000UL

static struct {
    uint8_t running;
    uint8_t paused;
} wdt_state;

/** @brief Write CONFIG0 and latch it, inside the write-protect window. */
static void wdt_config0(uint32_t v) {
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(WDT)) = ESP32C61_WDT_WKEY;
    TIKU_REG32(ESP32C61_TIMG_WDTCONFIG0(WDT)) = v;
    TIKU_REG32(ESP32C61_TIMG_WDTCONFIG0(WDT)) = v | ESP32C61_TIMG_WDT_UPDATE;
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(WDT)) = 0UL;
}

/** @brief Feed the watchdog, inside the write-protect window. */
static void wdt_feed(void) {
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(WDT)) = ESP32C61_WDT_WKEY;
    TIKU_REG32(ESP32C61_TIMG_WDTFEED(WDT)) = 1UL;
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(WDT)) = 0UL;
}

/** @brief The running configuration: one stage, a system reset. */
static uint32_t wdt_armed(void) {
    return ESP32C61_TIMG_WDT_EN |
           (ESP32C61_TIMG_WDT_RESET_SYS << ESP32C61_TIMG_WDT_STG0_POS);
}

void tiku_cpu_esp32c61_watchdog_off_arch(void) {
    wdt_config0(0UL);
    wdt_state.running = 0U;
    wdt_state.paused  = 0U;
}

void tiku_cpu_esp32c61_watchdog_on_arch(tiku_wdt_clk_t src,
                                        tiku_wdt_interval_t interval) {
    uint32_t hold = (uint32_t)(((unsigned long)interval * WDT_HZ +
                                32767UL) / 32768UL);

    (void)src;
    if (hold < 2UL) {
        hold = 2UL;
    }
    TIKU_REG32(ESP32C61_PCR_TG0_CONF) |= ESP32C61_PCR_TG0_CLK_EN;
    TIKU_REG32(ESP32C61_PCR_TG0_WDT_CLK) =
        (TIKU_REG32(ESP32C61_PCR_TG0_WDT_CLK) & ~ESP32C61_PCR_TG0_WDT_SEL_MSK) |
        ESP32C61_PCR_TG0_WDT_EN;

    wdt_config0(0UL);
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(WDT)) = ESP32C61_WDT_WKEY;
    TIKU_REG32(ESP32C61_TIMG_WDTCONFIG1(WDT)) =
        (WDT_PRESCALE << ESP32C61_TIMG_WDT_PRESCALE_POS) |
        ESP32C61_TIMG_WDT_DIVCNT_RST;
    TIKU_REG32(ESP32C61_TIMG_WDTCONFIG2(WDT)) = hold;
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(WDT)) = 0UL;
    wdt_feed();
    wdt_config0(wdt_armed());
    wdt_state.running = 1U;
    wdt_state.paused  = 0U;
}

void tiku_cpu_esp32c61_watchdog_pause_arch(void) {
    if (wdt_state.running && !wdt_state.paused) {
        wdt_config0(0UL);
        wdt_state.paused = 1U;
    }
}

void tiku_cpu_esp32c61_watchdog_resume_arch(int kick_on_resume) {
    if (wdt_state.running && wdt_state.paused) {
        if (kick_on_resume) {
            wdt_feed();
        }
        wdt_config0(wdt_armed());
    }
    wdt_state.paused = 0U;
}

void tiku_cpu_esp32c61_watchdog_kick_arch(void) {
    if (wdt_state.running) {
        wdt_feed();
    }
}
