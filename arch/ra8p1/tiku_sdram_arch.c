/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdram_arch.c - EK-RA8P1 external SDRAM bring-up.
 *
 * Timings come from the IS42S32160F-6 datasheet in nanoseconds, converted at
 * the live bus clock rather than copied as cycle counts, so a clock change
 * cannot silently under-time the part.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_sdram_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_cpu_common.h"

#include <string.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/shell/tiku_shell_io.h>

/*
 * EK-RA8P1 wiring (board manual Table 30), 57 signals.  This is BOARD data,
 * not silicon: another RA8P1 design wiring fewer address lines or a 16-bit
 * bus needs its own table.  Every one takes PSEL = BUS.
 *
 * Encoded as (port << 8) | pin so the table stays one flat array; port 0xA-0xD
 * are ports 10-13, matching RA8P1_PFS()'s numbering.
 */
#define P(port, pin)  (uint16_t)(((port) << 8) | (pin))

static const uint16_t sdram_pins[] = {
    /* A0..A12 */
    P(0xA,3), P(0xA,2), P(0xA,1), P(0xA,0), P(5,3), P(5,4), P(5,5),
    P(5,6), P(5,7), P(5,8), P(5,9), P(5,10), P(6,8),
    /* BA0, BA1 */
    P(0xD,0), P(0xC,15),
    /* DQ0..DQ31 */
    P(3,2), P(3,1), P(3,0), P(1,12), P(1,13), P(1,14), P(1,15), P(6,9),
    P(0xA,11), P(0xA,12), P(0xA,13), P(0xA,14), P(6,10), P(6,11), P(6,12),
    P(6,13),
    P(0xC,14), P(0xC,13), P(0xC,12), P(0xC,11), P(0xC,10), P(0xC,9),
    P(0xC,8), P(0xC,7),
    P(0xC,6), P(0xC,5), P(0xC,4), P(0xC,3), P(0xC,2), P(0xC,1), P(0xC,0),
    P(6,7),
    /* CKE, CLK, DQM0..3, WE#, CAS#, RAS#, CS# */
    P(0xA,6), P(0xA,15), P(6,14), P(0xA,5), P(6,15), P(0xA,4),
    P(0xA,8), P(0xA,9), P(0xA,10), P(8,13),
};

#define SDRAM_NPINS  (sizeof(sdram_pins) / sizeof(sdram_pins[0]))

static uint8_t sdram_ready;

/**
 * @brief Round a nanosecond figure up to whole bus clocks.
 *
 * Always up: a cycle count short of the datasheet's minimum is the failure
 * mode that passes a bring-up test and corrupts data later under load.
 */
static uint32_t ns_to_cycles(uint32_t ns, uint32_t hz)
{
    uint64_t num = (uint64_t)ns * (uint64_t)hz;

    return (uint32_t)((num + 999999999ULL) / 1000000000ULL);
}

/** @brief Point every SDRAM pin at the external-bus peripheral function. */
static void sdram_pins_init(void)
{
    unsigned i;

    /* PFS writes are protected: clear B0WI, then set PFSWE -- the same
     * two-step the console pins need. */
    TIKU_REG8(RA8P1_PWPR_S) = 0U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;

    for (i = 0; i < SDRAM_NPINS; i++) {
        uint32_t port = (uint32_t)(sdram_pins[i] >> 8);
        uint32_t pin  = (uint32_t)(sdram_pins[i] & 0xFFU);

        /* High-speed high drive: 57 lines switching at 120 MHz will not make
         * their edges on the default low-drive setting, which presents as
         * data that is almost right rather than as a dead bus. */
        TIKU_REG32(RA8P1_PFS(port, pin)) =
            (RA8P1_PFS_PSEL_BUS << RA8P1_PFS_PSEL_SHIFT) |
            RA8P1_PFS_DSCR_HS_HIGH | RA8P1_PFS_PMR;
    }

    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;
    __asm__ volatile ("dsb" ::: "memory");
}

