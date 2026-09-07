/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbmsc.c - bring up the mass-storage face and report it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_usbmsc.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_io.h>
#include <arch/nordic/tiku_usbhs_arch.h>

#include <stdlib.h>
#include <string.h>

#if defined(TIKU_USBHS_MSC)

void tiku_shell_cmd_usbmsc(int argc, char **argv)
{
    if (argc < 2) {
        SHELL_PRINTF("usage: usbmsc up|down|stats|peek <lba>\n");
        return;
    }
    if (strcmp(argv[1], "up") == 0) {
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
        return;
    }
    if (strcmp(argv[1], "down") == 0) {
        tiku_nordic_usbhs_msc_stop();
        SHELL_PRINTF("usbmsc: stopped\n");
        return;
    }
    if (strcmp(argv[1], "peek") == 0 && argc >= 3) {
        uint8_t b[16];
        uint32_t lba = (uint32_t)atol(argv[2]);
        uint32_t n = tiku_nordic_usbhs_msc_peek(lba, b, sizeof b);
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

        tiku_nordic_usbhs_msc_stats(&cbw, &rd, &wr, &bad, &irq, &cfg);
        SHELL_PRINTF("usbmsc: irq=%lu configured=%u\n",
                     (unsigned long)irq, (unsigned)cfg);
        SHELL_PRINTF("      : cbw=%lu read=%lu write=%lu bad=%lu\n",
                     (unsigned long)cbw, (unsigned long)rd,
                     (unsigned long)wr, (unsigned long)bad);
        return;
    }
    SHELL_PRINTF("usage: usbmsc up|down|stats|peek <lba>\n");
}

#endif /* TIKU_USBHS_MSC */
