/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_emmc.c - `power emmc ...` verbs.
 *
 * Split out of the power command, whose top-level verb forwards here.  The verb
 * bodies were moved verbatim and gated on a before/after diff of every verb's
 * output, so this file deliberately contains no improvements.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdint.h>
#include <kernel/shell/tiku_shell_io.h>
#include "tiku_shell_cmd_util.h"
#include "tiku_shell_cmd_emmc.h"

#if (TIKU_DRV_EMMC_ENABLE + 0)

#include <arch/ambiq/tiku_emmc_arch.h>
#if (TIKU_DRV_USB_ENABLE + 0)
/* For the ownership rule below: while the host has the card mounted over MSC,
 * board-side access is refused.  Without this include the call compiled as an
 * implicit declaration -- it linked, but assumed int-returning and would have
 * broken silently the day the signature changed. */
#include <arch/ambiq/tiku_usb_arch.h>
#endif
#include <kernel/cpu/tiku_hang.h>

/** @brief eMMC ladder tracer -- a wedged rung names itself. */
static void emmc_trace(const char *step)
{
    SHELL_PRINTF("  emmc: %s\n", step);
}

void tiku_shell_cmd_emmc(uint8_t argc, const char *argv[])
{
    /* E1/E2/E3 for the board's 8 GB eMMC (U11).
     *
     *   power emmc id     ladder + upgrade to 8-bit high speed
     *   power emmc slow   ladder ONLY (1-bit, 400 kHz) -- the E2 config,
     *                     kept so the upgrade can be PRICED, not asserted
     *   power emmc regs   host registers (power-safe)
     *   power emmc gate   write/read-back on the scratch region only
     *   power emmc bench  E3 bench: sequential, random, init cost
     *   power emmc sleep  CMD5 sleep (contents kept, bus quiet)
     *   power emmc wake   leave sleep and reselect
     *   power emmc stage <mb> [lba]
     *                     E4: stage mb megabytes card -> PSRAM tier
     *   power emmc off    release the SDIO0 domain
     */
    static const char *const en[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "CMD", "ID", "ARG", "STATE" };
    tiku_emmc_id_t id;
    tiku_emmc_err_t rc;

#if (TIKU_DRV_USB_ENABLE + 0)
    /* THE OWNERSHIP RULE.  While the host has the card mounted over MSC,
     * its filesystem driver caches blocks and assumes it is the only
     * writer.  Board-side access would corrupt that -- not "might".
     * Reads are refused too: a read here is harmless to the medium but
     * tells the operator a lie, because the host's dirty blocks have not
     * necessarily reached the card yet. */
    if (tiku_usb_msc_owns_emmc() &&
        !(argc >= 3 && tiku_cmd_streq(argv[2], "regs"))) {
        SHELL_PRINTF("emmc: refused -- the USB host owns the card"
                     " (MSC is mounted).  `power usb off` first.\n");
        return;
    }
#endif

    if (argc >= 3 && tiku_cmd_streq(argv[2], "off")) {
        tiku_emmc_deinit();
        SHELL_PRINTF("emmc: SDIO0 released (powered %d)\n",
                     tiku_emmc_powered());
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "bench")) {
        tiku_emmc_bench_run();
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "diag")) {
        tiku_emmc_diag_run();
        return;
    }
    if (argc >= 3 && (tiku_cmd_streq(argv[2], "sleep") || tiku_cmd_streq(argv[2], "wake"))) {
        /* Both rungs report the TIME they took, because that is the
         * number the lifecycle policy is decided on: sleep is only worth
         * having if waking costs far less than the ~52 ms of a full
         * bring-up.  Printing it every time keeps the claim current. */
        const int to_sleep = tiku_cmd_streq(argv[2], "sleep");
        rc = to_sleep ? tiku_emmc_sleep() : tiku_emmc_wake();
        /* Quote a duration only for an operation that actually ran --
         * printing the previous call's time beside a refusal is the
         * same species of lie as a bandwidth beside the word FAIL. */
        if (rc == TIKU_EMMC_OK) {
            SHELL_PRINTF("emmc %s: ok in %lu us  (state now %s)\n",
                         argv[2], (unsigned long)tiku_emmc_last_op_us(),
                         tiku_emmc_asleep() ? "asleep" : "up");
        } else {
            SHELL_PRINTF("emmc %s: %s (state now %s)\n", argv[2], en[rc],
                         !tiku_emmc_powered() ? "down"
                         : tiku_emmc_asleep() ? "asleep" : "up");
        }
        return;
    }
