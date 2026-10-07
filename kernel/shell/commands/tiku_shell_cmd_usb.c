/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usb.c - `power usb ...` verbs.
 *
 * Bring-up, enumeration and mass-storage verbs for the Apollo510 USB device
 * controller; tiku_shell_cmd_power.c forwards the `usb` verb here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdint.h>
#include <kernel/shell/tiku_shell_io.h>
#include "tiku_shell_cmd_util.h"
#include "tiku_shell_cmd_usb.h"

#if (TIKU_DRV_USB_ENABLE + 0)

#include <arch/ambiq/tiku_usb_arch.h>
#include <arch/ambiq/tiku_emmc_arch.h>   /* MSC-on-eMMC block count */
#include <kernel/cpu/tiku_hang.h>

void tiku_shell_cmd_usb(uint8_t argc, const char *argv[])
{
    /* argv[2] selects the verb; anything unmatched prints the state.
     *
     *   power usb up [hs] [msc | emmc]
     *                      power both domains, clock the PHY, arm EP0.
     *                      default full speed + CDC console; "hs"
     *                      requests high speed, "msc" presents a RAM
     *                      disk instead of the console, "emmc" the card
     *   power usb attach   soft-connect; the host can then enumerate
     *   power usb detach   soft-disconnect
     *   power usb state    what the host has done so far
     *   power usb regs     USB registers; only DEVPWRSTATUS is read while
     *                      the USB domain is off
     *   power usb console  move the shell onto the CDC pipes
     *   power usb uart     move the shell back to the UART
     *   power usb hash [n] MSC counters and FNV-1a of the RAM disk
     *   power usb selftest MSC LBA bounds-check cases
     *   power usb adma on|off
     *                      MSC data path by ADMA or PIO
     *   power usb sink [ms]
     *                      count CDC input until ms of idle
     *   power usb off      release the rails and both domains
     *
     * `up` leaves the device detached; the host sees it only after
     * `attach`.
     */
    static const char *const un[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "ARG", "STATE", "FIFO" };
    if (argc >= 3 && tiku_cmd_streq(argv[2], "up")) {
        /* Full speed unless `hs` is given.  tiku_usb_up_full() takes the
         * PHY's reference clock from a different source for each speed, and
         * sets HSENAB in the POWER register only for high speed. */
        int hs = 0, msc = 0, emmc = 0, k;
        tiku_usb_err_t rc;
        for (k = 3; k < argc; k++) {
            if (tiku_cmd_streq(argv[k], "hs"))   { hs = 1; }
            if (tiku_cmd_streq(argv[k], "msc"))  { msc = 1; }
            if (tiku_cmd_streq(argv[k], "emmc")) { msc = 1; emmc = 1; }
        }
        rc = tiku_usb_up_full(hs ? TIKU_USB_SPEED_HIGH
                                 : TIKU_USB_SPEED_FULL,
                              msc ? TIKU_USB_CLASS_MSC
                                  : TIKU_USB_CLASS_CDC, emmc);
        SHELL_PRINTF("usb up (%s speed, %s%s): %s (powered %d)\n",
                     hs ? "high" : "full", msc ? "MSC" : "CDC",
                     emmc ? " on eMMC" : (msc ? " on RAM" : ""),
                     un[(unsigned)rc < 7u ? (unsigned)rc : 0u],
                     tiku_usb_powered());
        if (rc == TIKU_USB_ERR_STATE && emmc) {
            SHELL_PRINTF("  (the card must be identified first:"
                         " `power emmc id`)\n");
        }
#if (TIKU_DRV_EMMC_ENABLE + 0)
        /* Without the eMMC driver tiku_usb_up_full() refuses `emmc` with
         * TIKU_USB_ERR_ARG, so this report is compiled only with it. */
        if (emmc && rc == TIKU_USB_OK) {
            uint32_t cbw, rd, wr, blocks;
            tiku_usb_msc_stats(&cbw, &rd, &wr, &blocks);
            SHELL_PRINTF("  host will see %lu blocks (%lu MB) -- the top"
                         " %u are withheld so the scratch region stays"
                         " outside any filesystem\n",
                         (unsigned long)blocks,
                         (unsigned long)(blocks / 2048u),
                         TIKU_EMMC_SCRATCH_BLOCKS);
        }
#endif
        return;
    }
    if (argc >= 3 && (tiku_cmd_streq(argv[2], "attach") ||
                      tiku_cmd_streq(argv[2], "detach"))) {
        int on = tiku_cmd_streq(argv[2], "attach");
        tiku_usb_err_t rc = tiku_usb_attach(on);
        SHELL_PRINTF("usb %s: %s\n", argv[2],
                     un[(unsigned)rc < 7u ? (unsigned)rc : 0u]);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "console")) {
        /* The announcement goes out on the current backend, before the
         * switch. */
        if (!tiku_usb_cdc_ready()) {
            SHELL_PRINTF("usb console: refused -- not configured, or the"
                         " host has not opened the port (DTR clear)\n");
            return;
        }
        SHELL_PRINTF("usb console: switching; the shell now speaks on"
                     " /dev/ttyACM* -- `power usb uart` to come back\n");
        tiku_shell_io_set_backend(&tiku_shell_io_usbcdc);
        SHELL_PRINTF("\ntikuOS shell on USB CDC.\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "uart")) {
        SHELL_PRINTF("usb console: handing back to the UART\n");
        tiku_shell_io_set_backend(&tiku_shell_io_uart);
        SHELL_PRINTF("\ntikuOS shell back on UART.\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "selftest")) {
        uint32_t bad = tiku_usb_msc_selftest();
        SHELL_PRINTF("usb msc bounds check: %s (mask %08lx)\n",
                     bad ? "FAILED" : "all 8 cases correct",
                     (unsigned long)bad);
        SHELL_PRINTF("  includes the overflow pair (lba+nblk wraps to 0)"
                     " that defeats the naive check\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "adma") && argc >= 4) {
        int on = tiku_cmd_parse_on_off(argv[3]);
        if (on < 0) { SHELL_PRINTF("Usage: power usb adma on|off\n");
                      return; }
        tiku_usb_msc_adma(on);
        SHELL_PRINTF("usb adma: %s (off = PIO, for the comparison)\n",
                     tiku_usb_msc_adma_on() ? "on" : "off");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "hash")) {
        uint32_t nb = (argc >= 4) ? tiku_cmd_parse_u32(argv[3]) : 0u;
        uint32_t cbw, rd, wr, blocks;
        tiku_usb_msc_stats(&cbw, &rd, &wr, &blocks);
        SHELL_PRINTF("usb disk: %lu blocks x 512 B  cbw %lu  rd %lu"
                     "  wr %lu\n", (unsigned long)blocks,
                     (unsigned long)cbw, (unsigned long)rd,
                     (unsigned long)wr);
        {
            uint32_t x, e;
            tiku_usb_msc_dma_stats(&x, &e);
            SHELL_PRINTF("  adma %s: %lu transfers, %lu errors\n",
                         tiku_usb_msc_adma_on() ? "on" : "off",
                         (unsigned long)x, (unsigned long)e);
        }
        nb = tiku_usb_msc_hash_blocks(nb);
        SHELL_PRINTF("usb RAM-disk hash (%lu blocks): %08lx\n",
                     (unsigned long)nb,
                     (unsigned long)tiku_usb_msc_hash(nb));
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "sink")) {
        uint32_t ms = (argc >= 4) ? tiku_cmd_parse_u32(argv[3]) : 2000u;
        uint32_t n;
        SHELL_PRINTF("usb sink: draining until %lu ms idle...\n",
                     (unsigned long)ms);
        n = tiku_usb_cdc_sink(ms);
        SHELL_PRINTF("usb sink: %lu bytes\n", (unsigned long)n);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "off")) {
        tiku_usb_down();
        SHELL_PRINTF("usb: released (powered %d)\n", tiku_usb_powered());
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "regs")) {
        uint32_t g[9]; unsigned k;
        static const char *const nm[] = { "devpwrstatus", "usbrstctrl",
            "clkctrl", "power", "faddr", "intrusbe", "intrtxe", "frame",
            "phy_reg14" };
        tiku_usb_regs(g, 9u);
        SHELL_PRINTF("usb regs (read back):\n");
        for (k = 0u; k < 9u; k++) {
            SHELL_PRINTF("  %-13s %08lx\n", nm[k], (unsigned long)g[k]);
        }
        return;
    }
    {
        /* Default: print the state.  The ISR counts events, and the
         * counters show where enumeration stopped. */
        tiku_usb_counters_t c;
        static const char *const sp[] = { "none", "full", "high" };
        tiku_usb_speed_t spd = tiku_usb_speed();
        tiku_usb_counters(&c);
        SHELL_PRINTF("usb: powered %d, attached %d, speed %s"
                     " (requested %s)\n",
                     tiku_usb_powered(), tiku_usb_attached(),
                     sp[(unsigned)spd < 3u ? (unsigned)spd : 0u],
                     sp[(unsigned)tiku_usb_want() < 3u
                        ? (unsigned)tiku_usb_want() : 0u]);
        SHELL_PRINTF("  address %u  config %u\n",
                     tiku_usb_address(), tiku_usb_config());
        SHELL_PRINTF("  irq %lu  reset %lu  setup %lu  stall %lu\n",
                     (unsigned long)c.irq, (unsigned long)c.reset,
                     (unsigned long)c.setup, (unsigned long)c.stall);
        SHELL_PRINTF("  setupend %lu  suspend %lu  resume %lu"
                     "  last_req %04x\n",
                     (unsigned long)c.setupend, (unsigned long)c.suspend,
                     (unsigned long)c.resume, c.last_req);
        SHELL_PRINTF("  stalled: %04x %04x %04x %04x"
                     "  (0606=DEVICE_QUALIFIER, 0607=OTHER_SPEED --"
                     " both correct to refuse at full speed)\n",
                     c.stalled[0], c.stalled[1], c.stalled[2],
                     c.stalled[3]);
        {
            uint32_t tx, rx, drop, nak;
            tiku_usb_cdc_stats(&tx, &rx, &drop, &nak);
            SHELL_PRINTF("  cdc: configured %d dtr %d  tx %lu B  rx %lu B"
                         "  drops %lu  naks %lu\n",
                         tiku_usb_config() ? 1 : 0, tiku_usb_cdc_ready(),
                         (unsigned long)tx, (unsigned long)rx,
                         (unsigned long)drop, (unsigned long)nak);
        }
        if (c.irq == 0u) {
            SHELL_PRINTF("  (irq 0: no interrupt has fired -- check the"
                         " cable is in J18, not the J-Link J16)\n");
        }
    }
    return;

}

#endif /* TIKU_DRV_USB_ENABLE */