int tiku_ra8p1_sdram_init(void)
{
    uint32_t hz = (uint32_t)tiku_cpu_ra8p1_bclk_get_hz();
    uint32_t cl, trcd, trp, tras, twr, refi;
    uint32_t spins;

    if (sdram_ready) {
        return TIKU_RA8P1_SDRAM_OK;
    }
    if (hz == 0UL || hz > 167000000UL) {
        return TIKU_RA8P1_SDRAM_ERR_CLOCK;   /* -6 grade tops out at 167 MHz */
    }

    /*
     * CAS latency is a cliff, not a slope: the -6 part runs CL2 only to
     * 100 MHz and CL3 to 167.  Choosing by clock rather than hardcoding
     * means a future clock change cannot leave CL2 set above its limit.
     */
    cl = (hz <= 100000000UL) ? 2UL : 3UL;

    trcd = ns_to_cycles(18UL, hz);      /* ACT to READ/WRITE      */
    trp  = ns_to_cycles(18UL, hz);      /* PRE to ACT             */
    tras = ns_to_cycles(42UL, hz);      /* ACT to PRE, minimum    */
    twr  = ns_to_cycles(12UL, hz);      /* tDPL, data to precharge */

    /* 8192 rows every 64 ms.  Computed in ns to keep the division exact at
     * any clock: 64 ms / 8192 = 7812 ns. */
    refi = ns_to_cycles(7812UL, hz);

    sdram_pins_init();

    /*
     * SDCLK must run before the part sees any command, and enabling it needs
     * TWO things that are easy to miss together.  It lives in SYSC rather
     * than the bus block -- the same one-layer-deeper placement as SCICKCR
     * and GTCLKCR -- AND it is a clock-generation register, so PRCR_S.PRC0
     * must be open or the write is DROPPED IN SILENCE.
     *
     * Measured, before this unlock existed: SDCKOCR read back 0 after being
     * written, no clock reached the part, and every access returned whatever
     * the bus last drove -- which looked exactly like a wiring fault.
     */
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY | RA8P1_PRCR_PRC0);
    TIKU_REG8(RA8P1_SDCKOCR) = (uint8_t)RA8P1_SDCKOCR_SDCKOEN;
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)RA8P1_PRCR_KEY;
    __asm__ volatile ("dsb" ::: "memory");

    if ((TIKU_REG8(RA8P1_SDCKOCR) & RA8P1_SDCKOCR_SDCKOEN) == 0U) {
        return TIKU_RA8P1_SDRAM_ERR_CLOCK;   /* the write did not stick */
    }

    /* Access disabled while the controller is configured (UM Table 15.33). */
    TIKU_REG8(RA8P1_SDCCR) = 0U;
    TIKU_REG8(RA8P1_SDRFEN) = 0U;

    TIKU_REG8(RA8P1_SDCCR)  = (uint8_t)RA8P1_SDCCR_BSIZE_32;
    TIKU_REG8(RA8P1_SDADR)  = (uint8_t)RA8P1_SDADR_MXC_9BIT;  /* 512 columns */

    TIKU_REG32(RA8P1_SDTR) = RA8P1_SDTR_CL(cl) |
                             RA8P1_SDTR_RCD(trcd) |
                             RA8P1_SDTR_RP(trp) |
                             RA8P1_SDTR_RAI(tras) |
                             ((twr > 1UL) ? RA8P1_SDTR_WR_2CYC : 0UL);

    /*
     * The datasheet's power-up: 100 us of stable clock, precharge all, at
     * least two auto-refresh, then the mode register.  The controller's
     * sequencer does the last three; the 100 us is ours to wait.
     *
     * Eight refreshes rather than the minimum two: it is what JEDEC parts
     * conventionally get, costs microseconds once, and removes any question
     * about a marginal part needing more than the floor.
     */
    tiku_cpu_ra8p1_delay_us(200U);

    TIKU_REG16(RA8P1_SDIR) = (uint16_t)(RA8P1_SDIR_ARFI(8U) |  /* 11 cycles */
                                        RA8P1_SDIR_ARFC(8U) |  /* 8 times   */
                                        RA8P1_SDIR_PRC(0U));   /* 3 cycles  */

    TIKU_REG8(RA8P1_SDICR) = (uint8_t)RA8P1_SDICR_INIRQ;

    for (spins = 2000000UL; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_SDSR) & RA8P1_SDSR_INIST) == 0U) {
            break;
        }
    }
    if (spins == 0UL) {
        return TIKU_RA8P1_SDRAM_ERR_INIT;
    }

    /*
     * Mode register.  Burst length MUST be 1 -- UM 15.6 says operation is
     * not guaranteed otherwise, because the controller issues one column
     * command per access -- and the CL field must match SDTR.CL above.
     * Standard SDR layout: A2:A0 burst length, A3 burst type, A6:A4 CL.
     */
    TIKU_REG16(RA8P1_SDMOD) = (uint16_t)((cl << 4) | 0U);

    /* Writing SDMOD ISSUES the mode-register-set command; MRSST stays set
     * while it runs, and UM Table 15.33 forbids touching the other registers
     * until it clears. */
    for (spins = 2000000UL; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_SDSR) & RA8P1_SDSR_MRSST) == 0U) {
            break;
        }
    }
    if (spins == 0UL) {
        return TIKU_RA8P1_SDRAM_ERR_INIT;
    }

    /* Auto-refresh, then open the window. */
    TIKU_REG16(RA8P1_SDRFCR) = (uint16_t)(((refi - 1UL) & 0x0FFFUL) |
                                          (7U << 12));   /* REFW = 8 cycles */
    TIKU_REG8(RA8P1_SDRFEN) = (uint8_t)RA8P1_SDRFEN_RFEN;

    TIKU_REG8(RA8P1_SDCCR) = (uint8_t)(RA8P1_SDCCR_BSIZE_32 |
                                       RA8P1_SDCCR_EXENB);
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    sdram_ready = 1U;
    return TIKU_RA8P1_SDRAM_OK;
}

