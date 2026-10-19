/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - MSP430 CPU watchdog timer configuration
 *
 * WDT_A driver behind the watchdog HAL: every operation is one
 * password-carrying write to WDTCTL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"

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

 void tiku_cpu_msp430_watchdog_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel)
{
    /* Watchdog (reset) mode, running immediately, counter cleared */
    tiku_cpu_msp430_watchdog_config_arch(TIKU_WDT_MODE_WATCHDOG, src, isel, 0, 1);
}

void tiku_cpu_msp430_watchdog_interval_timer_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel)
{
    /* Interval-timer mode (interrupt), running, counter cleared */
    tiku_cpu_msp430_watchdog_config_arch(TIKU_WDT_MODE_INTERVAL, src, isel, 0, 1);
    /* WDTIE stays as it was; the interrupt needs SFRIE1 |= WDTIE. */
}

void tiku_cpu_msp430_watchdog_off_arch(void)
{

    WDTCTL = WDTPW | WDTHOLD;

}

void tiku_cpu_msp430_watchdog_pause_arch(void)
{

    WDTCTL = (WDTCTL & 0x00FF) | WDTPW | WDTHOLD;

}

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

void tiku_cpu_msp430_watchdog_kick_arch(void)
{
    uint16_t sr = __get_SR_register();
    __disable_interrupt();

    WDTCTL = (WDTCTL & 0x00FF) | WDTPW | WDTCNTCL;

    __bis_SR_register(sr & GIE);
}
