/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - MSP430FR5969 CPU watchdog timer configuration
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

#include "tiku_cpu_watchdog_arch.h"


/**
 * @brief Configure the watchdog/interval timer.
 *
 * @param mode   TIKU_WDT_MODE_WATCHDOG or TIKU_WDT_MODE_INTERVAL
 * @param src    TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel   One of WDTIS__* (device-specific)
 * @param start_held If nonzero, leave WDTHOLD set (paused). If zero, run.
 * @param kick_on_start If nonzero, clear counter now (WDTCNTCL).
 */
 void tiku_cpu_msp430_watchdog_config_arch(tiku_wdt_mode_t mode,
    tiku_wdt_clk_t src,
    tiku_wdt_interval_t isel,
    int start_held,
    int kick_on_start)
{
/* Compose the entire low byte in one go; any write replaces it */
uint16_t low = 0;

/* Mode & clock & interval come straight from header bitfields */
low |= (uint16_t)mode;
low |= (uint16_t)src;
low |= (uint16_t)isel;

if (start_held)      low |= WDTHOLD;   /* keep paused */
if (kick_on_start)   low |= WDTCNTCL;  /* start fresh */


WDTCTL = WDTPW | low;
}

/**
 * @brief Enable the watchdog timer in watchdog mode.
 *
 * @param src    TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel   One of WDTIS__* (device-specific)
 */
 void tiku_cpu_msp430_watchdog_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel)
{
    /* Watchdog (reset) mode, running immediately, counter cleared */
    tiku_cpu_msp430_watchdog_config_arch(TIKU_WDT_MODE_WATCHDOG, src, isel, 0, 1);
}

/**
 * @brief Enable the interval timer in interval mode.
 *
 * @param src    TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel   One of WDTIS__* (device-specific)
 */
void tiku_cpu_msp430_watchdog_interval_timer_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel)
{
    /* Interval-timer mode (interrupt), running, counter cleared */
    tiku_cpu_msp430_watchdog_config_arch(TIKU_WDT_MODE_INTERVAL, src, isel, 0, 1);
    /* Remember to enable the interrupt if you want it: SFRIE1 |= WDTIE; */
}

/**
 * @brief Disables the watchdog timer.
 *
 * This function stops the watchdog timer to prevent unintended system resets.
 *
 * @note This is a critical function to call at the beginning of the
 *       application to avoid a boot loop.
 */
void tiku_cpu_msp430_watchdog_off_arch(void)
{

    WDTCTL = WDTPW | WDTHOLD;

}

/**
 * @brief Pauses the watchdog timer without altering its configuration.
 *
 * Preserves mode, clock source, and interval settings so the watchdog
 * can be resumed later without full reconfiguration.
 */
void tiku_cpu_msp430_watchdog_pause_arch(void)
{

    WDTCTL = (WDTCTL & 0x00FF) | WDTPW | WDTHOLD;

}

/**
 * @brief Resumes the watchdog timer from a paused state.
 * @param kick_on_resume 0 = resume as-is, nonzero = also clear counter (“kick”)
 *
 * Atomically clears WDTHOLD and optionally WDTCNTCL while preserving
 * all other WDTCTL low-byte bits. Safe against ISR races.
 */
void tiku_cpu_msp430_watchdog_resume_arch(int kick_on_resume)
{
    uint16_t sr = __get_SR_register();
    __disable_interrupt();

    uint16_t low = (WDTCTL & 0x00FF);
    low &= ~WDTHOLD;
    if (kick_on_resume) {
        low |= WDTCNTCL;
    }
    WDTCTL = WDTPW | low;

    __bis_SR_register(sr & GIE);
}

/**
 * @brief Kicks the watchdog timer.
 *
 * Clears the watchdog timer counter to prevent a reset.
 *
 * @note This function is used to prevent the watchdog timer from resetting
 *       the system.
 */
void tiku_cpu_msp430_watchdog_kick_arch(void)
{
    uint16_t sr = __get_SR_register();
    __disable_interrupt();

    WDTCTL = (WDTCTL & 0x00FF) | WDTPW | WDTCNTCL;

    __bis_SR_register(sr & GIE);
}
