/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog.c - Watchdog timer implementation
 *
 * Platform-independent watchdog timer implementation. Delegates to
 * architecture-specific functions.
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

#include <tiku.h>
#include "tiku_watchdog.h"

#ifdef PLATFORM_MSP430

/*
 * Watchdog timer configuration
 *
 * This function initializes the watchdog timer with default values.
 * This aligns with the default values in the watchdog timer configuration with platforms without
 * such configurations for watchdog timer.
 */
tiku_wdt_mode_t tiku_watchdog_mode = TIKU_WDT_MODE_WATCHDOG;
tiku_wdt_clk_t tiku_watchdog_clk = TIKU_WDT_SRC_ACLK;
tiku_wdt_interval_t tiku_watchdog_interval = WDTIS__32768;  /* ~1 second with 32kHz ACLK */
int tiku_watchdog_start_held = 0;
int tiku_watchdog_kick_on_start = 1;  /* Good practice to kick on start */
#endif

/*
 * Initialize the watchdog timer
 *
 * This function initializes the watchdog timer.
 *
 * @return void
*/
void tiku_watchdog_init(void)
{

#ifdef PLATFORM_MSP430
    tiku_cpu_msp430_watchdog_on_arch(tiku_watchdog_clk, tiku_watchdog_interval);
#endif

}

#ifdef PLATFORM_MSP430
/*
 * Configure the watchdog timer
 *
 * This function configures the watchdog timer.
 * This function is used to configure the watchdog timer with custom values.
 *
 * @return void
*/
void tiku_watchdog_config(tiku_wdt_mode_t mode, tiku_wdt_clk_t clk, tiku_wdt_interval_t interval, int start_held, int kick_on_start)
{
    tiku_watchdog_mode = mode;
    tiku_watchdog_clk = clk;
    tiku_watchdog_interval = interval;
    tiku_watchdog_start_held = start_held;
    tiku_watchdog_kick_on_start = kick_on_start;

    tiku_watchdog_init();
}
#endif

/*
 * Kick the watchdog timer
 *
 * This function kicks the watchdog timer.
 *
 * @return void
*/
void tiku_watchdog_kick(void)
{

#ifdef PLATFORM_MSP430
    tiku_cpu_msp430_watchdog_kick_arch();
#endif

}

/*
 * Pause the watchdog timer
 *
 * This function pauses the watchdog timer.
 *
 * @return void
*/
void tiku_watchdog_pause(void)
{

#ifdef PLATFORM_MSP430
    tiku_cpu_msp430_watchdog_pause_arch();
#endif

}

/*
 * Resume the watchdog timer
 *
 * This function resumes the watchdog timer.
 *
 * @return void
*/
void tiku_watchdog_resume(void)
{

#ifdef PLATFORM_MSP430
    tiku_cpu_msp430_watchdog_resume_arch(0);
#endif

}

/*
 * Resume the watchdog timer with kick
 *
 * This function resumes the watchdog timer with kick.
 *
 * @return void
*/
void tiku_watchdog_resume_with_kick(void)
{

#ifdef PLATFORM_MSP430
    tiku_cpu_msp430_watchdog_resume_arch(1);
#endif

}

/*  
 * Off the watchdog timer
 *
 * This function turns off the watchdog timer.
 *
 * @return void
*/
void tiku_watchdog_off(void)
{

#ifdef PLATFORM_MSP430
    tiku_cpu_msp430_watchdog_off_arch();
#endif

}