int tiku_ra8p1_sdram_ready(void)
{
    return sdram_ready != 0U;
}

int tiku_ra8p1_sdram_attach(void)
{
    int rc = tiku_ra8p1_sdram_init();

    if (rc != TIKU_RA8P1_SDRAM_OK) {
        return rc;
    }
    if (tiku_tier_attach_psram((void *)TIKU_RA8P1_SDRAM_ADDR,
                               (tiku_mem_arch_size_t)TIKU_RA8P1_SDRAM_BYTES)
            != TIKU_MEM_OK) {
        return TIKU_RA8P1_SDRAM_ERR_INIT;
    }
    return TIKU_RA8P1_SDRAM_OK;
}

/*---------------------------------------------------------------------------*/
/* BENCH                                                                     */
/*---------------------------------------------------------------------------*/

#define SD_BENCH_BYTES  (1UL * 1024UL * 1024UL)   /* 1 MB per leg */
#define SD_DWT_CYCCNT   0xE0001004UL
#define SD_DWT_CTRL     0xE0001000UL
#define SD_DEMCR        0xE000EDFCUL

/** @brief Staging buffer for the copy legs; SRAM side of the transfer. */
static uint32_t sd_bench_src[1024];

/**
 * @brief Report one leg as MB/s, computed from CPU cycles.
 *
 * Every leg is NAMED because a single "SDRAM bandwidth" number is a fiction:
 * sequential and strided differ by an order of magnitude on the same array,
 * and quoting one as the figure is how a benchmark misleads.
 */
