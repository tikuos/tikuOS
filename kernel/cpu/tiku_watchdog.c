/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog.c - Watchdog timer implementation
 *
 * Platform-independent watchdog timer logic. All hardware register
 * access is delegated to the HAL layer so this file contains only
 * platform-independent code.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_watchdog.h"

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                         */
/*---------------------------------------------------------------------------*/

/* Default watchdog configuration — platform HAL provides the types */
static tiku_wdt_mode_t     tiku_watchdog_mode       = TIKU_WDT_MODE_WATCHDOG;
static tiku_wdt_clk_t      tiku_watchdog_clk        = TIKU_WDT_SRC_ACLK;
static tiku_wdt_interval_t tiku_watchdog_interval    = TIKU_WDT_INTERVAL_DEFAULT;
static int                 tiku_watchdog_start_held  = 0;
static int                 tiku_watchdog_kick_on_start = 1;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the watchdog timer with current configuration
 *
 * Delegates to the HAL to program the watchdog hardware.
 */
void tiku_watchdog_init(void)
{
    WDT_PRINTF("Init\n");
    tiku_watchdog_arch_on(tiku_watchdog_clk, tiku_watchdog_interval);
}

/**
 * @brief Configure the watchdog timer with custom parameters
 *
 * Stores the new configuration and re-initializes the hardware
 * via the HAL.
 *
 * @param mode          Watchdog or interval timer mode
 * @param clk           Clock source selection
 * @param interval      Timeout interval
 * @param start_held    If non-zero, start in held (paused) state
 * @param kick_on_start If non-zero, kick the timer on start
 */
void tiku_watchdog_config(tiku_wdt_mode_t mode, tiku_wdt_clk_t clk,
                          tiku_wdt_interval_t interval, int start_held,
                          int kick_on_start)
{
    WDT_PRINTF("Configured: mode=%u clk=%u\n", mode, clk);
    tiku_watchdog_mode = mode;
    tiku_watchdog_clk = clk;
    tiku_watchdog_interval = interval;
    tiku_watchdog_start_held = start_held;
    tiku_watchdog_kick_on_start = kick_on_start;

    tiku_watchdog_init();

    if (tiku_watchdog_start_held) {
        tiku_watchdog_arch_pause();
    }
}

/**
 * @brief Kick (reset) the watchdog timer to prevent timeout
 */
void tiku_watchdog_kick(void)
{
    tiku_watchdog_arch_kick();
}

/**
 * @brief Pause the watchdog timer
 */
void tiku_watchdog_pause(void)
{
    tiku_watchdog_arch_pause();
}

/**
 * @brief Resume the watchdog timer
 */
void tiku_watchdog_resume(void)
{
    tiku_watchdog_arch_resume(0);
}

/**
 * @brief Resume the watchdog timer with an immediate kick
 */
void tiku_watchdog_resume_with_kick(void)
{
    tiku_watchdog_arch_resume(1);
}

/**
 * @brief Disable the watchdog timer entirely
 */
void tiku_watchdog_off(void)
{
    WDT_PRINTF("Disabled\n");
    tiku_watchdog_arch_off();
}
