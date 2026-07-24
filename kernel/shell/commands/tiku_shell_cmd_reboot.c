/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_reboot.c - "reboot" command implementation
 *
 * Triggers a system reset by configuring the watchdog timer in watchdog
 * mode with the shortest available interval, then spinning until the
 * hardware resets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_reboot.h"
#include <kernel/shell/tiku_shell.h>             /* SHELL_PRINTF */
#include <kernel/cpu/tiku_watchdog.h>
#if defined(PLATFORM_RP2350)
extern void tiku_cpu_rp2350_reboot_to_bootsel(void);
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_reboot(uint8_t argc, const char *argv[])
{
#if defined(PLATFORM_RP2350)
    if (argc >= 2 && argv[1] != (const char *)0 &&
        argv[1][0] == 'b' && argv[1][1] == 'o' && argv[1][2] == 'o' &&
        argv[1][3] == 't') {
        SHELL_PRINTF("Rebooting to BOOTSEL...\n");
        tiku_cpu_rp2350_reboot_to_bootsel();
        /* If we return, the bootrom call failed; fall through to plain
         * watchdog reset below so the device at least reboots. */
    }
#endif
    (void)argc;
    (void)argv;

    SHELL_PRINTF("Rebooting...\n");

    /*
     * Configure the watchdog in watchdog mode (reset on expiry) with the
     * shortest interval (/64 divider on ACLK ~32 kHz -> ~2 ms).
     * start_held=0, kick_on_start=1: counter starts immediately from zero.
     */
    tiku_watchdog_config(TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_SRC_ACLK,
                         TIKU_WDT_INTERVAL_64, 0, 1);

    /* Spin until the watchdog fires the reset */
    for (;;) {
        /* empty */
    }
}
