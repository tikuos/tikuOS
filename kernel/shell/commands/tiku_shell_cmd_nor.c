/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_nor.c - `power nor ...` verbs.
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
#include "tiku_shell_cmd_nor.h"

#if (TIKU_DRV_NOR_ENABLE + 0)

#include <arch/ambiq/tiku_nor_arch.h>
#include <kernel/cpu/tiku_hang.h>

/** @brief NOR bring-up step tracer -- a wedged step names itself. */
static void nor_trace(const char *step)
{
    SHELL_PRINTF("  nor step: %s\n", step);
}

/** @brief Shared error-name table for the NOR verbs. */
static const char *nor_errname(tiku_nor_err_t rc)
{
    static const char *const en[] = { "ok", "POWER", "CLOCK", "TIMEOUT",
                                      "ID", "ARG", "STATE", "PROGRAM" };
    return ((unsigned)rc < 8u) ? en[rc] : "?";
}

void tiku_shell_cmd_nor(uint8_t argc, const char *argv[])
{
    /* N1/N2 bring-up and gates for the board's 8 MB octal NOR (U12).
     *
     *   power nor id [octal]  serial bring-up + identity; "octal" also
     *                         switches to octal DDR and re-verifies
     *   power nor fault       the same, with D0 stolen -- the guard
     *                         must ERROR rather than invent an answer
     *   power nor gate        the gate only an NVM can pass: erase,
     *                         program, verify, and report the stamp
     *                         that a later power cycle must still find
     *   power nor verify      re-read that stamp WITHOUT writing --
     *                         run it after a reboot or a load-switch
     *                         cycle to prove persistence
     *   power nor off | on    load switch: true zero / restore
     *   power nor erases      how many erases this boot has spent
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
        {   /* any trailing word may be `octal`, `xip` or `sector` */
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
        /* Sweep the SERIAL fast-read dummy count against the stamp the gate
         * programs.  Erased content is a weak reference -- a misframed read of
         * all-FF is still all-FF -- so the reference here is the 0xA5^i
         * pattern, which the octal path already reads back bit-exact. */
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
        /* N2's real gate: an ARRAY read in octal must agree with the same
         * address read in serial.  Identity is a register read, and a part
         * that strobes DQS for the array need not strobe it for registers --
         * so an octal identity that reads zero does not by itself condemn the
         * array path. */
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
            uint32_t tmask = tiku_nor_scan_turnaround(addr, ser, sizeof ser);
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
        {   /* A part that does not strobe DQS for register reads cannot be
             * captured at ANY delay; latching on the controller clock is the
             * distinguishing test. */
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
        /* Is the part already octal?  Configure the controller that way
         * and ask for identity; a serial-mode part will stay silent and
         * an octal one will finally answer. */
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
        /* Enable the flash's load switch, then ask the device who it is
         * over bit-bang (no controller involved).
         *
         * WHY HIGH IS THE SAFE DIRECTION: the switch is an NCP451FCT2G
         * with a 100 kohm pull-DOWN on its enable, so the flash is
         * unpowered by default -- which is exactly what an all-ff
         * bit-bang read means.  Driving the pad HIGH powers the device,
         * and the part is an inrush-limited switch designed for that.
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
        SHELL_PRINTF("nor ls: refused -- driving GP208 wedged the board\n"
                     "  (SWD dead at every speed/reset type; needed a\n"
                     "   physical power cycle).  Polarity and load are\n"
                     "   unestablished; confirm on a scope first.\n");
        return;
    }
    if (argc >= 4 && tiku_cmd_streq(argv[2], "ls") && 0) {
        /* Settle the load-switch polarity by experiment: drive the pad
         * each way (and high-Z) and see which state lets identity read. */
        int lv = tiku_cmd_streq(argv[3], "z") ? -1 : (argv[3][0] == '1' ? 1 : 0);
        tiku_nor_ls_set(lv);
        rc = tiku_nor_init_serial(TIKU_NOR_CLK_24MHZ);
        if (rc == TIKU_NOR_OK) { rc = tiku_nor_read_id(&id); }
        SHELL_PRINTF("nor ls=%s: mfr %02x type %02x cap %02x -- %s\n",
                     (lv < 0) ? "hi-Z" : (lv ? "high" : "low"),
                     id.mfr, id.type, id.capacity, nor_errname(rc));
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
        /* Persistence check with NO writes: read the stamp back. */
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
        for (i6 = 0u; i6 < sizeof rd; i6++) {
            if (rd[i6] != 0xFFu) { ok6 = 0; }
        }
        SHELL_PRINTF("  after erase: %s (erased NOR must read all ff)\n",
                     ok6 ? "all ff" : "NOT ERASED");
        if (!ok6) {
            /* Stop here.  Programming on top of an unerased sector can still
             * verify -- NOR only clears bits, so writing bytes that need no
             * 0->1 transition succeeds -- and the run would report bit-exact
             * while the erase did nothing. */
            SHELL_PRINTF("nor gate: ABORT -- erase did not clear the sector,"
                         " so program+verify would prove nothing\n");
            return;
        }
        for (i6 = 0u; i6 < sizeof wr; i6++) {
            /* Fixed, so `power nor verify` can check it after a power cycle
             * without knowing this run's history. A no-op erase cannot fake a
             * pass here because the all-ff check above aborts first. */
            wr[i6] = (uint8_t)(0xA5u ^ i6);
        }
        rc = tiku_nor_program(TIKU_NOR_SCRATCH_ADDR, wr, sizeof wr);
        if (rc != TIKU_NOR_OK) {
            SHELL_PRINTF("nor gate: program %s\n", nor_errname(rc));
            return;
        }
        rc = tiku_nor_read(TIKU_NOR_SCRATCH_ADDR, rd, sizeof rd);
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
