/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_freq.c - "freq" command: show or set the CPU core frequency.
 *
 *   freq          -- print the current core clock in MHz
 *   freq <mhz>    -- request a core frequency (e.g. 96, or 192 for Ambiq turbo)
 *
 * Setting drives the platform tiku_cpu_freq_init() path: on MSP430 it
 * reconfigures the DCO; on the Ambiq parts it selects the Low-Power / High-
 * Performance perf mode. A request the platform can't honour leaves the clock
 * unchanged and is reported back. (A runtime change can affect peripherals
 * whose clock derives from the core on some parts.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_freq.h"
#include <kernel/shell/tiku_shell.h>   /* SHELL_PRINTF */
#include <hal/tiku_cpu.h>              /* tiku_cpu_freq_init / tiku_cpu_mclk_hz */

void
tiku_shell_cmd_freq(uint8_t argc, const char *argv[])
{
    const char   *p;
    unsigned long req;
    unsigned long now;

    if (argc < 2) {
        SHELL_PRINTF("CPU: %lu MHz\n", tiku_cpu_mclk_hz() / 1000000UL);
        return;
    }

    /* Parse the requested core frequency in MHz (decimal). */
    p   = argv[1];
    req = 0u;
    while (*p >= '0' && *p <= '9') {
        req = req * 10u + (unsigned long)(*p - '0');
        p++;
    }
    if (*p != '\0' || req == 0u) {
        SHELL_PRINTF("Usage: freq [<mhz>]\n");
        SHELL_PRINTF("  no arg: show the core clock; <mhz>: request a frequency "
                     "(e.g. 96, or 192 for turbo).\n");
        return;
    }

    tiku_cpu_freq_init((unsigned int)req);
    now = tiku_cpu_mclk_hz() / 1000000UL;
    if (now == req) {
        SHELL_PRINTF("CPU: %lu MHz\n", now);
    } else {
        SHELL_PRINTF("CPU: %lu MHz (requested %lu not applied)\n", now, req);
    }
}
