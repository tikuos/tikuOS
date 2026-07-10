/*
 * Tiku Operating System v0.05
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
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_watchdog.h"
#include "tiku_hang.h"   /* a kick is also a check-in (liveness assertion) */

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                         */
/*---------------------------------------------------------------------------*/

/* Default watchdog configuration — platform HAL provides the types */
static struct {
    tiku_wdt_mode_t     mode;
    tiku_wdt_clk_t      clk;
    tiku_wdt_interval_t interval;
    int                 start_held;
    int                 kick_on_start;
} wdt = {
    .mode          = TIKU_WDT_MODE_WATCHDOG,
    .clk           = TIKU_WDT_SRC_ACLK,
    .interval      = TIKU_WDT_INTERVAL_DEFAULT,
    .start_held    = 0,
    .kick_on_start = 1,
};

/* Observability: armed flag and kick counter. The flag tracks
 * whether the WDT is currently armed (i.e. tiku_watchdog_off has
 * not been called since the last init/config/on). Pause/resume do
 * not flip it because the configuration is still in effect. */
static volatile uint8_t  wdt_enabled;
static volatile uint32_t wdt_kick_count;

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
    tiku_watchdog_arch_on(wdt.clk, wdt.interval);
    wdt_enabled = 1;
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
    wdt.mode = mode;
    wdt.clk = clk;
    wdt.interval = interval;
    wdt.start_held = start_held;
    wdt.kick_on_start = kick_on_start;

    tiku_watchdog_init();

    if (wdt.start_held) {
        tiku_watchdog_arch_pause();
    }
}

/**
 * @brief Kick (reset) the watchdog timer to prevent timeout
 */
void tiku_watchdog_kick(void)
{
    tiku_watchdog_arch_kick();
    wdt_kick_count++;

    /* A kick is a liveness assertion, so honour it in BOTH watchdog channels:
     * feed the check-in hang detector's heartbeat too.  The long cooperative-
     * blocking builtins (HTTPGET$'s TLS fetch over SLIP, the MQTT waits) hold
     * the CPU inside one dispatch for tens of seconds while kicking from
     * their net pumps; without this the hang detector -- which otherwise only
     * hears scheduler dispatches -- declared them wedged at
     * TIKU_HANG_THRESHOLD_TICKS (~2 s) and warm-reset mid-fetch.  A loop that
     * kicks while truly wedged evades the hang detector exactly as it already
     * evades the hardware watchdog -- no recoverability is lost; the common
     * wedge (an accidental loop that kicks nothing) is still caught. */
    tiku_hang_checkin();
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
 * @brief Return a human-readable string for the current watchdog mode
 */
const char *tiku_watchdog_mode_str(void)
{
    return (wdt.mode == TIKU_WDT_MODE_WATCHDOG)
               ? "watchdog" : "interval";
}

/**
 * @brief Return the current watchdog mode
 */
tiku_wdt_mode_t tiku_watchdog_get_mode(void)
{
    return wdt.mode;
}

/**
 * @brief Return the current clock source
 */
tiku_wdt_clk_t tiku_watchdog_get_clk(void)
{
    return wdt.clk;
}

/**
 * @brief Return the current interval divider
 */
tiku_wdt_interval_t tiku_watchdog_get_interval(void)
{
    return wdt.interval;
}

/**
 * @brief Disable the watchdog timer entirely
 */
void tiku_watchdog_off(void)
{
    WDT_PRINTF("Disabled\n");
    tiku_watchdog_arch_off();
    wdt_enabled = 0;
}

/**
 * @brief Re-enable the watchdog with the most recent configuration.
 */
void tiku_watchdog_on(void)
{
    if (wdt_enabled) {
        return;
    }
    tiku_watchdog_init();
}

int tiku_watchdog_is_on(void)
{
    return wdt_enabled != 0;
}

uint32_t tiku_watchdog_kicks(void)
{
    return wdt_kick_count;
}