static void sd_report(const char *name, uint32_t bytes, uint32_t cycles,
                      uint32_t cpu_hz)
{
    uint32_t mbps = 0U;

    if (cycles != 0U) {
        /* bytes/cycle * cpu_hz / 1e6, ordered to stay inside 32 bits. */
        mbps = (uint32_t)(((uint64_t)bytes * (uint64_t)cpu_hz) /
                          ((uint64_t)cycles * 1000000ULL));
    }
    SHELL_PRINTF("  %-18s %8lu cycles  %4lu MB/s\n", name,
                 (unsigned long)cycles, (unsigned long)mbps);
}

void tiku_ra8p1_sdram_bench_run(void)
{
    volatile uint32_t *sd = (volatile uint32_t *)TIKU_RA8P1_SDRAM_ADDR;
    volatile uint32_t *cyc = (volatile uint32_t *)SD_DWT_CYCCNT;
    uint32_t words = SD_BENCH_BYTES / 4UL;
    uint32_t cpu_hz = (uint32_t)tiku_cpu_ra8p1_clock_get_hz();
    uint32_t t0, i, sum;

    if (!sdram_ready) {
        SHELL_PRINTF("sdram: not up\n");
        return;
    }

    /* DWT must actually be counting; a bench on a dead counter reports
     * infinite bandwidth rather than failing. */
    TIKU_REG32(SD_DEMCR) |= (1UL << 24);            /* TRCENA */
    TIKU_REG32(SD_DWT_CTRL) |= 1UL;                 /* CYCCNTENA */
    t0 = *cyc;
    __asm__ volatile ("nop; nop; nop" ::: "memory");
    if (*cyc == t0) {
        SHELL_PRINTF("sdram: DWT cycle counter is not running -- refusing"
                     " to report timings\n");
        return;
    }

    SHELL_PRINTF("sdrambench: %lu MB per leg, bclk %lu Hz, cpu %lu Hz\n",
                 (unsigned long)(SD_BENCH_BYTES >> 20),
                 (unsigned long)tiku_cpu_ra8p1_bclk_get_hz(),
                 (unsigned long)cpu_hz);

    t0 = *cyc;
    for (i = 0; i < words; i++) { sd[i] = i; }
    sd_report("seq-write-32", SD_BENCH_BYTES, *cyc - t0, cpu_hz);

    t0 = *cyc;
    sum = 0U;
    for (i = 0; i < words; i++) { sum += sd[i]; }
    sd_report("seq-read-32", SD_BENCH_BYTES, *cyc - t0, cpu_hz);

    /* Strided by a cache line: every access a fresh line, so this is the
     * cache-miss path rather than the streaming one. */
    t0 = *cyc;
    for (i = 0; i < words; i += 8u) { sum += sd[i]; }
    sd_report("read-stride-32B", SD_BENCH_BYTES / 8UL, *cyc - t0, cpu_hz);

    /* Row-hostile: 4 KB apart is a new SDRAM row every access. */
    t0 = *cyc;
    for (i = 0; i < words; i += 1024u) { sum += sd[i]; }
    sd_report("read-stride-4KB", SD_BENCH_BYTES / 1024UL, *cyc - t0, cpu_hz);

    for (i = 0; i < 1024u; i++) { sd_bench_src[i] = i; }
    t0 = *cyc;
    for (i = 0; i < words; i += 1024u) {
        memcpy((void *)&sd[i], sd_bench_src, sizeof(sd_bench_src));
    }
    sd_report("memcpy-sram->sd", SD_BENCH_BYTES, *cyc - t0, cpu_hz);

    t0 = *cyc;
    for (i = 0; i < words; i += 1024u) {
        memcpy(sd_bench_src, (const void *)&sd[i], sizeof(sd_bench_src));
    }
    sd_report("memcpy-sd->sram", SD_BENCH_BYTES, *cyc - t0, cpu_hz);

    SHELL_PRINTF("  (checksum %lx)\n", (unsigned long)sum);
}
