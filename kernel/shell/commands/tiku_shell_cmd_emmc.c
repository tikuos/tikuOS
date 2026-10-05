/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_emmc.c - `power emmc ...` verbs.
 *
 * Identify, test, bench, sleep and stage the board's eMMC.  The power command
 * (tiku_shell_cmd_power.c) forwards `power emmc` here.
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
/* tiku_usb_msc_owns_emmc(), for the ownership check below. */
#include <arch/ambiq/tiku_usb_arch.h>
#endif
#include <kernel/cpu/tiku_hang.h>

/** @brief Init step tracer: prints each step, so a wedged init names it. */
static void emmc_trace(const char *step)
{
    SHELL_PRINTF("  emmc: %s\n", step);
}

void tiku_shell_cmd_emmc(uint8_t argc, const char *argv[])
{
    /* Verbs for the board's eMMC (U11):
     *
     *   power emmc id     init, then upgrade to an 8-bit high-speed bus
     *   power emmc slow   init only (1-bit, 400 kHz), for comparison
     *   power emmc hs200  switch to HS200; a failed switch falls back to HS 48
     *   power emmc regs   host registers (safe while powered down)
     *   power emmc gate   write and read back one scratch-region block
     *   power emmc bench  sequential, random-block and init-cost bench
     *   power emmc diag   read-path diagnostic
     *   power emmc sleep  CMD5 sleep (contents kept, bus quiet)
     *   power emmc wake   leave sleep and reselect
     *   power emmc stage <mb> [lba]
     *                     stage mb megabytes card -> PSRAM tier (PSRAM builds)
     *   power emmc off    release the SDIO0 domain
     *
     * No verb runs id; any other verb prints the usage.
     */
    /* Names for tiku_emmc_err_t, indexed by value: keep in enum order. */
    static const char *const en[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "CMD", "ID", "ARG", "STATE", "NOMEM" };
    tiku_emmc_id_t id;
    tiku_emmc_err_t rc;

#if (TIKU_DRV_USB_ENABLE + 0)
    /* While the host has the card mounted over MSC, its filesystem driver
     * caches blocks and assumes it is the only writer, so board-side access
     * is refused.  Reads are refused too: the host's dirty blocks may not
     * have reached the card, so a read could return stale data.  Only regs,
     * which reads host registers and not the card, is allowed. */
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
    if (argc >= 3 && tiku_cmd_streq(argv[2], "hs200")) {
        tiku_emmc_err_t rc = tiku_emmc_hs200();
        SHELL_PRINTF("emmc hs200: %s\n",
                     (rc == TIKU_EMMC_OK) ? "ok" : "failed (still usable at HS 48)");
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
        /* Both print how long they took, for comparing a wake with a
         * full init. */
        const int to_sleep = tiku_cmd_streq(argv[2], "sleep");
        rc = to_sleep ? tiku_emmc_sleep() : tiku_emmc_wake();
        /* Only a success prints a duration; a call that finds the card
         * already in the requested state succeeds at once and prints 0. */
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
        /* Write a pattern to one block of the scratch region and read it
         * back bit-exact; the card's own contents are never touched.  A
         * write below the scratch region must then be refused. */
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
        /* "slow" brings the card up at 1-bit, 400 kHz, for comparing the
         * bench with the default bus; "id" or no verb runs the default
         * init. */
        const int slow = (argc >= 3 && tiku_cmd_streq(argv[2], "slow"));
        uint32_t ladder_us, total_us;

        if (argc >= 3 && !slow && !tiku_cmd_streq(argv[2], "id")) {
            SHELL_PRINTF("usage: power emmc [id|slow|hs200|regs|gate|bench"
                         "|diag|sleep|wake"
#if (TIKU_DRV_PSRAM_ENABLE + 0)
                         "|stage <mb> [lba]"
#endif
                         "|off]\n");
            return;
        }

        tiku_emmc_set_trace(emmc_trace);
        rc = slow ? tiku_emmc_init_at(1u, 400000u) : tiku_emmc_init();
        tiku_emmc_set_trace((void (*)(const char *))0);
        if (rc != TIKU_EMMC_OK) {
            uint32_t e = tiku_emmc_last_error();
            /* Print whatever identity was collected before the failure:
             * it shows how far through init the card answered. */
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
        /* EXT_CSD[183] and [185] are the card's view of the bus width and
         * timing, read back after any switch; if they disagree with the
         * host's, data transfers are unreliable. */
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
