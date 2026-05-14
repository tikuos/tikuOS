/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_cpu_watchdog_arch.c - STM32F411RE independent watchdog backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include "tiku_stm32f411_regs.h"
#include <stdint.h>

#define STM32F411_IWDG_PR_DIV32     3U
#define STM32F411_IWDG_RELOAD_MAX   0x0FFFU
#define STM32F411_IWDG_SPIN_TIMEOUT 100000U

static volatile uint8_t g_iwdg_started = 0U;

static void stm32f411_iwdg_wait_ready(void) {
    uint32_t i = STM32F411_IWDG_SPIN_TIMEOUT;
    while (i--) {
        if ((_STM32F411_REG(STM32F411_IWDG_SR) &
             (STM32F411_IWDG_SR_PVU | STM32F411_IWDG_SR_RVU)) == 0U) {
            return;
        }
    }
}

static uint16_t stm32f411_iwdg_reload_from_interval(
    tiku_wdt_interval_t isel) {
    uint32_t reload = ((uint32_t)isel / 32U);

    if (reload == 0U) {
        reload = 1U;
    } else if (reload > STM32F411_IWDG_RELOAD_MAX) {
        reload = STM32F411_IWDG_RELOAD_MAX;
    }

    return (uint16_t)reload;
}

void tiku_cpu_stm32f411_watchdog_off_arch(void) {
    /* STM32 IWDG cannot be stopped once started. The reset default is
     * off unless option bytes force hardware watchdog mode, so boot-time
     * "off" is intentionally a no-op. */
    if (g_iwdg_started) {
        tiku_cpu_stm32f411_watchdog_kick_arch();
    }
}

void tiku_cpu_stm32f411_watchdog_on_arch(tiku_wdt_clk_t src,
                                         tiku_wdt_interval_t isel) {
    (void)src;

    _STM32F411_REG(STM32F411_IWDG_KR) = STM32F411_IWDG_KR_UNLOCK;
    _STM32F411_REG(STM32F411_IWDG_PR) = STM32F411_IWDG_PR_DIV32;
    _STM32F411_REG(STM32F411_IWDG_RLR) =
        stm32f411_iwdg_reload_from_interval(isel);
    stm32f411_iwdg_wait_ready();

    _STM32F411_REG(STM32F411_IWDG_KR) = STM32F411_IWDG_KR_RELOAD;
    _STM32F411_REG(STM32F411_IWDG_KR) = STM32F411_IWDG_KR_ENABLE;
    g_iwdg_started = 1U;
}

void tiku_cpu_stm32f411_watchdog_pause_arch(void) {
    if (g_iwdg_started) {
        tiku_cpu_stm32f411_watchdog_kick_arch();
    }
}

void tiku_cpu_stm32f411_watchdog_resume_arch(int kick_on_resume) {
    if (kick_on_resume) {
        tiku_cpu_stm32f411_watchdog_kick_arch();
    }
}

void tiku_cpu_stm32f411_watchdog_kick_arch(void) {
    _STM32F411_REG(STM32F411_IWDG_KR) = STM32F411_IWDG_KR_RELOAD;
}
