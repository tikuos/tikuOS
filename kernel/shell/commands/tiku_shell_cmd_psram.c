/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_psram.c - `power psram ...` verbs.
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
#include "tiku_shell_cmd_psram.h"

#if (TIKU_DRV_PSRAM_ENABLE + 0)

#include <arch/ambiq/tiku_psram_arch.h>
#include <arch/ambiq/tiku_cpu_common.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_hang.h>
#include "apollo510.h"   /* MSPI0 register block, used by the raw probe verbs */

/**
 * @brief PSRAM bring-up step tracer.
 *
 * Prints each step BEFORE it runs and flushes, so if a register write stalls
 * the bus the last line on the wire names the step that wedged.  This is how
 * the first bring-up attempt's silent hang was localised.
 */
static void psram_trace(const char *step)
{
    SHELL_PRINTF("  psram step: %s\n", step);
}

void tiku_shell_cmd_psram(uint8_t argc, const char *argv[])
{
    /* M1 bring-up verb for the board's 64 MB octal-DDR PSRAM (EVB U14).
     *
     *   power psram id [clk]   power MSPI0, reset the device, read its
     *                          mode registers and check identity
     *   power psram fault      the SAME read with D0 taken away from the
     *                          controller -- proves the error path fires
     *                          instead of returning plausible garbage
     *   power psram off        release the controller domain
     *
     * Identity before anything else, at the lowest clock, because a
     * mis-timed octal bus answers with numbers that look real. */
    unsigned clk = TIKU_PSRAM_CLK_48MHZ;
    int want_fault = (argc >= 3 && tiku_cmd_streq(argv[2], "fault"));
    int nodqs = 0;
    {   /* any trailing "nodqs" word switches the strobe off */
        int k;
        for (k = 2; k < argc; k++) {
            if (tiku_cmd_streq(argv[k], "nodqs")) { nodqs = 1; }
        }
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "up")) {
        /* power psram up [mhz] -- the M4 lifecycle: speed + identity +
         * (at 192) timing scan + XIP map + TIKU_MEM_PSRAM tier attach. */
        unsigned row = TIKU_PSRAM_CLK_192MHZ, n3 = 0u;
        tiku_psram_err_t rc;
        if (argc >= 4) {
            const char *q3 = argv[3];
            while (*q3 >= '0' && *q3 <= '9') { n3 = n3*10u + (unsigned)(*q3++ - '0'); }
            if (n3 && n3 < 96u)        { row = TIKU_PSRAM_CLK_48MHZ; }
            else if (n3 && n3 < 125u)  { row = TIKU_PSRAM_CLK_96MHZ; }
            else if (n3 && n3 < 192u)  { row = TIKU_PSRAM_CLK_125MHZ; }
        }
        rc = tiku_psram_up(row, (row == TIKU_PSRAM_CLK_192MHZ) ? 1 : 0);
        SHELL_PRINTF("psram up: %s -- io %lu Hz, tap %u, tier %s,"
                     " 64 MB at 0x%08lx\n",
                     (rc == TIKU_PSRAM_OK) ? "ok" : "FAILED",
                     tiku_psram_clock_hz(), tiku_psram_tap(),
                     (rc == TIKU_PSRAM_OK) ? "attached" : "no",
                     (unsigned long)TIKU_PSRAM_XIP_BASE);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "down")) {
        int force = (argc >= 4 && tiku_cmd_streq(argv[3], "force"));
        tiku_psram_err_t rc = tiku_psram_down(force);
        SHELL_PRINTF("psram down: %s%s\n",
                     (rc == TIKU_PSRAM_OK) ? "ok (contents gone)"
                     : "REFUSED -- tier has live allocations",
                     (rc != TIKU_PSRAM_OK) ? " (use: down force)" : "");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "sleep")) {
        tiku_psram_err_t rc = tiku_psram_halfsleep();
        SHELL_PRINTF("psram sleep: %s (contents retained on"
                     " self-refresh; access refused until wake)\n",
                     (rc == TIKU_PSRAM_OK) ? "ok" : "FAILED");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "wake")) {
        tiku_psram_err_t rc = tiku_psram_wake();
        SHELL_PRINTF("psram wake: %s\n",
                     (rc == TIKU_PSRAM_OK) ? "ok -- identity re-verified"
                                           : "FAILED");
        if (rc == TIKU_PSRAM_OK) {
            (void)tiku_psram_xip_enable(1);   /* restore the mapping */
        }
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "tier")) {
        /* The M4 acceptance gate: carve 32 MB from the PSRAM tier, fill
         * through the aperture, checksum it back, and report -- then
         * survive a sleep/wake with the SAME checksum. */
        static tiku_arena_t ar;
        uint8_t *p2;
        uint32_t i3, sum1 = 0u, sum2 = 0u;
        const uint32_t N = 32u * 1024u * 1024u;
        if (tiku_tier_arena_create(&ar, TIKU_MEM_PSRAM, N, 42u)
                != TIKU_MEM_OK) {
            SHELL_PRINTF("tier: arena create failed (is psram up?)\n");
            return;
        }
        p2 = (uint8_t *)tiku_arena_alloc(&ar, N - 64u);
        if (!p2) {
            SHELL_PRINTF("tier: alloc failed\n");
            return;
        }
        SHELL_PRINTF("tier: 32 MB arena, buf %08lx -- filling\n",
                     (unsigned long)(uintptr_t)p2);
        for (i3 = 0u; i3 < N - 64u; i3 += 4u) {
            *(volatile uint32_t *)(p2 + i3) = i3 * 2654435761u;
            if ((i3 & 0xFFFFFu) == 0u) { tiku_hang_checkin(); }
        }
        tiku_cpu_dcache_clean(p2, N - 64u);
        tiku_cpu_dcache_invalidate(p2, N - 64u);
        for (i3 = 0u; i3 < N - 64u; i3 += 4096u) {
            sum1 += *(volatile uint32_t *)(p2 + i3);
            if ((i3 & 0xFFFFFu) == 0u) { tiku_hang_checkin(); }
        }
        SHELL_PRINTF("tier: filled, sparse checksum %08lx --"
                     " sleeping...\n", (unsigned long)sum1);
        if (tiku_psram_halfsleep() != TIKU_PSRAM_OK) {
            SHELL_PRINTF("tier: sleep failed\n");
            return;
        }
        {   /* hold half sleep long enough to mean something */
            uint32_t ms3;
            for (ms3 = 0u; ms3 < 1500u; ms3++) {
                tiku_cpu_ambiq_delay_us(1000u);
                if ((ms3 & 63u) == 0u) { tiku_hang_checkin(); }
            }
        }
        if (tiku_psram_wake() != TIKU_PSRAM_OK) {
            SHELL_PRINTF("tier: wake failed\n");
            return;
        }
        (void)tiku_psram_xip_enable(1);
        tiku_cpu_dcache_invalidate(p2, N - 64u);
        for (i3 = 0u; i3 < N - 64u; i3 += 4096u) {
            sum2 += *(volatile uint32_t *)(p2 + i3);
            if ((i3 & 0xFFFFFu) == 0u) { tiku_hang_checkin(); }
        }
        SHELL_PRINTF("tier: after 1.5 s half sleep, checksum %08lx --"
                     " %s\n", (unsigned long)sum2,
                     (sum1 == sum2) ? "RETAINED, gate PASSES"
                                    : "LOST -- gate FAILS");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "speed") && argc >= 4) {
        /* power psram speed <48|96|125|192> -- program device latencies
         * and reconfigure the controller, then prove it with the
         * identity gate at the new clock. */
        unsigned n = 0u; const char *q = argv[3];
        unsigned row;
        tiku_psram_id_t id; tiku_psram_err_t rc;
        while (*q >= '0' && *q <= '9') { n = n*10u + (unsigned)(*q++ - '0'); }
        row = (n >= 192u) ? TIKU_PSRAM_CLK_192MHZ
            : (n >= 125u) ? TIKU_PSRAM_CLK_125MHZ
            : (n >= 96u)  ? TIKU_PSRAM_CLK_96MHZ
                          : TIKU_PSRAM_CLK_48MHZ;
        rc = tiku_psram_set_speed(row);
        if (rc != TIKU_PSRAM_OK) {
            SHELL_PRINTF("speed: set failed (%d)\n", (int)rc);
            return;
        }
        rc = tiku_psram_read_id(&id);
        SHELL_PRINTF("speed: io clock %lu Hz, identity %s"
                     " (vendor %02x density %x)\n",
                     tiku_psram_clock_hz(),
                     (rc == TIKU_PSRAM_OK) ? "ok" : "FAILED",
                     id.vendor_id, id.density_code);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "scan3")) {
        /* power psram scan3 [mhz] -- the real M2 timing scan at the live
         * (or requested) clock.  The output must show failing taps
         * bracketing the window, or the scan proved nothing. */
        uint32_t mask = 0u; unsigned center = 0u, width, t;
        if (argc >= 5) { }
        if (argc >= 4) {
            unsigned n = 0u; const char *q = argv[3];
            while (*q >= '0' && *q <= '9') { n = n*10u + (unsigned)(*q++ - '0'); }
            if (n) {
                unsigned row = (n >= 192u) ? TIKU_PSRAM_CLK_192MHZ
                             : (n >= 125u) ? TIKU_PSRAM_CLK_125MHZ
                             : (n >= 96u)  ? TIKU_PSRAM_CLK_96MHZ
                                           : TIKU_PSRAM_CLK_48MHZ;
                if (tiku_psram_set_speed(row) != TIKU_PSRAM_OK) {
                    SHELL_PRINTF("scan3: speed set failed\n");
                    return;
                }
            }
        }
        width = tiku_psram_timing_scan(&mask, &center);
        SHELL_PRINTF("timing scan @ %lu Hz: taps 0..31 = ",
                     tiku_psram_clock_hz());
        for (t = 0u; t < 32u; t++) {
            SHELL_PRINTF("%c", (mask & (1u << t)) ? 'P' : '.');
        }
        SHELL_PRINTF("\n  widest window %lu taps, shipped tap %u%s\n",
                     (unsigned long)width, center,
                     (width == 32u) ? "  [WARNING: passes everywhere --"
                                      " not a proven scan at this clock]"
                                    : "");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "mem")) {
        /* power psram mem -- the M2 acceptance gate: 64 KB address-derived
         * pattern across low + high regions, bit-exact, via PIO. */
        static uint8_t wr[1024], rd[1024];
        static uint32_t xorh[256];
        static const uint32_t base[2] = { 0x00010000u, 0x03F00000u };
        uint32_t r, off, i, errs = 0u, sum = 0u;
        for (i = 0u; i < 256u; i++) { xorh[i] = 0u; }
        for (r = 0u; r < 2u; r++) {
            for (off = 0u; off < 32768u; off += (uint32_t)(sizeof wr)) {
                for (i = 0u; i < (uint32_t)(sizeof wr); i++) {
                    uint32_t a = base[r] + off + i;
                    wr[i] = (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0x5Au);
                }
                if (tiku_psram_mem_write(base[r] + off, wr,
                        (uint32_t)(sizeof wr)) != TIKU_PSRAM_OK) {
                    SHELL_PRINTF("mem: write fail @%lx\n",
                                 (unsigned long)(base[r] + off));
                    return;
                }
            }
            for (off = 0u; off < 32768u; off += (uint32_t)(sizeof rd)) {
                if (tiku_psram_mem_read(base[r] + off, rd,
                        (uint32_t)(sizeof rd)) != TIKU_PSRAM_OK) {
                    SHELL_PRINTF("mem: read fail @%lx\n",
                                 (unsigned long)(base[r] + off));
                    return;
                }
                for (i = 0u; i < (uint32_t)(sizeof rd); i++) {
                    uint32_t a = base[r] + off + i;
                    uint8_t e = (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0x5Au);
                    if (rd[i] != e) {
                        if (errs < 6u) {
                            SHELL_PRINTF("    @%08lx want %02x got %02x\n",
                                         (unsigned long)a, e, rd[i]);
                        }
                        errs++;
                        xorh[(uint8_t)(rd[i] ^ e)]++;
                    }
                    sum += rd[i];
                }
                tiku_hang_checkin();
            }
        }
        for (i = 0u; i < 256u; i++) {
            if (xorh[i] != 0u) {
                SHELL_PRINTF("    xor %02lx : %lu times\n",
                             (unsigned long)i, (unsigned long)xorh[i]);
            }
        }
        SHELL_PRINTF("mem: 64 KB x2 regions @ %lu Hz: %lu errors,"
                     " checksum %08lx -- %s\n",
                     tiku_psram_clock_hz(), (unsigned long)errs,
                     (unsigned long)sum,
                     errs ? "FAIL" : "bit-exact");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "retain")) {
        /* power psram retain <ms> -- the refresh-integrity gate: write a
         * pattern, WAIT (self-refresh must carry it), verify bit-exact.
         * Guards every burst/pause tuning against silent decay. */
        static uint8_t wr2[1024];
        uint32_t ms2 = 500u, off2, i2, errs2 = 0u;
        if (argc >= 4) {
            unsigned n2 = 0u; const char *q2 = argv[3];
            while (*q2 >= '0' && *q2 <= '9') { n2 = n2*10u + (unsigned)(*q2++ - '0'); }
            if (n2) { ms2 = n2; }
        }
        for (off2 = 0u; off2 < 65536u; off2 += (uint32_t)(sizeof wr2)) {
            for (i2 = 0u; i2 < (uint32_t)(sizeof wr2); i2++) {
                uint32_t a2 = 0x00200000u + off2 + i2;
                wr2[i2] = (uint8_t)(a2 ^ (a2 >> 8) ^ 0x3Cu);
            }
            if (tiku_psram_mem_write(0x00200000u + off2, wr2,
                    (uint32_t)(sizeof wr2)) != TIKU_PSRAM_OK) {
                SHELL_PRINTF("retain: write fail\n");
                return;
            }
        }
        for (i2 = 0u; i2 < ms2; i2++) {
            tiku_cpu_ambiq_delay_us(1000u);
            if ((i2 & 63u) == 0u) { tiku_hang_checkin(); }
        }
        for (off2 = 0u; off2 < 65536u; off2 += (uint32_t)(sizeof wr2)) {
            if (tiku_psram_mem_read(0x00200000u + off2, wr2,
                    (uint32_t)(sizeof wr2)) != TIKU_PSRAM_OK) {
                SHELL_PRINTF("retain: read fail\n");
                return;
            }
            for (i2 = 0u; i2 < (uint32_t)(sizeof wr2); i2++) {
                uint32_t a2 = 0x00200000u + off2 + i2;
                if (wr2[i2] != (uint8_t)(a2 ^ (a2 >> 8) ^ 0x3Cu)) { errs2++; }
            }
            tiku_hang_checkin();
        }
        SHELL_PRINTF("retain: 64 KB held %lu ms: %lu errors -- %s\n",
                     (unsigned long)ms2, (unsigned long)errs2,
                     errs2 ? "FAIL (refresh starved?)" : "bit-exact");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "dbb") && argc >= 4) {
        /* power psram dbb <code> -- DMA boundary A/B: 6=1K 7=2K 8=4K
         * 9=8K 10=16K.  Longer bursts amortize the fixed per-row tax;
         * the RISK is CE-low time vs the die's refresh (tCEM), which is
         * exactly what the retain gate after each setting must clear. */
        unsigned n5 = 0u; const char *q5 = argv[3];
        while (*q5 >= '0' && *q5 <= '9') { n5 = n5*10u + (unsigned)(*q5++ - '0'); }
        MSPI0->DEV0BOUNDARY_b.DMABOUND0 = n5;
        SHELL_PRINTF("dbb: DMABOUND0 = %lu\n",
                     (unsigned long)MSPI0->DEV0BOUNDARY_b.DMABOUND0);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "dtl") && argc >= 4) {
        /* power psram dtl <n> -- runtime DMATIMELIMIT A/B (the per-KB
         * plateau hunt).  Integrity gates (mem/retain) MUST follow any
         * change before a number is believed. */
        unsigned n4 = 0u; const char *q4 = argv[3];
        while (*q4 >= '0' && *q4 <= '9') { n4 = n4*10u + (unsigned)(*q4++ - '0'); }
        MSPI0->DEV0BOUNDARY_b.DMATIMELIMIT0 = n4;
        SHELL_PRINTF("dtl: DMATIMELIMIT0 = %lu\n",
                     (unsigned long)MSPI0->DEV0BOUNDARY_b.DMATIMELIMIT0);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "bench")) {
        /* power psram bench -- M3: DWT-timed bandwidth through each path.
         * Work is the denominator: bytes moved + checksum per leg. */
        extern void tiku_psram_bench_run(void);
        tiku_psram_bench_run();
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "scan2")) {
        /* Hunt the RX capture point in no-DQS mode.  The device is
         * proven alive (bit-bang: MR1 0x8d, MR2 0xde), so any cell that
         * reads 8d is the capture configuration this silicon wants. */
        unsigned rn, rc2, rs, ta;
        SHELL_PRINTF("rx capture sweep (no DQS), target MR1=8d:\n");
        for (rn = 0u; rn <= 1u; rn++) {
         for (rc2 = 0u; rc2 <= 1u; rc2++) {
          for (rs = 0u; rs <= 3u; rs++) {
            for (ta = 8u; ta <= 18u; ta += 1u) {
                tiku_psram_id_t id;
                tiku_psram_deinit();
                tiku_psram_set_dqs(0);
                tiku_psram_set_rx(rn, rc2, rs);
                tiku_psram_set_turnaround(ta);
                if (tiku_psram_init(TIKU_PSRAM_CLK_48MHZ)
                        != TIKU_PSRAM_OK) { continue; }
                (void)tiku_psram_read_id(&id);
                if (id.mr1 != 0x42u && id.mr1 != 0x00u) {
                    SHELL_PRINTF("  rxneg %u rxcap %u rxsmp %u ta %2u:"
                                 " MR1 %02x MR2 %02x%s\n",
                                 rn, rc2, rs, ta, id.mr1, id.mr2,
                                 (id.mr1 == 0x8Du) ? "  <== TARGET" : "");
                }
            }
          }
         }
        }
        SHELL_PRINTF("sweep done (silent cells read 42 or 00)\n");
        tiku_psram_set_rx(0u, 0u, 1u);
        tiku_psram_set_turnaround(0u);
        tiku_psram_set_dqs(1);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "txtest")) {
        /* Does CONTROLLER TX reach the device at all?  The device is
         * proven alive over GPIO, so: bit-bang-read MR0, write MR0
         * through the CONTROLLER (drive-strength bits flipped), then
         * bit-bang-read it again.  A change proves controller TX end to
         * end with no dependence on controller RX; no change means the
         * controller's bus never reaches the part and every RX theory
         * is moot. */
        static uint8_t before[16], after[16];
        unsigned k; uint8_t b0 = 0u, a0 = 0u;
        uint32_t wr;
        tiku_psram_deinit();
        tiku_psram_bitbang_reg(0u, before, 16u);
        for (k = 8u; k < 16u; k += 2u) {   /* steady repeat region */
            if (before[k] == before[k + 2u < 16u ? k + 2u : k]) {
                b0 = before[k]; break;
            }
        }
        tiku_psram_set_dqs(0);
        if (tiku_psram_init(TIKU_PSRAM_CLK_48MHZ) != TIKU_PSRAM_OK) {
            SHELL_PRINTF("txtest: init failed\n");
            return;
        }
        wr = (uint32_t)(b0 ^ 0x01u);       /* flip DS bit0 */
        (void)tiku_psram_reg_write(0u, wr);
        tiku_psram_deinit();
        tiku_psram_bitbang_reg(0u, after, 16u);
        for (k = 8u; k < 16u; k += 2u) {
            if (after[k] == after[k + 2u < 16u ? k + 2u : k]) {
                a0 = after[k]; break;
            }
        }
        SHELL_PRINTF("txtest: MR0 before:");
        for (k = 0u; k < 16u; k++) { SHELL_PRINTF(" %02x", before[k]); }
        SHELL_PRINTF("\n        MR0 after :");
        for (k = 0u; k < 16u; k++) { SHELL_PRINTF(" %02x", after[k]); }
        SHELL_PRINTF("\n        wrote %02lx: %s\n", (unsigned long)wr,
                     (a0 == (uint8_t)wr) ? "CONTROLLER TX REACHES DEVICE"
                     : (a0 == b0) ? "no change -- controller TX never lands"
                                  : "changed to something ELSE (partial)");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "arb")) {
        /* THE ARBITER.  Controller-write a distinctive 64 B pattern at
         * 0x4000, then bit-bang-read 0x3800 / 0x4000 / 0x4800 and print
         * the streams.  Wherever the pattern physically shows up names
         * the guilty path: at 0x4000 = write correct (read path adds
         * 0x800); at 0x4800 = write path adds 0x800; at 0x3800 = write
         * path subtracts. */
        static uint8_t pat[64]; static uint8_t ed[48];
        uint32_t i; unsigned k2;
        static const uint32_t probe[3] = { 0x3800u, 0x4000u, 0x4800u };
        tiku_psram_err_t rc;
        unsigned row = TIKU_PSRAM_CLK_48MHZ;
        if (argc >= 4) {
            unsigned n2 = 0u; const char *q2 = argv[3];
            while (*q2 >= '0' && *q2 <= '9') { n2 = n2*10u + (unsigned)(*q2++ - '0'); }
            if (n2 >= 192u)      { row = TIKU_PSRAM_CLK_192MHZ; }
            else if (n2 >= 125u) { row = TIKU_PSRAM_CLK_125MHZ; }
            else if (n2 >= 96u)  { row = TIKU_PSRAM_CLK_96MHZ; }
        }
        rc = tiku_psram_set_speed(row);
        if (rc != TIKU_PSRAM_OK) {
            SHELL_PRINTF("arb: speed failed\n");
            return;
        }
        for (i = 0u; i < 64u; i++) { pat[i] = (uint8_t)(0xB0u + i); }
        if (tiku_psram_mem_write(0x4000u, pat, 64u) != TIKU_PSRAM_OK) {
            SHELL_PRINTF("arb: write failed\n");
            return;
        }
        tiku_psram_deinit();
        for (k2 = 0u; k2 < 3u; k2++) {
            tiku_psram_bitbang_mem(probe[k2], ed, 48u);
            SHELL_PRINTF("  bb @%04lx:", (unsigned long)probe[k2]);
            for (i = 0u; i < 48u; i++) { SHELL_PRINTF(" %02x", ed[i]); }
            SHELL_PRINTF("\n");
        }
        SHELL_PRINTF("  (wrote b0,b1,b2.. at 4000 via controller;"
                     " find it above)\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "bb")) {
        /* Ground truth: the same identity read, bit-banged on GPIO with
         * the MSPI controller out of the picture entirely. */
        static uint8_t edges[32];
        unsigned k;
        tiku_psram_deinit();     /* controller off the pads first */
        tiku_psram_bitbang_id(edges, 32u);
        SHELL_PRINTF("bitbang MR1 read, D0-7 after each edge:\n ");
        for (k = 0u; k < 32u; k++) {
            SHELL_PRINTF(" %02x", edges[k]);
            if ((k & 7u) == 7u) { SHELL_PRINTF("\n "); }
        }
        SHELL_PRINTF("(0d/8d somewhere = device alive in octal;"
                     " 00/ff throughout = no answer)\n");
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "scan")) {
        /* Sweep the read window and print the identity register for each
         * setting.  The right answer is the one that reads 0x0D in the
         * low five bits -- and the sweep must SHOW the wrong settings
         * either side of it, or it has not proven anything. */
        unsigned ta;
        SHELL_PRINTF("turnaround sweep (%s), want MR1 vendor 0d:\n",
                     nodqs ? "no DQS" : "DQS");
        for (ta = 4u; ta <= 30u; ta += 1u) {
            tiku_psram_id_t id;
            tiku_psram_err_t rc;
            tiku_psram_deinit();
            tiku_psram_set_dqs(nodqs ? 0 : 1);
            tiku_psram_set_turnaround(ta);
            if (tiku_psram_init(TIKU_PSRAM_CLK_48MHZ) != TIKU_PSRAM_OK) {
                SHELL_PRINTF("  ta %2u: init failed\n", ta);
                continue;
            }
            rc = tiku_psram_read_id(&id);
            SHELL_PRINTF("  ta %2u: MR1 %02x MR2 %02x vendor %02x%s\n",
                         ta, id.mr1, id.mr2, id.vendor_id,
                         (rc == TIKU_PSRAM_OK) ? "   <== MATCH" : "");
        }
        tiku_psram_set_turnaround(0u);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "cmd")) {
        uint32_t c = 0u;
        tiku_psram_err_t rc;
        static const char *const en2[] = { "ok", "POWER", "CLOCK",
                                          "TIMEOUT", "ID", "ARG" };
        tiku_psram_set_dqs(nodqs ? 0 : 1);
        rc = tiku_psram_init(clk);
        if (rc != TIKU_PSRAM_OK) {
            SHELL_PRINTF("psram init: %s\n", en2[rc]);
            return;
        }
        rc = tiku_psram_cmd_probe(&c);
        SHELL_PRINTF("psram cmd (no data phase): %s  ctrl %08lx"
                     " (bit1 STATUS=done, bit2 BUSY)\n",
                     en2[rc], (unsigned long)c);
        return;
    }

    if (argc >= 3 && tiku_cmd_streq(argv[2], "regs")) {
        tiku_psram_regs_t g;
        tiku_psram_regs(&g);
        SHELL_PRINTF("psram regs (read back, not assumed):\n");
        SHELL_PRINTF("  devpwrstatus %08lx  clkgen.misc %08lx  ioclkctrl %08lx\n",
                     (unsigned long)g.devpwrstatus,
                     (unsigned long)g.clkgen_misc,
                     (unsigned long)g.mspiioclkctrl);
        SHELL_PRINTF("  dev0cfg %08lx cfg1 %08lx ddr %08lx xip %08lx instr %08lx\n",
                     (unsigned long)g.dev0cfg, (unsigned long)g.dev0cfg1,
                     (unsigned long)g.dev0ddr, (unsigned long)g.dev0xip,
                     (unsigned long)g.dev0instr);
        SHELL_PRINTF("  padouten %08lx mspicfg %08lx ctrl %08lx intstat %08lx\n",
                     (unsigned long)g.padouten, (unsigned long)g.mspicfg,
                     (unsigned long)g.ctrl, (unsigned long)g.intstat);
        SHELL_PRINTF("  rx %lu tx %lu entries\n",
                     (unsigned long)g.rxentries, (unsigned long)g.txentries);
        SHELL_PRINTF("  during last xfer: ctrl@start %08lx tx@write %lu"
                     " -> tx %lu ctrl %08lx intstat %08lx\n",
                     (unsigned long)g.dbg_ctrl_after_start,
                     (unsigned long)g.dbg_tx_after_write,
                     (unsigned long)g.dbg_tx_settled,
                     (unsigned long)g.dbg_ctrl_settled,
                     (unsigned long)g.dbg_intstat);
        return;
    }
    if (argc >= 3 && tiku_cmd_streq(argv[2], "off")) {
        tiku_psram_deinit();
        SHELL_PRINTF("psram: MSPI0 domain released (powered %d)\n",
                     tiku_psram_powered());
        return;
    }
    if (argc >= 4) {
        const char *q = argv[3]; unsigned n = 0u;
        while (*q >= '0' && *q <= '9') { n = n*10u + (unsigned)(*q++ - '0'); }
        if (n >= 192u)      { clk = TIKU_PSRAM_CLK_192MHZ; }
        else if (n >= 125u) { clk = TIKU_PSRAM_CLK_125MHZ; }
        else if (n >= 96u)  { clk = TIKU_PSRAM_CLK_96MHZ; }
    }
    {
        tiku_psram_id_t id;
        tiku_psram_err_t rc;
        tiku_psram_set_trace(psram_trace);
        tiku_psram_set_dqs(nodqs ? 0 : 1);
        rc = tiku_psram_init(clk);
        tiku_psram_set_trace((void (*)(const char *))0);
        static const char *const en[] = { "ok", "POWER", "CLOCK",
                                          "TIMEOUT", "ID", "ARG" };
        if (rc != TIKU_PSRAM_OK) {
            SHELL_PRINTF("psram init: %s\n", en[rc]);
            return;
        }
        SHELL_PRINTF("psram: MSPI0 up, io clock %lu Hz, powered %d\n",
                     tiku_psram_clock_hz(), tiku_psram_powered());
        if (want_fault) {
            tiku_psram_fault_inject(1);
        }
        rc = tiku_psram_read_id(&id);
        if (want_fault) {
            tiku_psram_fault_inject(0);
        }
        /* Raw bytes ALWAYS printed, verdict separately: the caller needs
         * the numbers to tell a dead bus from a wrong part. */
        SHELL_PRINTF("  MR0 %02x MR1 %02x MR2 %02x MR3 %02x MR4 %02x MR8 %02x\n",
                     id.mr0, id.mr1, id.mr2, id.mr3, id.mr4, id.mr8);
        SHELL_PRINTF("  vendor %02x (0d=AP) density %x (6=512Mb) gen %u die %s\n",
                     id.vendor_id, id.density_code,
                     (unsigned)(id.generation == 0u ? 5u : id.generation + 1u),
                     id.good_die ? "pass" : "BAD");
        SHELL_PRINTF("  size %lu bytes -- verdict: %s%s\n",
                     (unsigned long)id.size_bytes, en[rc],
                     want_fault ? "  (fault injected: error EXPECTED)" : "");
    }
    return;

}

#endif /* TIKU_DRV_PSRAM_ENABLE */
