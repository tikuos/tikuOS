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
 * hardware resets.  ESP32-C61 resets through tiku_cpu_esp32c61_restart().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_reboot.h"
#include <shell/tiku_shell.h>             /* SHELL_PRINTF */
#include <kernel/cpu/tiku_watchdog.h>
#if defined(PLATFORM_RP2350)
extern void tiku_cpu_rp2350_reboot_to_bootsel(void);
#endif
#if defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_cpu_common.h>
#endif
#if defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_cpu_common.h>
#endif
#if defined(PLATFORM_AMBIQ)
#include <arch/ambiq/tiku_cpu_freq_boot_arch.h>
#if (TIKU_DRV_EMMC_ENABLE + 0)
#include <arch/ambiq/tiku_emmc_arch.h>
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
#include <arch/ambiq/tiku_psram_arch.h>
#endif
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
        /* A return means the bootrom call failed; the watchdog reset
         * below runs instead. */
    }
#endif
    (void)argc;
    (void)argv;

    SHELL_PRINTF("Rebooting...\n");

#if defined(PLATFORM_AMBIQ)
    /* Return to a power-on-equivalent state before the reset.  A warm
     * reset does not power-cycle the eMMC die, the PSRAM die or the
     * always-on power state, and a warm reset from the fully brought-up
     * state (CPU HP, eMMC HS200, PSRAM up) can hang the secure bootloader
     * with SWD unable to attach until a power cycle.  Each step ignores
     * failure, so the reset always follows. */
#if (TIKU_DRV_EMMC_ENABLE + 0)
    if (tiku_emmc_powered()) {
        (void)tiku_emmc_sleep();       /* card quiescent: no lines driven  */
        tiku_emmc_deinit();            /* host clock + bus power + domain  */
    }
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
    if (tiku_psram_powered()) {
        (void)tiku_psram_xip_enable(0);   /* aperture off before PIO       */
        (void)tiku_psram_device_reset();  /* device to power-up defaults   */
        tiku_psram_deinit();              /* MSPI clock + power domain     */
    }
#endif
    tiku_cpu_freq_ambiq_init(96u);     /* HP -> LP, the SBL's own state    */
#endif
#if defined(PLATFORM_ESP32C61)
    /* A reset that cuts a cache fetch short hangs the C61's ROM.  This
     * call stops the cache and resets from code in SRAM, since this command
     * may run from flash; it does not return. */
    tiku_cpu_esp32c61_restart(1);
#endif
#if defined(PLATFORM_ESP32C5)
    tiku_cpu_c5_restart();
#endif

    /*
     * Configure the watchdog in watchdog mode (reset on expiry) with its
     * shortest interval, TIKU_WDT_INTERVAL_64 (about 2 ms from a 32 kHz
     * ACLK on MSP430).  start_held=0, kick_on_start=1: the count starts at
     * once from zero.
     */
    tiku_watchdog_config(TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_SRC_ACLK,
                         TIKU_WDT_INTERVAL_64, 0, 1);

    /* Spin until the watchdog fires the reset */
    for (;;) {
        /* empty */
    }
}
