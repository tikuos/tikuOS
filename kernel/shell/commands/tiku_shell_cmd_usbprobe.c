/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_usbprobe.c - "usbprobe" command (nRF54LM20 USB high speed).
 *
 * Powers the block and decodes what the DWC2 core reports about itself, so
 * the device driver above it is written against the silicon's own answers
 * rather than an assumed configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_usbprobe.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_io.h>
#include <arch/nordic/tiku_usbhs_arch.h>

#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* DECODERS                                                                  */
/*---------------------------------------------------------------------------*/

/* GHWCFG2.OTGARCH: how the core moves packet data. */
static const char *arch_str(uint32_t hwcfg2)
{
    switch ((hwcfg2 >> 3) & 0x3u) {
    case 0u:  return "slave-only (FIFO reads/writes)";
    case 1u:  return "external DMA";
    case 2u:  return "internal DMA";
    default:  return "reserved";
    }
}

/* DSTS.ENUMSPD, valid once the host has finished a reset. */
static const char *speed_str(uint32_t dsts)
{
    switch ((dsts >> 1) & 0x3u) {
    case 0u:  return "high";
    case 1u:  return "full (30/60MHz PHY)";
    case 2u:  return "low";
    default:  return "full (48MHz PHY)";
    }
}

/* PHY.CLOCK.FSEL: which reference the PHY PLL was told it has. */
static const char *fsel_str(uint32_t clk)
{
    switch (clk & 0x7u) {
    case 0u:  return "19.2MHz";
    case 1u:  return "20MHz";
    case 2u:  return "24MHz";
    case 7u:  return "50MHz";
    default:  return "unlisted";
    }
}

/*---------------------------------------------------------------------------*/
/* REPORTS                                                                   */
/*---------------------------------------------------------------------------*/

/* Each stage is printed before it acts: the core stalls the bus rather than
 * faulting, so the last line out names the step that did not return. */
static void usbprobe_note(const char *stage)
{
    SHELL_PRINTF("  .. %s\n", stage);
}

