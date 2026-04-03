/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sleep.c - "sleep" command implementation
 *
 * Configures the scheduler's idle hook to enter a low-power mode
 * when no events are pending.  The CPU wakes on any enabled
 * interrupt — Timer A0 (system clock) always wakes from LPM3.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_sleep.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/scheduler/tiku_sched.h>

/*---------------------------------------------------------------------------*/
/* LPM ENTRY FUNCTIONS (arch layer)                                          */
/*---------------------------------------------------------------------------*/

extern void tiku_cpu_boot_msp430_power_lpm0_enter(void);
extern void tiku_cpu_boot_msp430_power_lpm3_enter(void);
extern void tiku_cpu_boot_msp430_power_lpm4_enter(void);

/*---------------------------------------------------------------------------*/
/* CURRENT MODE TRACKING                                                     */
/*---------------------------------------------------------------------------*/

/**
 * Current LPM mode: 0 = off, 1 = LPM0, 3 = LPM3, 4 = LPM4
 */
static uint8_t current_lpm;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

static uint8_t
streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

static const char *
lpm_name(uint8_t mode)
{
    switch (mode) {
    case 0: return "off (busy-wait)";
    case 1: return "LPM0 (CPU off, SMCLK+ACLK on)";
    case 3: return "LPM3 (CPU+SMCLK off, ACLK on)";
    case 4: return "LPM4 (all clocks off)";
    default: return "unknown";
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_sleep(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        SHELL_PRINTF("Idle mode: %s\n", lpm_name(current_lpm));
        return;
    }

    if (streq(argv[1], "off")) {
        tiku_sched_set_idle_hook((tiku_sched_idle_hook_t)0);
        current_lpm = 0;
        SHELL_PRINTF("LPM disabled\n");

    } else if (streq(argv[1], "lpm0")) {
        tiku_sched_set_idle_hook(
            tiku_cpu_boot_msp430_power_lpm0_enter);
        current_lpm = 1;
        SHELL_PRINTF("Idle: LPM0\n");

    } else if (streq(argv[1], "lpm3")) {
        tiku_sched_set_idle_hook(
            tiku_cpu_boot_msp430_power_lpm3_enter);
        current_lpm = 3;
        SHELL_PRINTF("Idle: LPM3\n");

    } else if (streq(argv[1], "lpm4")) {
        tiku_sched_set_idle_hook(
            tiku_cpu_boot_msp430_power_lpm4_enter);
        current_lpm = 4;
        SHELL_PRINTF("Idle: LPM4\n");

    } else {
        SHELL_PRINTF("Usage: sleep <off|lpm0|lpm3|lpm4>\n");
    }
}
