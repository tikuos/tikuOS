/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_policy.c - the idle mode the scheduler enters, chosen by name.
 *
 * The choice lasts one boot.  The scheduler calls policy_idle(), which checks
 * the wake sources again before each sleep because a driver can disarm one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_power_policy.h"
#include <kernel/scheduler/tiku_sched.h>
#include <hal/tiku_wake_hal.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/** The mode policy_idle() enters while it is the scheduler's hook. */
static tiku_cpu_idle_mode_t selected = TIKU_CPU_IDLE_LIGHT;

/** Policy names, indexed by tiku_cpu_idle_mode_t. */
static const char *const mode_tokens[] = { "off", "light", "deep", "deepest" };

#define MODE_COUNT  (sizeof(mode_tokens) / sizeof(mode_tokens[0]))

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Is a source that wakes @p mode armed?
 */
static int
wake_armed(tiku_cpu_idle_mode_t mode)
{
    tiku_wake_sources_t wake;

    if (mode == TIKU_CPU_IDLE_OFF) {
        return 1;
    }
    tiku_wake_arch_query(&wake);
    return (wake.sources & tiku_cpu_idle_mode_wakes(mode)) != 0u;
}

/**
 * @brief The scheduler's idle hook while a policy mode is set.
 *
 * Light sleep wakes on every source, so it stands in when the last source
 * the chosen mode wakes on has been disarmed since it was chosen.
 */
static void
policy_idle(void)
{
    tiku_cpu_idle_mode_t mode = selected;

    if (!wake_armed(mode)) {
        mode = TIKU_CPU_IDLE_LIGHT;
    }
    tiku_cpu_idle_hook(mode)();
}

/*---------------------------------------------------------------------------*/
/* QUERIES                                                                   */
/*---------------------------------------------------------------------------*/

int
tiku_power_policy_supported(tiku_cpu_idle_mode_t mode)
{
    tiku_cpu_idle_enter_t hook;

    if ((unsigned)mode >= MODE_COUNT) {
        return 0;
    }
    if (mode == TIKU_CPU_IDLE_OFF) {
        return 1;
    }
    hook = tiku_cpu_idle_hook(mode);
    if (hook == NULL) {
        return 0;
    }
    if (mode == TIKU_CPU_IDLE_LIGHT) {
        return 1;
    }
    if (hook == tiku_cpu_idle_hook(TIKU_CPU_IDLE_LIGHT)) {
        return 0;
    }
    return mode != TIKU_CPU_IDLE_DEEPEST ||
           hook != tiku_cpu_idle_hook(TIKU_CPU_IDLE_DEEP);
}

int
tiku_power_policy_loses_console(tiku_cpu_idle_mode_t mode)
{
    return mode != TIKU_CPU_IDLE_OFF &&
           (tiku_cpu_idle_mode_wakes(mode) & TIKU_WAKE_UART_RX) == 0u;
}

int
tiku_power_policy_get(void)
{
    tiku_sched_idle_hook_t hook = tiku_sched_get_idle_hook();
    unsigned mode;

    if (hook == policy_idle) {
        return (int)selected;
    }
    if (hook == NULL) {
        return TIKU_CPU_IDLE_OFF;
    }
    for (mode = TIKU_CPU_IDLE_LIGHT; mode < MODE_COUNT; mode++) {
        if (hook == tiku_cpu_idle_hook((tiku_cpu_idle_mode_t)mode)) {
            return (int)mode;
        }
    }
    return TIKU_POWER_CUSTOM;
}

const char *
tiku_power_policy_token(int mode)
{
    if (mode < 0 || (unsigned)mode >= MODE_COUNT) {
        return "custom";
    }
    return mode_tokens[mode];
}

int
tiku_power_policy_parse(const char *token)
{
    unsigned mode;

    if (token == NULL) {
        return -1;
    }
    for (mode = 0; mode < MODE_COUNT; mode++) {
        if (strcmp(token, mode_tokens[mode]) == 0) {
            return (int)mode;
        }
    }
    return -1;
}

const char *
tiku_power_policy_mode_name(void)
{
    int mode = tiku_power_policy_get();

    if (mode == TIKU_POWER_CUSTOM) {
        return "custom";
    }
    return tiku_cpu_idle_mode_name((tiku_cpu_idle_mode_t)mode);
}

/*---------------------------------------------------------------------------*/
/* SETTER                                                                    */
/*---------------------------------------------------------------------------*/

int
tiku_power_policy_set(tiku_cpu_idle_mode_t mode, uint8_t allow_console_loss)
{
    if ((unsigned)mode >= MODE_COUNT) {
        return TIKU_POWER_INVALID;
    }
    if (!tiku_power_policy_supported(mode)) {
        return TIKU_POWER_UNSUPPORTED;
    }
    if (tiku_power_policy_loses_console(mode) && !allow_console_loss) {
        return TIKU_POWER_ACK_REQUIRED;
    }
    if (!wake_armed(mode)) {
        return TIKU_POWER_NO_WAKE;
    }

    selected = mode;
    /* A mode the tick cannot end keeps the scheduler awake while a timer is
     * armed, or the timer's deadline would pass unseen. */
    tiku_sched_set_idle_tick_wakes(
        (uint8_t)tiku_cpu_idle_mode_wakes_on_tick(mode));
    tiku_sched_set_idle_hook(mode == TIKU_CPU_IDLE_OFF ? NULL : policy_idle);
    return TIKU_POWER_OK;
}