static void usbprobe_regs(void)
{
    tiku_nordic_usbhs_regs_t r;
    uint32_t core_irqs = 0u, vbus_irqs = 0u, seen = 0u;

    tiku_nordic_usbhs_read(&r);
    tiku_nordic_usbhs_counts(&core_irqs, &vbus_irqs, &seen);

    SHELL_PRINTF("WRAP : enable=%lx (core=%lu phy=%lu)\n",
                 (unsigned long)r.enable, (unsigned long)(r.enable & 1u),
                 (unsigned long)((r.enable >> 1) & 1u));
    SHELL_PRINTF("PHY  : clock=%lx (fsel %s) config=%lx\n",
                 (unsigned long)r.phy_clock, fsel_str(r.phy_clock),
                 (unsigned long)r.phy_config);
    SHELL_PRINTF("PHYST: battchrgstatus=%lx (chgdet=%lu fsvplus=%lu"
                 " fsvminus=%lu)\n",
                 (unsigned long)r.phy_status,
                 (unsigned long)(r.phy_status & 1u),
                 (unsigned long)((r.phy_status >> 1) & 1u),
                 (unsigned long)((r.phy_status >> 2) & 1u));
    SHELL_PRINTF("CLK  : xo=%lu pll=%lu pclk24m=%lu\n",
                 (unsigned long)r.xo_run, (unsigned long)r.pll_run,
                 (unsigned long)r.pclk24m);
    SHELL_PRINTF("VBUS : present=%d irqs=%lu\n",
                 tiku_nordic_usbhs_vbus_present(), (unsigned long)vbus_irqs);
    if (r.core_up == 0u) {
        SHELL_PRINTF("CORE : not powered ('usbprobe up' first)\n");
        return;
    }
    SHELL_PRINTF("CORE : snpsid=%lx hwcfg=%lx %lx %lx %lx\n",
                 (unsigned long)r.snpsid, (unsigned long)r.hwcfg1,
                 (unsigned long)r.hwcfg2, (unsigned long)r.hwcfg3,
                 (unsigned long)r.hwcfg4);
    SHELL_PRINTF("     : %s, %lu device endpoints, fifo %lu words\n",
                 arch_str(r.hwcfg2),
                 (unsigned long)(((r.hwcfg2 >> 10) & 0xFu) + 1u),
                 (unsigned long)((r.hwcfg3 >> 16) & 0xFFFFu));
    SHELL_PRINTF("REGS : gintsts=%lx gahbcfg=%lx gusbcfg=%lx grstctl=%lx\n",
                 (unsigned long)r.gintsts, (unsigned long)r.gahbcfg,
                 (unsigned long)r.gusbcfg, (unsigned long)r.grstctl);
    SHELL_PRINTF("DEV  : dcfg=%lx dctl=%lx dsts=%lx (speed %s)\n",
                 (unsigned long)r.dcfg, (unsigned long)r.dctl,
                 (unsigned long)r.dsts, speed_str(r.dsts));
    SHELL_PRINTF("IRQ  : core=%lu seen=%lx\n",
                 (unsigned long)core_irqs, (unsigned long)seen);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_usbprobe(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        SHELL_PRINTF("usage: usbprobe regs|vbus|up|down|live [n]"
                     "|dev|enum|try <en> <first> [fsel]\n");
        return;
    }
    if (strcmp(argv[1], "regs") == 0) {
        usbprobe_regs();
        return;
    }
    if (strcmp(argv[1], "vbus") == 0) {
        (void)tiku_nordic_usbhs_vbus_start();
        SHELL_PRINTF("VREGUSB started; plug the nRF USB port and read"
                     " 'usbprobe regs'\n");
        return;
    }
    if (strcmp(argv[1], "up") == 0) {
        int rc;

        usbprobe_regs();          /* the safe half, before the core read */
        rc = tiku_nordic_usbhs_up(usbprobe_note);

        SHELL_PRINTF("%s\n", (rc == 0) ? "PHY + core up, core out of reset"
                                       : "core never went idle (rc -1)");
        if (rc == 0) {
            usbprobe_regs();
        }
        return;
    }
    if (strcmp(argv[1], "dev") == 0) {
        int phyif = (argc >= 3) ? (int)strtol(argv[2], (char **)0, 0) : -1;
        uint32_t trd = (argc >= 4)
                       ? (uint32_t)strtoul(argv[3], (char **)0, 0) : 0xFFu;
        uint32_t spd = (argc >= 5)
                       ? (uint32_t)strtoul(argv[4], (char **)0, 0) : 0u;
        int rc = tiku_nordic_usbhs_dev_start_cfg(phyif, trd, spd);

        SHELL_PRINTF("%s\n", (rc == 0) ? "device mode up, pull-up presented"
                                        : "core not idle (usbprobe up first)");
        return;
    }
    if (strcmp(argv[1], "enum") == 0) {
        uint32_t setup = 0u, rst = 0u, ed = 0u, spd = 0u;
        uint8_t addr = 0u, cfg = 0u;

        tiku_nordic_usbhs_dev_stats(&setup, &rst, &ed, &spd, &addr, &cfg);
        SHELL_PRINTF("ENUM : setup=%lu resets=%lu enumdone=%lu speed=%s\n",
                     (unsigned long)setup, (unsigned long)rst,
                     (unsigned long)ed, speed_str(spd << 1));
        SHELL_PRINTF("     : address=%u configuration=%u started=%u\n",
                     (unsigned)addr, (unsigned)cfg,
                     (unsigned)tiku_nordic_usbhs_dev_started());
        {
            uint8_t su[8];
            uint32_t tx = 0u, ind = 0u, outd = 0u, tz = 0u, ct = 0u, ii = 0u;

            tiku_nordic_usbhs_dev_trace(su, &tx, &ind, &outd, &tz, &ct, &ii);
            SHELL_PRINTF("SETUP: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                         su[0], su[1], su[2], su[3], su[4], su[5], su[6],
                         su[7]);
            SHELL_PRINTF("EP0  : tx=%lu in_done=%lu out_done=%lu\n",
                         (unsigned long)tx, (unsigned long)ind,
                         (unsigned long)outd);
            SHELL_PRINTF("     : dieptsiz=%lx diepctl=%lx diepint=%lx\n",
                         (unsigned long)tz, (unsigned long)ct,
                         (unsigned long)ii);
            {
                uint32_t ad = 0u, ta = 0u, al = 0u;
                uint8_t b8[8];

                tiku_nordic_usbhs_dev_dma(&ad, &ta, &al, b8);
                SHELL_PRINTF("DMA  : buf=%lx armed_len=%lu tsiz_after=%lx\n",
                             (unsigned long)ad, (unsigned long)al,
                             (unsigned long)ta);
                SHELL_PRINTF("     : bytes %02x %02x %02x %02x %02x %02x"
                             " %02x %02x\n", b8[0], b8[1], b8[2], b8[3],
                             b8[4], b8[5], b8[6], b8[7]);
            }
        }
        return;
    }
    if (strcmp(argv[1], "log") == 0) {
        uint8_t i;

        for (i = 0u; i < 8u; i++) {
            uint8_t q[8];
            uint16_t ans = 0u;

            tiku_nordic_usbhs_dev_log(i, q, &ans);
            if (q[0] == 0u && q[1] == 0u && q[6] == 0u && q[7] == 0u) {
                continue;
            }
            SHELL_PRINTF("REQ%u : %02x %02x val=%02x%02x len=%u -> %s%u\n",
                         (unsigned)i, q[0], q[1], q[3], q[2],
                         (unsigned)(q[6] | ((unsigned)q[7] << 8)),
                         (ans == 0xFFFFu) ? "STALL " : "", (unsigned)ans);
        }
        return;
    }
    if (strcmp(argv[1], "live") == 0) {
        uint32_t n = (argc >= 3)
                     ? (uint32_t)strtoul(argv[2], (char **)0, 0) : 30u;

        tiku_nordic_usbhs_live(n);
        SHELL_PRINTF("enabled and started, waited %lu settles\n",
                     (unsigned long)n);
        usbprobe_regs();
        return;
    }
    if (strcmp(argv[1], "try") == 0 && argc >= 4) {
        uint32_t en = (uint32_t)strtoul(argv[2], (char **)0, 0);
        int first = (int)strtoul(argv[3], (char **)0, 0);
        uint32_t fsel = (argc >= 5)
                        ? (uint32_t)strtoul(argv[4], (char **)0, 0) : 0xFFu;
        int rc;

        SHELL_PRINTF("try en=%lu start_first=%d fsel=%lu\n",
                     (unsigned long)en, first, (unsigned long)fsel);
        rc = tiku_nordic_usbhs_try(en, first, fsel, usbprobe_note);
        SHELL_PRINTF("%s\n", (rc == 0) ? "CORE ANSWERED" : "no ahb-idle");
        usbprobe_regs();
        return;
    }
    if (strcmp(argv[1], "down") == 0) {
        tiku_nordic_usbhs_down();
        SHELL_PRINTF("core and PHY disabled\n");
        return;
    }
    SHELL_PRINTF("usage: usbprobe regs|vbus|up|down|live [n]"
                 "|try <en> <first> [fsel]\n");
}