#if (TIKU_DRV_PSRAM_ENABLE + 0)
    if (argc >= 3 && tiku_cmd_streq(argv[2], "stage")) {
        uint32_t mb  = (argc >= 4) ? (uint32_t)tiku_cmd_parse_u32(argv[3]) : 1u;
        uint32_t lba = (argc >= 5) ? (uint32_t)tiku_cmd_parse_u32(argv[4]) : 0u;
        tiku_emmc_stage_run(mb, lba);
        return;
    }
#endif
    if (argc >= 3 && tiku_cmd_streq(argv[2], "regs")) {
        uint32_t g[9]; unsigned k;
        static const char *const nm[] = { "devpwrstatus","present",
            "clockctrl","hostctrl1","intstat","capabilities0",
            "response0","transfer" };
        tiku_emmc_regs(g, 8u);
        SHELL_PRINTF("emmc regs (read back):\n");
        for (k = 0u; k < 8u; k++) {
            SHELL_PRINTF("  %-14s %08lx\n", nm[k], (unsigned long)g[k]);
        }
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "gate")) {
        /* E2: write a pattern to the scratch region and read it back.
         * Single block, bit-exact, and nowhere near the card's own
         * contents. */
        static uint8_t wr[512], rd[512];
        uint32_t lba, i, errs = 0u;
        rc = tiku_emmc_read_id(&id);
        if (rc != TIKU_EMMC_OK) {
            SHELL_PRINTF("emmc gate: not identified (%s) -- run"
                         " `power emmc id` first\n", en[rc]);
            return;
        }
        lba = tiku_emmc_scratch_lba();
        for (i = 0u; i < sizeof wr; i++) {
            wr[i] = (uint8_t)((lba + i) ^ (i >> 3) ^ 0x5Au);
        }
        rc = tiku_emmc_write_blocks(lba, 1u, wr, 0);
        if (rc != TIKU_EMMC_OK) {
            uint32_t e = tiku_emmc_last_error();
            SHELL_PRINTF("emmc gate: write %s  intstat %08lx"
                         "%s%s%s%s%s\n", en[rc], (unsigned long)e,
                         (e & (1u<<16)) ? " CMD-TIMEOUT" : "",
                         (e & (1u<<17)) ? " CMD-CRC" : "",
                         (e & (1u<<19)) ? " CMD-INDEX" : "",
                         (e & (1u<<20)) ? " DATA-TIMEOUT" : "",
                         (e & (1u<<21)) ? " DATA-CRC" : "");
            return;
        }
        for (i = 0u; i < sizeof rd; i++) { rd[i] = 0u; }
        rc = tiku_emmc_read_blocks(lba, 1u, rd);
        if (rc != TIKU_EMMC_OK) {
            SHELL_PRINTF("emmc gate: read %s\n", en[rc]);
            return;
        }
        for (i = 0u; i < sizeof rd; i++) {
            if (rd[i] != wr[i]) { errs++; }
        }
        SHELL_PRINTF("emmc gate: LBA %lu, 512 B: %lu errors -- %s\n",
                     (unsigned long)lba, (unsigned long)errs,
                     errs ? "MISMATCH" : "bit-exact");
        SHELL_PRINTF("  negative: write below scratch must refuse: %s\n",
                     (tiku_emmc_write_blocks(0u, 1u, wr, 0)
                      == TIKU_EMMC_ERR_ARG) ? "REFUSED (correct)"
                                            : "ALLOWED -- BUG");
        return;
    }
    {
        /* "slow" reproduces the E2 configuration exactly.  It exists so
         * the E3 numbers can be a COMPARISON rather than a claim: the
         * same bench, the same card, one variable changed. */
        const int slow = (argc >= 3 && tiku_cmd_streq(argv[2], "slow"));
        uint32_t ladder_us, total_us;

        tiku_emmc_set_trace(emmc_trace);
        rc = slow ? tiku_emmc_init_at(1u, 400000u) : tiku_emmc_init();
        tiku_emmc_set_trace((void (*)(const char *))0);
        if (rc != TIKU_EMMC_OK) {
            uint32_t e = tiku_emmc_last_error();
            /* Show whatever identity WAS collected before the failure:
             * a partial ladder still proves how far the card answered. */
            (void)tiku_emmc_read_id(&id);
            if (id.mfr_id != 0u) {
                SHELL_PRINTF("  (partial) mfr %02x product '%s' serial"
                             " %08lx  made %u/%u\n", id.mfr_id,
                             id.product, (unsigned long)id.serial,
                             id.mfg_month, id.mfg_year);
            }
            SHELL_PRINTF("emmc init: %s  intstat %08lx\n", en[rc],
                         (unsigned long)e);
            if (e) {
                SHELL_PRINTF("  errors:%s%s%s%s%s\n",
                    (e & (1u<<16)) ? " CMD-TIMEOUT" : "",
                    (e & (1u<<17)) ? " CMD-CRC" : "",
                    (e & (1u<<18)) ? " CMD-ENDBIT" : "",
                    (e & (1u<<19)) ? " CMD-INDEX" : "",
                    (e & (1u<<20)) ? " DATA-TIMEOUT" : "");
            }
            return;
        }
        rc = tiku_emmc_read_id(&id);
        SHELL_PRINTF("emmc: mfr %02x oem %04x product '%s' rev %02x\n",
                     id.mfr_id, id.oem_id, id.product, id.rev);
        SHELL_PRINTF("  serial %08lx  made %u/%u  rca %lu\n",
                     (unsigned long)id.serial, id.mfg_month, id.mfg_year,
                     (unsigned long)id.rca);
        SHELL_PRINTF("  capacity %lu blocks = %lu MB  (ext_csd rev %u,"
                     " spec %u)\n",
                     (unsigned long)id.sec_count,
                     (unsigned long)(id.sec_count / 2048u),
                     id.ext_csd_rev, id.spec_vers);
        SHELL_PRINTF("  bus %u-bit @ %lu Hz  scratch from LBA %lu\n",
                     id.bus_width, (unsigned long)id.clock_hz,
                     (unsigned long)tiku_emmc_scratch_lba());
        /* The card's own account of its configuration -- the E3 gate.
         * EXT_CSD[183] is what the CARD thinks the bus width is; if it
         * disagrees with the host's, the two ends are out of step and
         * every data transfer is a coin flip. */
        SHELL_PRINTF("  ext_csd: bus_width %u, hs_timing %u,"
                     " device_type %02x (%s%s)\n",
                     id.ext_bus_width, id.ext_hs_timing, id.device_type,
                     (id.device_type & 1u) ? "26MHz " : "",
                     (id.device_type & 2u) ? "52MHz" : "");
        tiku_emmc_init_time(&ladder_us, &total_us);
        SHELL_PRINTF("  init: ladder %lu us, total %lu us"
                     " (upgrade %lu us)\n",
                     (unsigned long)ladder_us, (unsigned long)total_us,
                     (unsigned long)(total_us - ladder_us));
        SHELL_PRINTF("  verdict: %s\n", en[rc]);
    }
    return;

}

#endif /* TIKU_DRV_EMMC_ENABLE */
