/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sleep.c - "sleep" command implementation
 *
 * Configures the scheduler's idle hook to enter a low-power mode
 * when no events are pending. Modes are abstract (off / lpm0 /
 * lpm3 / lpm4) and resolved to the platform's real entry function
 * by the CPU HAL.
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
#include <hal/tiku_cpu.h>

/*---------------------------------------------------------------------------*/
/* CURRENT MODE TRACKING                                                     */
/*---------------------------------------------------------------------------*/

static tiku_cpu_idle_mode_t current_idle = TIKU_CPU_IDLE_OFF;

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

/**
 * Map a user-typed token ("off", "lpm0", "lpm3", "lpm4") to a HAL
 * idle-mode enum. Returns 0 on match, -1 on unknown token.
 *
 * Token names follow the platform-specific short name returned by
 * tiku_cpu_idle_mode_name(); this keeps the user-facing CLI stable
 * with the documented MSP430 mode names while letting the HAL
 * choose the actual entry hook.
 */
static int
parse_mode(const char *tok, tiku_cpu_idle_mode_t *out)
{
    if (streq(tok, "off"))  { *out = TIKU_CPU_IDLE_OFF;     return 0; }
    if (streq(tok, "lpm0")) { *out = TIKU_CPU_IDLE_LIGHT;   return 0; }
    if (streq(tok, "lpm3")) { *out = TIKU_CPU_IDLE_DEEP;    return 0; }
    if (streq(tok, "lpm4")) { *out = TIKU_CPU_IDLE_DEEPEST; return 0; }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_sleep(uint8_t argc, const char *argv[])
{
    tiku_cpu_idle_mode_t mode;

    if (argc < 2) {
        SHELL_PRINTF("Idle mode: %s\n",
                     tiku_cpu_idle_mode_desc(current_idle));
        return;
    }

    if (parse_mode(argv[1], &mode) != 0) {
        SHELL_PRINTF("Usage: sleep <off|lpm0|lpm3|lpm4>\n");
        return;
    }

    tiku_sched_set_idle_hook(tiku_cpu_idle_hook(mode));
    current_idle = mode;

    if (mode == TIKU_CPU_IDLE_OFF) {
        SHELL_PRINTF("LPM disabled\n");
    } else {
        SHELL_PRINTF("Idle: %s\n", tiku_cpu_idle_mode_name(mode));
    }
}

const char *
tiku_shell_sleep_mode_str(void)
{
    return tiku_cpu_idle_mode_name(current_idle);
}
