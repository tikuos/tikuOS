/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbhs.c - "usb" and "store" commands (RA8P1 USB-HS).
 *
 * Brings the device controller up as a disk the host can write, and reports
 * what the flash-backed model store holds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdint.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include "tiku_shell_cmd_util.h"
#include "tiku_shell_cmd_usbhs.h"

#if (TIKU_SHELL_CMD_USBHS + 0)

#include <arch/ra8p1/tiku_usbhs_arch.h>
#include <arch/ra8p1/tiku_store_arch.h>

/*
 * The MSC pump runs as a shell pump, in process context: a SCSI command can
 * take milliseconds.  EP0 is served from the interrupt, so enumeration
 * completes while the shell is busy.
 */

/**
 * @brief Serve pending SCSI work, then advance a running store import by one
 *        erase sector.
 *
 * Host writes are refused while an import runs; the disk stays attached.
 */
static void usbhs_pump(void)
{
    tiku_ra8p1_usbhs_msc_poll();
    (void)tiku_ra8p1_store_step(NULL);
}

/**
 * @brief Handle `usb up|down|info`.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_usb(uint8_t argc, const char *argv[])
{
    static const char *const dev[5] = {
        "powered", "default", "address", "configured", "suspend"
    };
    static const char *const spd[3] = { "none", "full", "high" };

    if (argc >= 2u && tiku_cmd_streq(argv[1], "up")) {
        int rc = tiku_ra8p1_usbhs_up(1);

        if (rc != TIKU_RA8P1_USBHS_OK) {
            SHELL_PRINTF("usb: bring-up failed (%d)\n", rc);
            return;
        }
        (void)tiku_shell_add_pump(usbhs_pump);
        (void)tiku_ra8p1_usbhs_attach(1);
        SHELL_PRINTF("usb: attached; the host should find a 64 MB disk\n");
        return;
    }

    if (argc >= 2u && tiku_cmd_streq(argv[1], "down")) {
        (void)tiku_ra8p1_usbhs_attach(0);
        tiku_shell_remove_pump(usbhs_pump);
        tiku_ra8p1_usbhs_down();
        SHELL_PRINTF("usb: detached\n");
        return;
    }

    if (argc < 2u || tiku_cmd_streq(argv[1], "info")) {
        uint32_t irqs = 0, dvst = 0, c = 0, rd = 0, wr = 0, bad = 0;
        unsigned d, s;

        if (!tiku_ra8p1_usbhs_up_state()) {
            SHELL_PRINTF("usb: down  (`usb up` to attach)\n");
            return;
        }
        d = (unsigned)tiku_ra8p1_usbhs_devstate();
        s = (unsigned)tiku_ra8p1_usbhs_speed();
        tiku_ra8p1_usbhs_irq_stats(&irqs, &dvst);
        tiku_ra8p1_usbhs_msc_stats(&c, &rd, &wr, &bad);

        SHELL_PRINTF("usb: %s at %s speed, address %u, config %u\n",
                     dev[d < 5u ? d : 4u], spd[s < 3u ? s : 0u],
                     (unsigned)tiku_ra8p1_usbhs_address(),
                     (unsigned)tiku_ra8p1_usbhs_configured());
        SHELL_PRINTF("     irq %lu (%lu bus events), pll %s\n",
                     (unsigned long)irqs, (unsigned long)dvst,
                     tiku_ra8p1_usbhs_pll_locked() ? "locked" : "UNLOCKED");
        SHELL_PRINTF("     scsi: %lu commands, %lu read, %lu write,"
                     " %lu refused\n",
                     (unsigned long)c, (unsigned long)rd,
                     (unsigned long)wr, (unsigned long)bad);
        return;
    }

    SHELL_PRINTF("usage: usb up|down|info\n");
}

/**
 * @brief Handle `store info`.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_store(uint8_t argc, const char *argv[])
{
    char name[TIKU_STORE_NAME_MAX + 1u];
    uint32_t len = 0;

    (void)argc;
    (void)argv;

    if (tiku_ra8p1_store_busy()) {
        SHELL_PRINTF("store: importing...\n");
        return;
    }
    if (!tiku_ra8p1_store_info(name, &len)) {
        SHELL_PRINTF("store: empty\n");
        SHELL_PRINTF("       write a model to the disk, then a commit record"
                     " to block %lu\n",
                     (unsigned long)tiku_ra8p1_store_commit_lba());
        return;
    }
    /* tiku_ra8p1_store_verify() recomputes the CRC over the stored bytes
     * and compares it with the header's. */
    SHELL_PRINTF("store: \"%s\", %lu bytes, crc %s\n", name,
                 (unsigned long)len,
                 tiku_ra8p1_store_verify() ? "ok" : "MISMATCH");
}

#endif /* TIKU_SHELL_CMD_USBHS */
