/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_nor.c - `power nor ...` verbs.
 *
 * Bring-up and test verbs for the Apollo510 EVB's 8 MB octal NOR (U12);
 * tiku_shell_cmd_power.c forwards the `nor` verb here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdint.h>
#include <kernel/shell/tiku_shell_io.h>
#include "tiku_shell_cmd_util.h"
#include "tiku_shell_cmd_nor.h"

#if (TIKU_DRV_NOR_ENABLE + 0)

#include <arch/ambiq/tiku_nor_arch.h>
#include <kernel/cpu/tiku_hang.h>

/** @brief Print one NOR bring-up step; the last one printed names a hang. */
static void nor_trace(const char *step)
{
    SHELL_PRINTF("  nor step: %s\n", step);
}

/** @brief Name of a tiku_nor_err_t code, or "?" for an unknown one. */
static const char *nor_errname(tiku_nor_err_t rc)
{
    static const char *const en[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "ID", "ARG", "STATE", "PROGRAM" };
    return ((unsigned)rc < 8u) ? en[rc] : "?";
}

void tiku_shell_cmd_nor(uint8_t argc, const char *argv[])
{
    /* argv[2] selects the verb; anything unmatched reads identity.
     *
     *   power nor id [octal]  serial bring-up + identity; "octal" also
     *                         switches to octal DDR and re-reads it
     *   power nor fault       the same, with the D0 pad set to GPIO for
     *                         the read, which is expected to report an
     *                         error
     *   power nor gate        erase, program and verify the scratch
     *                         sector, leaving a fixed stamp there
     *   power nor verify      re-read the stamp without writing, after
     *                         a reboot or a load-switch cycle
     *   power nor off         load switch off: VDD_FLASH at zero
     *   power nor lson        load switch on, then bit-bang READ_ID
     *   power nor erases      erases spent this boot
     *   power nor xip         read one word through the XIP aperture
     *   power nor bench [octal] [xip] [sector]
     *                         tiku_nor_bench_run(), optionally in octal
     *                         DDR; `xip` adds the XIP leg and `sector`
     *                         the 128 KB sector-erase leg
     *   power nor tascan      serial dummy-count sweep against the stamp
     *   power nor hears       whether the part parses octal commands
     *   power nor arraycmp [addr]
     *                         octal array read against serial
     *   power nor scan        RX DQS delay sweep, DQS on and off
     *   power nor forceoctal  identity with only the controller in octal
     *   power nor bbtest      self-test of the bit-bang read path
     *   power nor bb          bit-bang READ_ID, controller off the pads
     *   power nor regs        controller register snapshot
     *   power nor ls really   refused
     */
    tiku_nor_id_t id;
    tiku_nor_err_t rc;

    if (argc >= 3 && tiku_cmd_streq(argv[2], "xip")) {
        uint32_t w = 0u;
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor xip: bring-up %s\n", nor_errname(rc));
            return;
        }
        SHELL_PRINTF("nor xip: opening the aperture and reading one word --"
                     " if this is the last line, the bus stalled\n");
        if (tiku_nor_xip_probe(&w) != 0) {
            SHELL_PRINTF("  aperture would not open\n");
            return;
        }
        SHELL_PRINTF("  read %08lx at %08lx (stamp a5 a4 a7 a6 little-endian after"
                     " `power nor gate`)\n", (unsigned long)w,
                     (unsigned long)(TIKU_NOR_XIP_BASE + TIKU_NOR_SCRATCH_ADDR));
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "bench")) {
        int want_oct = (argc >= 4 && tiku_cmd_streq(argv[3], "octal"));
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc == TIKU_NOR_OK && want_oct) {
            rc = tiku_nor_enter_octal_raw(TIKU_NOR_CLK_96MHZ);
        }
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("norbench: bring-up %s\n", nor_errname(rc));
            return;
        }
        {   /* argv[3] may be `octal`; any trailing word may be `xip` or
             * `sector` */
            int k5, xip = 0, sec = 0;
            for (k5 = 3; k5 < argc; k5++) {
                if (tiku_cmd_streq(argv[k5], "xip"))    { xip = 1; }
                if (tiku_cmd_streq(argv[k5], "sector")) { sec = 1; }
            }
            tiku_nor_bench_set_xip(xip);
            tiku_nor_bench_set_sector(sec);
        }
        tiku_nor_bench_run();
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "tascan")) {
        /* Sweep the serial fast-read dummy count against the 0xA5^i stamp
         * that `gate` programs.  Erased content cannot be the reference: a
         * misframed read of all-FF is still all-FF. */
        static uint8_t want[32];
        uint32_t i8, mask;
        unsigned t8;

        for (i8 = 0u; i8 < sizeof want; i8++) {
            want[i8] = (uint8_t)(0xA5u ^ i8);
        }
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor tascan: serial init %s\n", nor_errname(rc));
            return;
        }
        mask = tiku_nor_scan_turnaround(TIKU_NOR_SCRATCH_ADDR, want,
                                        sizeof want);
        SHELL_PRINTF("nor serial turnaround sweep @%08lx: %08lx\n",
                     (unsigned long)TIKU_NOR_SCRATCH_ADDR,
                     (unsigned long)mask);
        if (mask == 0u) {
            SHELL_PRINTF("  no dummy count reproduces the stamp -- run"
                         " `power nor gate` first to put it there\n");
        } else {
            SHELL_PRINTF("  matches at:");
            for (t8 = 0u; t8 < 32u; t8++) {
                if (mask & (1u << t8)) { SHELL_PRINTF(" %u", t8); }
            }
            SHELL_PRINTF("\n");
        }
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "hears")) {
        int heard;
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc == TIKU_NOR_OK) {
            rc = tiku_nor_enter_octal_raw(TIKU_NOR_CLK_24MHZ);
        }
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor hears: octal entry %s\n", nor_errname(rc));
            return;
        }
        heard = tiku_nor_octal_hears();
        SHELL_PRINTF("nor hears: %s\n",
            heard == 1 ? "YES -- octal reset landed, the device parses octal"
                         " commands; only the READ path is broken"
          : heard == 0 ? "NO -- the device ignores octal commands, so it is"
                         " not in octal DDR despite leaving serial"
                       : "not in octal");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "arraycmp")) {
        /* Compare an octal array read with a serial read of the same
         * address.  Identity is a register read, and the part may strobe DQS
         * for array reads but not for register reads, so this verb tests
         * the array path directly. */
        static uint8_t ser[64], oct[64];
        uint32_t addr = 0u, i7;
        int same = 1, ser_blank = 1;

        if (argc >= 4) { addr = tiku_cmd_parse_u32(argv[3]); }
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc == TIKU_NOR_OK) { rc = tiku_nor_read(addr, ser, sizeof ser); }
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor arraycmp: serial read %s\n", nor_errname(rc));
            return;
        }
        for (i7 = 0u; i7 < sizeof ser; i7++) {
            if (ser[i7] != 0xFFu) { ser_blank = 0; }
        }
        SHELL_PRINTF("nor arraycmp @%08lx: serial %02x %02x %02x %02x"
                     " %02x %02x %02x %02x%s\n", (unsigned long)addr,
                     ser[0], ser[1], ser[2], ser[3],
                     ser[4], ser[5], ser[6], ser[7],
                     ser_blank ? "  (erased -- pick an address with content"
                                 " or this proves little)" : "");

        rc = tiku_nor_enter_octal_raw(TIKU_NOR_CLK_24MHZ);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("  octal entry %s\n", nor_errname(rc));
            return;
        }
        rc = tiku_nor_read(addr, oct, sizeof oct);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("  octal read %s -- the ARRAY path fails too\n",
                         nor_errname(rc));
            return;
        }
        for (i7 = 0u; i7 < sizeof oct; i7++) {
            if (oct[i7] != ser[i7]) { same = 0; }
        }
        SHELL_PRINTF("  octal  %02x %02x %02x %02x %02x %02x %02x %02x\n",
                     oct[0], oct[1], oct[2], oct[3],
                     oct[4], oct[5], oct[6], oct[7]);
        SHELL_PRINTF("  verdict: %s\n", same
            ? "MATCH -- octal array reads work; identity is the wrong probe"
            : "DIFFER -- sweeping the array turnaround against serial");
        if (!same) {
            uint32_t tmask = tiku_nor_scan_turnaround(addr, ser, 32u);
            unsigned t7;
            SHELL_PRINTF("  turnaround sweep: %08lx",
                         (unsigned long)tmask);
            if (tmask == 0u) {
                SHELL_PRINTF("  -- no dummy count reproduces serial\n");
            } else {
                SHELL_PRINTF("  -- matches at:");
                for (t7 = 0u; t7 < 32u; t7++) {
                    if (tmask & (1u << t7)) { SHELL_PRINTF(" %u", t7); }
                }
                SHELL_PRINTF("\n");
            }
        }
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "scan")) {
        uint32_t mask;
        unsigned d;
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc == TIKU_NOR_OK) {
            rc = tiku_nor_enter_octal_raw(TIKU_NOR_CLK_24MHZ);
        }
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor scan: octal entry %s\n", nor_errname(rc));
            return;
        }
        mask = tiku_nor_scan_rxdqs(1);
        SHELL_PRINTF("nor rxdqs scan @%lu Hz: dqs-on %08lx",
                     tiku_nor_clock_hz(), (unsigned long)mask);
        {   /* With DQS on, no delay setting captures the identity of a part
             * that does not strobe DQS for register reads; the dqs-off sweep
             * latches the data on the controller clock instead. */
            uint32_t nodqs = tiku_nor_scan_rxdqs(0);
            SHELL_PRINTF("  dqs-off %08lx\n", (unsigned long)nodqs);
            mask |= nodqs;
        }
        if (mask == 0u) {
            SHELL_PRINTF("  nothing captures an ID either way -- the fault is"
                         " not DQS capture timing\n");
            return;
        }
        SHELL_PRINTF("  good delays:");
        for (d = 0u; d < 32u; d++) {
            if (mask & (1u << d)) { SHELL_PRINTF(" %u", d); }
        }
        SHELL_PRINTF("\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "forceoctal")) {
        /* Configure only the controller for octal and read identity: a
         * part in serial mode stays silent, one already in octal answers. */
        unsigned k9;
        static const unsigned rows[3] = { TIKU_NOR_CLK_24MHZ,
                                          TIKU_NOR_CLK_48MHZ,
                                          TIKU_NOR_CLK_96MHZ };
        for (k9 = 0u; k9 < 3u; k9++) {
            rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
            if (rc != TIKU_NOR_OK) { continue; }
            rc = tiku_nor_force_octal(rows[k9]);
            if (rc != TIKU_NOR_OK) { continue; }
            rc = tiku_nor_read_id(&id);
            SHELL_PRINTF("  forced octal @%lu Hz: mfr %02x type %02x"
                         " cap %02x -- %s\n", tiku_nor_clock_hz(),
                         id.mfr, id.type, id.capacity, nor_errname(rc));
        }
        SHELL_PRINTF("  (all 00 in every row = the part is not answering"
                     " octal either)\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "bbtest")) {
        uint32_t t9 = tiku_nor_bitbang_selftest();
        SHELL_PRINTF("nor bbtest: %02lx -- drive-low reads %lu,"
                     " drive-high reads %lu, D1 floating %lu,"
                     " D0 floating %lu\n", (unsigned long)t9,
                     (unsigned long)(t9 & 1u), (unsigned long)((t9>>1)&1u),
                     (unsigned long)((t9>>2)&1u),
                     (unsigned long)((t9>>3)&1u));
        SHELL_PRINTF("  CE lo/hi %lu/%lu  CLK lo/hi %lu/%lu"
                     "  RST hi %lu  LSEN hi %lu\n",
                     (unsigned long)((t9>>4)&1u), (unsigned long)((t9>>5)&1u),
                     (unsigned long)((t9>>6)&1u), (unsigned long)((t9>>7)&1u),
                     (unsigned long)((t9>>8)&1u), (unsigned long)((t9>>9)&1u));
        SHELL_PRINTF("  instrument %s\n",
                     ((t9 & 3u) == 2u) ? "WORKS (0 then 1)"
                                       : "BROKEN -- ff verdicts are void");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "lson")) {
        /* Enable the flash's load switch, then read the device ID by
         * bit-bang with the controller off the pads.
         *
         * The switch is an NCP451FCT2G, an inrush-limited part with a
         * 100 kohm pull-down on its enable: the flash is unpowered until the
         * pad is driven high, and a bit-bang read of it returns all ff.
         * This verb never drives the pad low. */
        static uint8_t idb[8];
        unsigned k8;
        tiku_nor_deinit();
        tiku_nor_ls_set(1);
        tiku_nor_bitbang_id(idb, 8u);
        SHELL_PRINTF("nor loadsw HIGH, bitbang READ_ID:");
        for (k8 = 0u; k8 < 8u; k8++) { SHELL_PRINTF(" %02x", idb[k8]); }
        SHELL_PRINTF("\n  (9d = ISSI: the switch was the whole story)\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "bb")) {
        static uint8_t idb[8];
        unsigned k8;
        tiku_nor_deinit();          /* controller off the pads first */
        tiku_nor_bitbang_id(idb, 8u);
        SHELL_PRINTF("nor bitbang READ_ID:");
        for (k8 = 0u; k8 < 8u; k8++) { SHELL_PRINTF(" %02x", idb[k8]); }
        SHELL_PRINTF("\n  (9d 5b 17 = IS25WX064 alive; all 00 or all ff ="
                     " no answer)\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "regs")) {
        uint32_t g[12]; unsigned k7;
        static const char *const nm[] = {
            "devpwrstatus","ioclkctrl","dev0cfg","dev0cfg1","dev0ddr",
            "dev0xip","dev0instr","padouten","mspicfg","ctrl","intstat",
            "rxentries" };
        tiku_nor_regs(g, 12u);
        SHELL_PRINTF("nor regs (read back):\n");
        for (k7 = 0u; k7 < 12u; k7++) {
            SHELL_PRINTF("  %-13s %08lx\n", nm[k7], (unsigned long)g[k7]);
        }
        return;
    }
    if (argc >= 4 && tiku_cmd_streq(argv[2], "ls") && tiku_cmd_streq(argv[3], "really")) {
        SHELL_PRINTF("nor ls: refused; GP208 polarity and load are "
                     "unverified\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "erases")) {
        SHELL_PRINTF("nor: %lu erases performed this boot (scratch"
                     " sector %08lx)\n",
                     (unsigned long)tiku_nor_erase_count(),
                     (unsigned long)TIKU_NOR_SCRATCH_ADDR);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "off")) {
        tiku_nor_deinit();
        tiku_nor_power(0);
        SHELL_PRINTF("nor: load switch OFF -- VDD_FLASH at true zero,"
                     " contents retained\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "verify")) {
        /* Persistence check: read the stamp back without writing. */
        static uint8_t rd[64];
        uint32_t i6;
        int ok6 = 1;
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc == TIKU_NOR_OK) { rc = tiku_nor_read_id(&id); }
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor verify: bring-up %s\n", nor_errname(rc));
            return;
        }
        rc = tiku_nor_read(TIKU_NOR_SCRATCH_ADDR, rd, sizeof rd);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor verify: read %s\n", nor_errname(rc));
            return;
        }
        for (i6 = 0u; i6 < sizeof rd; i6++) {
            if (rd[i6] != (uint8_t)(0xA5u ^ i6)) { ok6 = 0; }
        }
        SHELL_PRINTF("nor verify: stamp %02x %02x %02x %02x ... -- %s\n",
                     rd[0], rd[1], rd[2], rd[3],
                     ok6 ? "INTACT (survived power loss)"
                         : "absent/modified");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "gate")) {
        /* erase -> program -> verify, on the scratch sector only. */
        static uint8_t wr[64], rd[64];
        uint32_t i6;
        int ok6 = 1;
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc == TIKU_NOR_OK) { rc = tiku_nor_read_id(&id); }
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor gate: bring-up %s\n", nor_errname(rc));
            return;
        }
        SHELL_PRINTF("  erasing scratch sector %08lx (this spends one"
                     " erase cycle)...\n",
                     (unsigned long)TIKU_NOR_SCRATCH_ADDR);
        rc = tiku_nor_erase(TIKU_NOR_SCRATCH_ADDR, 0, 0);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor gate: erase %s\n", nor_errname(rc));
            return;
        }
        rc = tiku_nor_read(TIKU_NOR_SCRATCH_ADDR, rd, sizeof rd);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor gate: read after erase %s\n", nor_errname(rc));
            return;
        }
        for (i6 = 0u; i6 < sizeof rd; i6++) {
            if (rd[i6] != 0xFFu) { ok6 = 0; }
        }
        SHELL_PRINTF("  after erase: %s (erased NOR must read all ff)\n",
                     ok6 ? "all ff" : "NOT ERASED");
        if (!ok6) {
            /* Programming only clears bits, so a program over an unerased
             * sector can still read back bit-exact; the gate stops when the
             * erase left any byte other than ff. */
            SHELL_PRINTF("nor gate: ABORT -- erase did not clear the sector,"
                         " so program+verify would prove nothing\n");
            return;
        }
        for (i6 = 0u; i6 < sizeof wr; i6++) {
            /* A fixed pattern: `power nor verify` checks the same bytes
             * after a power cycle. */
            wr[i6] = (uint8_t)(0xA5u ^ i6);
        }
        rc = tiku_nor_program(TIKU_NOR_SCRATCH_ADDR, wr, sizeof wr);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor gate: program %s\n", nor_errname(rc));
            return;
        }
        rc = tiku_nor_read(TIKU_NOR_SCRATCH_ADDR, rd, sizeof rd);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor gate: read after program %s\n", nor_errname(rc));
            return;
        }
        ok6 = 1;
        for (i6 = 0u; i6 < sizeof rd; i6++) {
            if (rd[i6] != wr[i6]) { ok6 = 0; }
        }
        SHELL_PRINTF("nor gate: program+verify %s -- erases used %lu\n",
                     ok6 ? "bit-exact" : "MISMATCH",
                     (unsigned long)tiku_nor_erase_count());
        SHELL_PRINTF("  now: power-cycle the board, then"
                     " `power nor verify`\n");
        return;
    }
    {
        int want_octal = 0, want_fault = 0, k6;
        for (k6 = 2; k6 < argc; k6++) {
            if (tiku_cmd_streq(argv[k6], "octal")) { want_octal = 1; }
            if (tiku_cmd_streq(argv[k6], "fault"))  { want_fault = 1; }
        }
        tiku_nor_set_trace(nor_trace);
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        tiku_nor_set_trace((void (*)(const char *))0);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor init: %s\n", nor_errname(rc));
            return;
        }
        if (want_fault) { tiku_nor_fault_inject(1); }
        rc = tiku_nor_read_id(&id);
        if (want_fault) { tiku_nor_fault_inject(0); }
        SHELL_PRINTF("nor serial @%lu Hz: mfr %02x (9d=ISSI) type %02x"
                     " cap %02x status %02x nvcr6 %02x\n",
                     tiku_nor_clock_hz(), id.mfr, id.type, id.capacity,
                     id.status, id.ncr6);
        SHELL_PRINTF("  verdict: %s%s\n", nor_errname(rc),
                     want_fault ? "  (fault injected: error EXPECTED)"
                                : "");
        if (want_octal && rc == TIKU_NOR_OK) {
            tiku_nor_set_trace(nor_trace);
            rc = tiku_nor_enter_octal(TIKU_NOR_CLK_96MHZ);
            tiku_nor_set_trace((void (*)(const char *))0);
            if (rc == TIKU_NOR_ERR_STATE) {
                SHELL_PRINTF("nor octal: REFUSED -- non-volatile CR[6]"
                             " is %02x, not ff; this driver does not"
                             " write non-volatile config\n", id.ncr6);
                return;
            }
            (void)tiku_nor_read_id(&id);
            SHELL_PRINTF("nor octal @%lu Hz: %s -- mfr %02x cap %02x"
                         " (identity re-read IN OCTAL)\n",
                         tiku_nor_clock_hz(), nor_errname(rc),
                         id.mfr, id.capacity);
        }
    }
    return;

}

#endif /* TIKU_DRV_NOR_ENABLE */
