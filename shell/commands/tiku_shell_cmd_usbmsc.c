/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbmsc.c - "usbmsc" command: present the board as a disk.
 *
 * Brings the USB-HS core of the nRF54LM20 or the ESP32-P4 up as a
 * mass-storage device, stops it, and prints its counters or the first bytes
 * of a block.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_usbmsc.h"
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#if defined(PLATFORM_ESP32P4)
#include <arch/esp32p4/tiku_usbhs_arch.h>
#else
#include <arch/nordic/tiku_usbhs_arch.h>
#endif

#include <stdlib.h>
#include <string.h>

#if defined(TIKU_USBHS_MSC)

#if defined(PLATFORM_ESP32P4)

/** @brief Present the disk; prints the outcome. */
static void msc_up(void)
{
    static const char *const why[] = {
        "disk presented", "core did not leave reset", "core never idle",
        "interrupt line taken", "FIFO layout too large", "no SRAM for the disk"
    };
    int r = tiku_p4_usbhs_msc_start();

    SHELL_PRINTF("usbmsc: %s\n", why[r <= 0 && r >= -5 ? -r : 1]);
}

/** @brief Print the PHY controls and the core's registers. */
static void msc_regs(void)
{
    tiku_p4_usbhs_regs_t r;

    tiku_p4_usbhs_read(&r);
    SHELL_PRINTF("usbmsc: clocks=%lx resets=%lx otg_ctrl=%08lx\n",
                 (unsigned long)r.clocks, (unsigned long)r.resets,
                 (unsigned long)r.otg_ctrl);
    SHELL_PRINTF("      : snpsid=%08lx hwcfg2=%08lx hwcfg3=%08lx hwcfg4=%08lx\n",
                 (unsigned long)r.snpsid, (unsigned long)r.hwcfg2,
                 (unsigned long)r.hwcfg3, (unsigned long)r.hwcfg4);
    SHELL_PRINTF("      : gotgctl=%08lx gusbcfg=%08lx grstctl=%08lx\n",
                 (unsigned long)r.gotgctl, (unsigned long)r.gusbcfg,
                 (unsigned long)r.grstctl);
    SHELL_PRINTF("      : gintsts=%08lx dsts=%08lx dctl=%08lx\n",
                 (unsigned long)r.gintsts, (unsigned long)r.dsts,
                 (unsigned long)r.dctl);
}

#define msc_down()          tiku_p4_usbhs_msc_stop()
#define msc_peek(l, b, n)   tiku_p4_usbhs_msc_peek((l), (b), (n))

#else /* nRF54LM20 */

/** @brief Present the disk; prints the outcome. */
static void msc_up(void)
{
    int r;

    if (tiku_nordic_usbhs_vbus_start() != 0) {
        SHELL_PRINTF("usbmsc: no VBUS (is the cable in?)\n");
        return;
    }
    if (tiku_nordic_usbhs_up((void (*)(const char *))0) != 0) {
        SHELL_PRINTF("usbmsc: core did not come up\n");
        return;
    }
    r = tiku_nordic_usbhs_msc_start();
    SHELL_PRINTF("usbmsc: %s\n", (r == 0) ? "disk presented" :
                 "start refused (core not idle)");
}

#define msc_down()          tiku_nordic_usbhs_msc_stop()
#define msc_peek(l, b, n)   tiku_nordic_usbhs_msc_peek((l), (b), (n))

#endif

void tiku_shell_cmd_usbmsc(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        SHELL_PRINTF("usage: usbmsc up|down|stats|peek <lba>\n");
        return;
    }
    if (strcmp(argv[1], "up") == 0) {
        msc_up();
        return;
    }
    if (strcmp(argv[1], "down") == 0) {
        msc_down();
        SHELL_PRINTF("usbmsc: stopped\n");
        return;
    }
    if (strcmp(argv[1], "peek") == 0 && argc >= 3) {
        uint8_t b[16];
        uint32_t lba = (uint32_t)atol(argv[2]);
        uint32_t n = msc_peek(lba, b, sizeof b);
        uint32_t i;

        SHELL_PRINTF("blk %lu:", (unsigned long)lba);
        for (i = 0u; i < n; i++) {
            SHELL_PRINTF(" %02x", b[i]);
        }
        SHELL_PRINTF("\n");
        return;
    }
    if (strcmp(argv[1], "stats") == 0) {
        uint32_t cbw = 0u, rd = 0u, wr = 0u, bad = 0u, irq = 0u;
        uint8_t  cfg = 0u;
#if defined(PLATFORM_ESP32P4)
        uint32_t speed = 7u;

        tiku_p4_usbhs_msc_stats(&cbw, &rd, &wr, &bad, &irq, &cfg, &speed);
#else
        tiku_nordic_usbhs_msc_stats(&cbw, &rd, &wr, &bad, &irq, &cfg);
#endif
        SHELL_PRINTF("usbmsc: irq=%lu configured=%u\n",
                     (unsigned long)irq, (unsigned)cfg);
        SHELL_PRINTF("      : cbw=%lu read=%lu write=%lu bad=%lu\n",
                     (unsigned long)cbw, (unsigned long)rd,
                     (unsigned long)wr, (unsigned long)bad);
#if defined(PLATFORM_ESP32P4)
        SHELL_PRINTF("      : speed=%s\n", speed == 0u ? "high" :
                     speed == 2u ? "low" : speed == 7u ? "none" : "full");
#endif
        return;
    }
#if defined(PLATFORM_ESP32P4)
    if (strcmp(argv[1], "regs") == 0) {
        msc_regs();
        return;
    }
#endif
    SHELL_PRINTF("usage: usbmsc up|down|stats|peek <lba>\n");
}

#endif /* TIKU_USBHS_MSC */
