/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - RA8P1 clock state, read back rather than assumed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_uart_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_xflash_arch.h"
#include "tiku_cache_arch.h"
#include "tiku_cpu_common.h"
#if (TIKU_DRV_CPU1_ENABLE + 0)
#include "tiku_cpu1_arch.h"
#endif

#include <stdint.h>

#include <kernel/memory/tiku_mem.h>

#ifndef TIKU_RA8P1_FREQ_UNPROVEN
#define TIKU_RA8P1_FREQ_UNPROVEN 0
#endif

#if (TIKU_RA8P1_FREQ_UNPROVEN + 0)
/*
 * How far the last rung change got, in warm-persist memory so it survives
 * whatever it is explaining.  Only an unproven-rung build carries it, so the
 * ladder is instrumented exactly while it is being brought up.
 */
TIKU_PERSIST_WARM volatile uint32_t tiku_ra8p1_freq_step;
/* Why the last rung change failed: 0xF1 mosc, 0xF2 MRCFREQ, 0xF3 lock
 * timeout (low byte carries OSCSF).  Survives the fallback that would
 * otherwise overwrite the step trail. */
TIKU_PERSIST_WARM volatile uint32_t tiku_ra8p1_freq_fail;
#define STEP(n)  do { tiku_ra8p1_freq_step = (n); } while (0)
#define FAILREC(v) do { tiku_ra8p1_freq_fail = (v); } while (0)
#else
#define STEP(n)  do { } while (0)
#define FAILREC(v) do { } while (0)
#endif

/** @brief Rate the tree is running at now; the boot clock until it is raised. */
static unsigned long cpu_hz_now = TIKU_RA8P1_ICLK_BOOT_HZ;

/** @brief SCICLK, which the console divides -- MOCO at /1 out of reset. */
static unsigned long sci_hz_now = TIKU_RA8P1_MOCO_HZ;

/**
 * @brief Turn a SCKDIVCR 4-bit field into the divisor it means.
 *
 * @param code  Field value, 0..15
 * @return The divisor, or 1 for a code the manual marks prohibited
 */
static uint8_t div_of(uint32_t code)
{
    switch (code & 0xFU) {
    case 0x0U: return 1U;
    case 0x1U: return 2U;
    case 0x2U: return 4U;
    case 0x3U: return 8U;
    case 0x4U: return 16U;
    case 0x5U: return 32U;
    case 0x6U: return 64U;
    case 0x8U: return 3U;
    case 0x9U: return 6U;
    case 0xAU: return 12U;
    case 0xBU: return 24U;
    default:   return 1U;    /* prohibited encoding; treat as undivided */
    }
}

/**
 * @brief Rate of the selected system clock source, where it is knowable.
 *
 * MOCO, LOCO and the board's crystals are exact.  HOCO is option-trimmed and
 * the PLLs depend on registers R4 has not written, so those report 0 rather
 * than a plausible guess a wrong baud divisor could hide behind.
 *
 * @param cksel  SCKSCR.CKSEL value
 * @return Source rate in Hz, or 0 when this port cannot yet derive it
 */
static unsigned long src_hz_of(uint8_t cksel)
{
    switch (cksel) {
    case TIKU_RA8P1_CKSEL_MOCO:   return TIKU_RA8P1_MOCO_HZ;
    case TIKU_RA8P1_CKSEL_LOCO:   return 32768UL;
    case TIKU_RA8P1_CKSEL_MAIN:   return TIKU_BOARD_MOSC_HZ;
    case TIKU_RA8P1_CKSEL_SUBCLK: return TIKU_BOARD_SUBCLK_HZ;
    /* PLL1P is CPUCK0's source at every rung, and CPUCK0 divides it by one,
     * so the rate the driver last established IS the source rate.  Before the
     * PLL is raised this is the boot clock, which is also correct because the
     * tree is not on PLL1P then. */
    case TIKU_RA8P1_CKSEL_PLL1P:  return cpu_hz_now;
    default:                      return 0UL;
    }
}

/**
 * @brief Unlock or relock the clock-generation registers.
 *
 * Every write to PRCR_S must carry the key; without it the write is dropped
 * silently and the oscillator simply never starts.
 *
 * @param unlock  Non-zero to allow writes, zero to protect again
 */
static void clock_protect(int unlock)
{
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY |
                                          (unlock ? (RA8P1_PRCR_PRC0 |
                                                     RA8P1_PRCR_PRC1) : 0U));
}

int tiku_cpu_ra8p1_mosc_start(void)
{
    unsigned long spins;
    uint8_t       tries;

    if ((TIKU_REG8(RA8P1_MOSCCR) & RA8P1_MOSCCR_MOSTP) == 0U) {
        return 0;                       /* already running */
    }

    /*
     * More than one attempt, because SYSRESETREQ re-asserts MOSTP while the
     * crystal is still ringing, and a decaying resonator does not always
     * reach the stability flag inside one wait.  Stop, let it ring down,
     * start again.  Bounded: a board with no crystal fitted must report
     * failure, not hang the boot it was called from.
     */
    for (tries = 0U; tries < 3U; tries++) {
        clock_protect(1);
        /* MOMCR before MOSTP -- the manual is explicit that the mode
         * register must be settled first (UM 9, MOSCCR note 1). */
        TIKU_REG8(RA8P1_MOMCR) = (uint8_t)RA8P1_MOMCR_DRV_24MHZ;
        TIKU_REG8(RA8P1_MOSCWTCR) = 0x09U;  /* longest documented wait */
        TIKU_REG8(RA8P1_MOSCCR) = 0U;       /* MOSTP = 0: oscillate    */
        clock_protect(0);

        for (spins = 4000000UL; spins != 0UL; spins--) {
            if (TIKU_REG8(RA8P1_OSCSF) & RA8P1_OSCSF_MOSCSF) {
                return 0;
            }
        }

        clock_protect(1);
        TIKU_REG8(RA8P1_MOSCCR) = (uint8_t)RA8P1_MOSCCR_MOSTP;
        clock_protect(0);
        tiku_cpu_ra8p1_delay_us(500U);      /* ring-down before retry */
    }
    return -1;
}

uint16_t tiku_cpu_ra8p1_cac_measure(uint8_t target, uint8_t reference,
                                    uint8_t ref_div)
{
    unsigned long spins;
    uint16_t count;

    TIKU_REG32(RA8P1_MSTPCRC) &= ~RA8P1_MSTPC_CAC;

    TIKU_REG8(RA8P1_CACR0) = 0U;        /* stop before reprogramming */
    TIKU_REG8(RA8P1_CAICR) = (uint8_t)(RA8P1_CAICR_FERRFCL |
                                       RA8P1_CAICR_MENDFCL |
                                       RA8P1_CAICR_OVFFCL);
    TIKU_REG8(RA8P1_CACR1) = (uint8_t)RA8P1_CACR1_FMCS(target);
    TIKU_REG8(RA8P1_CACR2) = (uint8_t)(RA8P1_CACR2_RPS_INT |
                                       RA8P1_CACR2_RSCS(reference) |
                                       RA8P1_CACR2_RCDS(ref_div));
    /* Widest window: the bounds registers gate the FERRF flag only, and this
     * function reports the count rather than a pass/fail verdict. */
    TIKU_REG16(RA8P1_CAULVR) = 0xFFFFU;
    TIKU_REG16(RA8P1_CALLVR) = 0x0000U;

    TIKU_REG8(RA8P1_CACR0) = (uint8_t)RA8P1_CACR0_CFME;

    for (spins = 8000000UL; spins != 0UL; spins--) {
        if (TIKU_REG8(RA8P1_CASTR) & RA8P1_CASTR_MENDF) {
            break;
        }
    }
    if (spins == 0UL || (TIKU_REG8(RA8P1_CASTR) & RA8P1_CASTR_OVFF)) {
        TIKU_REG8(RA8P1_CACR0) = 0U;
        return 0U;                      /* never completed, or overflowed */
    }

    count = TIKU_REG16(RA8P1_CACNTBR);
    TIKU_REG8(RA8P1_CACR0) = 0U;
    return count;
}

/*
 * OPERATING POINTS.
 *
 * One table entry per rung, because the alternative -- a function per
 * frequency -- is how the 240 path ended up with the ceilings, the wait
 * states and the private clocks each written down in a different place.
 * Every field here is derived from UM Table 9.2, and the constraints that
 * bound them are listed beside the encodings in tiku_ra8p1_regs.h.
 *
 * The dividers are chosen so that everything except the core lands on the
 * SAME rate at every rung.  That is not tidiness: it means a rung change
 * cannot alter SDRAM timing, the flash eye, the console divisor or any
 * peripheral's idea of time, so when a rung misbehaves the core clock is the
 * only thing that changed.  It also makes the pair of benchmarks meaningful,
 * since MRAM fetch is pinned while the core doubles.
 */
#define DIV1  0U
#define DIV2  1U
#define DIV4  2U
#define DIV8  3U
#define DIV16 4U

/**
 * @brief SCKDIVCR word for a tree whose bus divisors scale with @p s.
 *
 * @param s  Scale: 1 at PLL1P 240, 2 at 480, 4 at 1000
 */
#define SCKDIVCR_TREE(s)                                                   \
    (((uint32_t)(DIV2 + (s)) << RA8P1_SCKDIVCR_MRPCK_SHIFT) |  /* 120 */   \
     ((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR_ICK_SHIFT)   |  /* 240 */   \
     ((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR_PCKE_SHIFT)  |  /* 240 */   \
     ((uint32_t)(DIV2 + (s)) << RA8P1_SCKDIVCR_BCK_SHIFT)   |  /* 120 */   \
     ((uint32_t)(DIV2 + (s)) << RA8P1_SCKDIVCR_PCKA_SHIFT)  |  /* 120 */   \
     ((uint32_t)(DIV4 + (s)) << RA8P1_SCKDIVCR_PCKB_SHIFT)  |  /*  60 */   \
     ((uint32_t)(DIV2 + (s)) << RA8P1_SCKDIVCR_PCKC_SHIFT)  |  /* 120 */   \
     ((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR_PCKD_SHIFT))    /* 240 */

/*
 * NPUCK sits at /1 relative to ICLK, not /2: UM Table 9.2 requires
 * NPUCLK >= ICLK.  An NPU clocked below ICLK is out of spec and silent
 * about it while the Ethos-U55 is undriven.
 */
#define SCKDIVCR2_TREE(s, cpu0div)                                                    \
    (((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR2_MRICK_SHIFT)  |  /* 240 */   \
     ((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR2_NPUCK_SHIFT)  |  /* 240 */   \
     ((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR2_CPUCK1_SHIFT) |  /* 240 */   \
     ((uint32_t)(cpu0div)     << RA8P1_SCKDIVCR2_CPUCK0_SHIFT))

/*
 * Only rungs PROVEN on hardware are offered; today that is all three.  The
 * flag stays because the next rung added starts unproven, and a `freq` verb
 * that resets the board is worse than one that refuses.  Build with
 * -DTIKU_RA8P1_FREQ_UNPROVEN=1 to select unproven entries while bringing
 * one up.
 */

/** @brief One selectable rung of the clock ladder. */
typedef struct {
    unsigned int  mhz;        /**< core rate, and what `freq` names        */
    uint8_t       proven;     /**< 1 when demonstrated on hardware         */
    uint8_t       plidiv;     /**< PLL input divider code                  */
    uint16_t      pllmul;     /**< PLL multiplier, whole part              */
    uint8_t       pllmulnf;   /**< PLL multiplier fraction code            */
    uint8_t       plodivp;    /**< PLL1P output divider code               */
    uint8_t       scale;      /**< bus-divider scale; see SCKDIVCR_TREE    */
    uint8_t       scickdiv;   /**< SCICLK divider code (ceiling 120 MHz)   */
    uint16_t      mrc_mhz;    /**< what MRAM is told MRICLK will be        */
    uint8_t       cpuck0div;  /**< CPUCK0 divider code; /1 rides PLL1P     */
    uint8_t       vscm;       /**< VDD target: VSCR_2 to 600, VSCR_1 above */
    unsigned long sci_hz;     /**< what the divider makes SCICLK           */
} ra8p1_opoint_t;

/*
 * x40 is the multiplier FLOOR, so a 24 MHz reference at PLIDIV /1 pins the
 * VCO at 960 MHz and 240 is simply 960/4.  480 is therefore one field --
 * PLODIVP /4 -> /2 -- with every bus divisor doubled to stand still.
 *
 * 1000 cannot come off that VCO: PLODIVP has no /1, so 960/2 = 480 is the
 * most it yields.  It needs a 2000 MHz VCO instead, which the 8-to-24 MHz
 * input window reaches cleanly at 8 MHz (PLIDIV /3) times an integer 250 --
 * no fractional multiplier, and inside the 960-2400 VCO range.
 */
/*
 * No MOCO rung, and that is a decision rather than an omission.  Returning to
 * the 8 MHz boot clock at runtime would put SCICLK at 8 MHz too, where the
 * house 115200 misses by +8.5% -- far outside what a UART frames -- so the
 * verb would trade the shell for the rung.  Nothing is lost by refusing it:
 * 8 MHz IS the state the part boots in, before anything calls freq, so an
 * idle-floor measurement simply declines to raise the clock.
 */
static const ra8p1_opoint_t opoints[] = {
    /*
     * Every rung runs at VSCR_1, including this one that VSCR_2 would carry:
     * the voltage transition never completes while CPU1 is active, and a
     * single boot-time setting removes runtime DVFS from every rung change.
     */
    { 240U, 1U, RA8P1_PLIDIV_1, 40U,  RA8P1_PLLMULNF_0, RA8P1_PLODIV_4,
      0U, RA8P1_CKDIV_2,  240U, DIV1, RA8P1_VSCR_VSCM_1, 120000000UL },
    { 480U, 1U, RA8P1_PLIDIV_1, 40U,  RA8P1_PLLMULNF_0, RA8P1_PLODIV_2,
      1U, RA8P1_CKDIV_4,  240U, DIV1, RA8P1_VSCR_VSCM_1, 120000000UL },
    /* SCICLK /8 would be 125, over its 120 ceiling; /10 = 100 is the fastest
     * legal setting at this rung, and the baud divisor follows sci_hz. */
    { 1000U, 1U, RA8P1_PLIDIV_3, 250U, RA8P1_PLLMULNF_0, RA8P1_PLODIV_2,
      2U, RA8P1_CKDIV_10, 250U, DIV1, RA8P1_VSCR_VSCM_1, 100000000UL },
};

/** @brief Core rate at the rung the port boots into. */
#define RA8P1_PLL_CPU_HZ    240000000UL

/**
 * @brief Bring PLL1 up on the main oscillator and switch the tree onto it.
 *
 * Follows the manual's own ordering (UM Table 9.7): memory told its new
 * frequency BEFORE the clock rises, dividers set before the switch, source
 * switched last.
 *
 * @return 0 on success, -1 when the oscillator or the PLL never stabilised
 */
static int pll_up(const ra8p1_opoint_t *op)
{
    unsigned long spins;
    uint8_t       i;

    STEP(1);
    if (tiku_cpu_ra8p1_mosc_start() != 0) {
        FAILREC(0xF100UL);
        return -1;
    }
    STEP(2);

    clock_protect(1);

    /*
     * The prefetch buffer comes down across the change and goes back up
     * after (UM 60.4.3).  The three read-backs are the manual's, and they are
     * there because the disable has to be VISIBLE to the MRAM controller
     * before the clock moves under it -- a posted write that is still in
     * flight would not be.
     */
    TIKU_REG8(RA8P1_MRCPFB) = 0U;
    for (i = 0U; i < 3U; i++) {
        (void)TIKU_REG8(RA8P1_MRCPFB);
    }
    STEP(3);

    /*
     * Park on MOCO before touching the PLL.  PLLCCR is not writable while
     * the PLL runs, and the PLL cannot be stopped while the system clock is
     * sourced from it, so stopping it directly kills the clock this code
     * executes on.  SCICLK comes back to MOCO too, or the console loses its
     * source mid-sequence; bytes in flight are lost either way and the
     * divisor is recomputed at the end.
     */
    TIKU_REG8(RA8P1_SCICKCR) |= (uint8_t)RA8P1_SCICKCR_SREQ;
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) == 0U) { }
    TIKU_REG8(RA8P1_SCICKDIVCR) = (uint8_t)RA8P1_CKDIV_1;
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)(RA8P1_SCICKCR_SREQ |
                                         RA8P1_SCICKSEL_MOCO);
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)RA8P1_SCICKSEL_MOCO;
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) != 0U) { }
    STEP(4);

    /* Undivided is safe at MOCO's 8 MHz: every ceiling in Table 9.2 is far
     * above it, so no domain is out of spec during the window. */
    TIKU_REG32(RA8P1_SCKDIVCR)  = 0UL;
    TIKU_REG16(RA8P1_SCKDIVCR2) = 0U;
    TIKU_REG8(RA8P1_SCKSCR) = (uint8_t)TIKU_RA8P1_CKSEL_MOCO;
    while ((TIKU_REG8(RA8P1_SCKSCR) & RA8P1_SCKSCR_CKSEL_MASK) !=
           TIKU_RA8P1_CKSEL_MOCO) { }
    cpu_hz_now = TIKU_RA8P1_MOCO_HZ;
    STEP(5);

    /* Memory first.  MRAM picks its read wait states from the frequency it is
     * TOLD, so telling it after the clock rose would mean running a whole
     * window of fetches at too few waits.  (Going the other way the manual
     * inverts this; every rung here is entered from the boot clock, so this
     * is always the speed-up order.)
     *
     * Write until it reads back, per the manual's own flow (Figure 60.5
     * loops the write): the notification can miss on a first attempt, so a
     * single-shot verify rejects transitions the hardware accepts.  Extra
     * MRAM gets the same telling -- MRPCLK is half of MRICLK at every rung
     * in this table. */
    {
        unsigned long tell;

        for (tell = 100UL; tell != 0UL; tell--) {
            TIKU_REG32(RA8P1_MRCFREQ) =
                RA8P1_MRCFREQ_KEY | (uint32_t)op->mrc_mhz;
            if ((TIKU_REG32(RA8P1_MRCFREQ) & RA8P1_MRCFREQ_MHZ_MASK) ==
                (uint32_t)op->mrc_mhz) {
                break;
            }
        }
        if (tell == 0UL) {
            TIKU_REG8(RA8P1_MRCPFB) = (uint8_t)RA8P1_MRCPFB_MPFBEN;
            clock_protect(0);
            FAILREC(0xF200UL);
            return -1;  /* notification never landed; do not raise the clock */
        }
        for (tell = 100UL; tell != 0UL; tell--) {
            TIKU_REG32(RA8P1_MREFREQ) =
                RA8P1_MREFREQ_KEY | (uint32_t)(op->mrc_mhz / 2U);
            if ((TIKU_REG32(RA8P1_MREFREQ) & RA8P1_MRCFREQ_MHZ_MASK) ==
                (uint32_t)(op->mrc_mhz / 2U)) {
                break;
            }
        }
        if (tell == 0UL) {
            TIKU_REG8(RA8P1_MRCPFB) = (uint8_t)RA8P1_MRCPFB_MPFBEN;
            clock_protect(0);
            FAILREC(0xF400UL);
            return -1;
        }
    }
    /* ICLK lands past half of SRAM's 250 MHz ceiling at every rung, so one
     * wait, which is the only setting this bit has. */
    STEP(6);
    TIKU_REG8(RA8P1_SRAMWTSC) = (uint8_t)RA8P1_SRAMWTSC_WTEN;

    TIKU_REG8(RA8P1_PLLCR) = (uint8_t)RA8P1_PLLCR_PLLSTP;   /* stop first */
    /*
     * Wait for the stop to take, not merely be requested: a VCO still
     * spinning down from the old multiplier does not relock when
     * reprogrammed, so a change of PLL recipe needs this gap.  PLLSF
     * clearing is the hardware reporting the PLL down.
     */
    {
        unsigned long spd;

        for (spd = 100000UL; spd != 0UL; spd--) {
            if ((TIKU_REG8(RA8P1_OSCSF) & RA8P1_OSCSF_PLLSF) == 0U) {
                break;
            }
        }
    }
    tiku_cpu_ra8p1_delay_us(100U);

    TIKU_REG32(RA8P1_PLLCCR) = RA8P1_PLLCCR_PLIDIV(op->plidiv) |
                               RA8P1_PLLCCR_PLLMULNF(op->pllmulnf) |
                               RA8P1_PLLCCR_PLLMUL((uint32_t)op->pllmul);
    TIKU_REG16(RA8P1_PLLCCR2) = (uint16_t)(
        RA8P1_PLLCCR2_PLODIVP(op->plodivp) |
        RA8P1_PLLCCR2_PLODIVQ(RA8P1_PLODIV_6) |
        RA8P1_PLLCCR2_PLODIVR(RA8P1_PLODIV_6));
    STEP(7);
    /*
     * VDD next, while the PLL is stopped and the tree is parked at 8 MHz --
     * the one moment both voltage ranges are unconditionally in spec, so the
     * same path serves raising and lowering.  The transition is asynchronous
     * and the CM85 caches must be off while it is in flight (UM 11.7); TCM is
     * not used by this port, so the cache is the whole of that obligation.
     */
    if ((TIKU_REG8(RA8P1_VSCR) & RA8P1_VSCR_VSCM_MASK) != op->vscm) {
        tiku_ra8p1_cache_disable();
        TIKU_REG8(RA8P1_VSCR) = op->vscm;
        /* Bounded: a transition that cannot finish must fail the rung change,
         * not hang the machine that asked for it. */
        for (spins = 4000000UL; spins != 0UL; spins--) {
            if ((TIKU_REG8(RA8P1_VSCR) & RA8P1_VSCR_VSCMTSF) == 0U) {
                break;
            }
        }
        tiku_ra8p1_cache_enable();
        if (spins == 0UL) {
            TIKU_REG8(RA8P1_MRCPFB) = (uint8_t)RA8P1_MRCPFB_MPFBEN;
            clock_protect(0);
            FAILREC(0xF500UL);
            return -1;
        }
    }

    TIKU_REG8(RA8P1_PLLCR) = 0U;                            /* run */

    for (spins = 4000000UL; spins != 0UL; spins--) {
        if (TIKU_REG8(RA8P1_OSCSF) & RA8P1_OSCSF_PLLSF) {
            break;
        }
    }
    if (spins == 0UL) {
        /* Left parked on MOCO with the tree undivided -- slow, but running
         * and answering, which is what a failed rung change should leave. */
        TIKU_REG8(RA8P1_MRCPFB) = (uint8_t)RA8P1_MRCPFB_MPFBEN;
        clock_protect(0);
        FAILREC(0xF300UL | TIKU_REG8(RA8P1_OSCSF));
        return -1;
    }

    STEP(8);
    TIKU_REG32(RA8P1_SCKDIVCR)  = SCKDIVCR_TREE(op->scale);
    /*
     * 16-bit, and the width is load-bearing: SCKDIVCR2 is a 16-bit register
     * at +0x024 and SCKSCR -- the system clock SOURCE -- is the byte at
     * +0x026.  A 32-bit store also writes 0x00 over SCKSCR, and CKSEL 0
     * selects the HOCO, which this part keeps disabled.
     */
    TIKU_REG16(RA8P1_SCKDIVCR2) =
        (uint16_t)SCKDIVCR2_TREE(op->scale, op->cpuck0div);
    TIKU_REG8(RA8P1_SCKSCR) = (uint8_t)TIKU_RA8P1_CKSEL_PLL1P;
    /* Read back: the switch crosses into the clock domain being switched, and
     * continuing before it lands would run the next writes at an unknown
     * rate. */
    while ((TIKU_REG8(RA8P1_SCKSCR) & RA8P1_SCKSCR_CKSEL_MASK) !=
           TIKU_RA8P1_CKSEL_PLL1P) { }

    /*
     * NOPs, and nothing else, for 30 us (UM Figure 9.15).  The DCDC is
     * settling into the stepped-up load, and the manual's word for what the
     * core may do meanwhile is NOP -- no loads, no stores, no peripherals.
     *
     * Sized in iterations of a register-only loop at the new core rate: even
     * fully dual-issued at one per cycle, 120 x mhz iterations is 120 us,
     * four times the requirement and still invisible beside the oscillator
     * waits either side.
     */
    {
        unsigned long settle = 120UL * (unsigned long)op->mhz;

        while (settle-- != 0UL) {
            __asm__ volatile ("nop");
        }
    }
    STEP(9);

    /* The console rides SCICLK, which is still on MOCO and would otherwise
     * stay there -- so the tree moving is exactly when it must be moved too.
     * The manual's handshake: request, wait ready, program, release, wait. */
    TIKU_REG8(RA8P1_SCICKCR) |= (uint8_t)RA8P1_SCICKCR_SREQ;
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) == 0U) { }
    TIKU_REG8(RA8P1_SCICKDIVCR) = op->scickdiv;
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)(RA8P1_SCICKCR_SREQ |
                                         RA8P1_SCICKSEL_PLL1P);
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)RA8P1_SCICKSEL_PLL1P;  /* release */
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) != 0U) { }
    STEP(10);
    sci_hz_now = op->sci_hz;

    /* Prefetch back on: above 100 MHz the manual requires it, and MRICLK is
     * 240 or 250 at every rung here. */
    TIKU_REG8(RA8P1_MRCPFB) = (uint8_t)RA8P1_MRCPFB_MPFBEN;

    clock_protect(0);
    cpu_hz_now = (unsigned long)op->mhz * 1000000UL;
    STEP(11);
    return 0;
}

/** @brief The rung currently established, for falling back to. */
static const ra8p1_opoint_t *cur_op;

/** @brief The table entry for @p mhz, or NULL when there is none. */
static const ra8p1_opoint_t *opoint_of(unsigned int mhz)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)(sizeof(opoints) / sizeof(opoints[0])); i++) {
        if (opoints[i].mhz == mhz) {
            if (opoints[i].proven || (TIKU_RA8P1_FREQ_UNPROVEN + 0)) {
                return &opoints[i];
            }
            return 0;
        }
    }
    return 0;
}

void tiku_cpu_freq_ra8p1_init(unsigned int mhz)
{
    const ra8p1_opoint_t *op = opoint_of(mhz);

    /* Any rate this port cannot produce is refused rather than approximated:
     * `freq` naming a rate the part is not running at is worse than a
     * refusal. */
    if (op == 0) {
        return;
    }

    /*
     * The octal flash picks its bus divider once, at OPI entry, from the
     * PLL1P rate live then.  Moving PLL1P underneath it would overclock
     * OM_SCLK past the device's rating and show up as corrupted reads rather
     * than as a clock fault, so the rung is refused instead.  Re-dividing
     * OCTACLK and re-centring the DQS eye across a live rung change is real
     * work and belongs where it can be proven on hardware, not asserted here.
     */
    if (mhz != (unsigned int)(cpu_hz_now / 1000000UL) &&
        tiku_ra8p1_xflash_in_opi()) {
        return;
    }

#if (TIKU_DRV_CPU1_ENABLE + 0)
    /*
     * Nor while the second core is active, which is the whole time it is: a
     * rung change parks the tree on MOCO and reprogrammes the PLL, CPU1
     * fetches through that same tree, a halted payload still FETCHES, and
     * nothing returns CPU1 to power gating.  There is no window in which the
     * tree can move underneath it, so launching CPU1 pins the rung until a
     * reset -- a real constraint, refused rather than half-honoured.
     */
    if (mhz != (unsigned int)(cpu_hz_now / 1000000UL) &&
        tiku_ra8p1_cpu1_active()) {
        return;
    }
#endif

    /*
     * A rung change either takes effect or leaves the tree where it was.
     *
     * pll_up() parks on MOCO before it touches the PLL, so a failure exits
     * with the tree at 8 MHz and every consumer still tuned for the old rate
     * -- a console too fast to frame and a tick counting wrong, which is
     * indistinguishable from a dead board.  Fall back to the rung already
     * established, and only if that cannot be re-entered accept the parked
     * tree and retune everything down to it.
     */
    /*
     * Atomic.  An interrupt taken mid-change means exception entry through
     * a half-reprogrammed machine: a parked 8 MHz tree, caches off inside
     * the voltage transition, MRAM waits mid-retell.  The vendor BSP masks
     * interrupts around its clock changes for the same reason.
     */
    {
        uint32_t primask;

        __asm__ volatile ("mrs %0, primask" : "=r" (primask));
        __asm__ volatile ("cpsid i" ::: "memory");

        if (pll_up(op) != 0) {
            if (cur_op == 0 || pll_up(cur_op) != 0) {
                tiku_cpu_ra8p1_spin_invalidate();
                (void)tiku_ra8p1_clock_arch_retune(cpu_hz_now);
                tiku_uart_init();
            }
            if (primask == 0UL) {
                __asm__ volatile ("cpsie i" ::: "memory");
            }
            return;
        }
        cur_op = op;

        /* Still masked: the retune and console re-init below are part of
         * the same inconsistent window -- a tick against the stale reload or
         * an RX against a half-initialised SCI. */
        tiku_cpu_ra8p1_spin_invalidate();
        STEP(12);
        (void)tiku_ra8p1_clock_arch_retune(cpu_hz_now);
        STEP(13);
        tiku_uart_init();
        STEP(14);

        if (primask == 0UL) {
            __asm__ volatile ("cpsie i" ::: "memory");
        }
        return;
    }

}

int tiku_cpu_freq_ra8p1_supported(unsigned int mhz)
{
    return (opoint_of(mhz) != 0) ? 1 : 0;
}

void tiku_cpu_boot_ra8p1_init(void)
{
    /* Deliberately empty: the tree is raised explicitly, not at boot.  See
     * the header. */
}

void tiku_cpu_ra8p1_clock_probe(tiku_ra8p1_clock_t *out)
{
    uint32_t divcr;

    if (out == 0) { return; }

    divcr = TIKU_REG32(RA8P1_SCKDIVCR);
    out->cksel     = (uint8_t)(TIKU_REG8(RA8P1_SCKSCR) &
                               RA8P1_SCKSCR_CKSEL_MASK);
    out->iclk_div  = div_of(divcr >> RA8P1_SCKDIVCR_ICK_SHIFT);
    out->pclka_div = div_of(divcr >> RA8P1_SCKDIVCR_PCKA_SHIFT);
    out->pclkb_div = div_of(divcr >> RA8P1_SCKDIVCR_PCKB_SHIFT);
    out->src_hz    = src_hz_of(out->cksel);
    out->iclk_hz   = out->src_hz / out->iclk_div;
    out->pclka_hz  = out->src_hz / out->pclka_div;
}

unsigned long tiku_cpu_ra8p1_clock_get_hz(void)
{
    return cpu_hz_now;
}

unsigned long tiku_cpu_ra8p1_pclka_get_hz(void)
{
    /* PCLKA and CPUCLK0 divide the SAME source, so recover that source from
     * the core rate and its own divider before applying PCKA's. */
    unsigned long src = cpu_hz_now *
        div_of(TIKU_REG16(RA8P1_SCKDIVCR2) >> RA8P1_SCKDIVCR2_CPUCK0_SHIFT);

    return src / div_of(TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_PCKA_SHIFT);
}

unsigned long tiku_cpu_ra8p1_bclk_get_hz(void)
{
    unsigned long src = cpu_hz_now *
        div_of(TIKU_REG16(RA8P1_SCKDIVCR2) >> RA8P1_SCKDIVCR2_CPUCK0_SHIFT);

    return src / div_of(TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_BCK_SHIFT);
}

unsigned long tiku_cpu_ra8p1_pclkb_get_hz(void)
{
    unsigned long src = cpu_hz_now *
        div_of(TIKU_REG16(RA8P1_SCKDIVCR2) >> RA8P1_SCKDIVCR2_CPUCK0_SHIFT);

    return src / div_of(TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_PCKB_SHIFT);
}

unsigned long tiku_cpu_ra8p1_pclkd_get_hz(void)
{
    unsigned long src = cpu_hz_now *
        div_of(TIKU_REG16(RA8P1_SCKDIVCR2) >> RA8P1_SCKDIVCR2_CPUCK0_SHIFT);

    return src / div_of(TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_PCKD_SHIFT);
}

unsigned long tiku_cpu_ra8p1_sciclk_get_hz(void)
{
    return sci_hz_now;
}

void tiku_cpu_boot_ra8p1_power_wfi_enter(void)
{
    /*
     * Sleep mode: WFI with SBYCR.SSBY clear, which is the reset state and the
     * only mode this port enters.  The core stops, every clock keeps running,
     * and any unmasked interrupt resumes it -- so the tick, the console and an
     * armed htimer all still wake it.
     *
     * Software Standby (SSBY=1) would be deeper, but it stops the clocks, so
     * coming back needs a wake source the ICU is told to honour while stopped.
     * Entering it before that is wired means a part that never wakes, and the
     * saving cannot be shown until R9 puts a PPK2 on the measurement header.
     * Deliberately not entered here, rather than mapped and hoped for.
     */
    /*
     * Above 240 MHz the core is stepped DOWN to ICLK for the duration of the
     * sleep and restored on wake, which is what the vendor BSP does ("Need to
     * slow CPUCLK down before sleeping if it is above 240MHz").
     *
     * Not required for correctness on this port: 1 GHz survives a soak of
     * tick-paced idles without it.  Kept because the vendor states it as a
     * requirement, which a soak cannot disprove for a marginal effect, and
     * because idling a 1 GHz clock tree at ICLK should cost less.  Neither
     * claim is measured.
     *
     * All four SCKDIVCR2 fields go to ICLK's divider rather than only the two
     * the vendor writes: their shape leaves CPUCK1 and NPUCK at /1, which
     * would stand the M33 at PLL1P against its 250 MHz ceiling.  Equal to
     * ICLK satisfies every ordering rule at once and cannot exceed a ceiling
     * that ICLK is already inside.
     *
     * A wake landing before the restore runs its handler at the reduced
     * clock, which is the safe direction: exception entry is exactly what
     * misbehaves at speed.
     */
    uint16_t saved = 0U;
    uint8_t  slowed = 0U;

    if (tiku_cpu_ra8p1_clock_get_hz() > 240000000UL) {
        uint32_t ick = (TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_ICK_SHIFT) & 0xFU;

        saved  = TIKU_REG16(RA8P1_SCKDIVCR2);
        slowed = 1U;
        clock_protect(1);
        TIKU_REG16(RA8P1_SCKDIVCR2) = (uint16_t)(
            (ick << RA8P1_SCKDIVCR2_MRICK_SHIFT)  |
            (ick << RA8P1_SCKDIVCR2_NPUCK_SHIFT)  |
            (ick << RA8P1_SCKDIVCR2_CPUCK1_SHIFT) |
            (ick << RA8P1_SCKDIVCR2_CPUCK0_SHIFT));
        clock_protect(0);
    }

    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("wfi" ::: "memory");

    if (slowed) {
        unsigned long settle;

        clock_protect(1);
        TIKU_REG16(RA8P1_SCKDIVCR2) = saved;
        clock_protect(0);
        /* Let the rail catch up before real work resumes, as on a rung
         * change; NOPs only, and short because the PLL never moved. */
        for (settle = 2000UL; settle != 0UL; settle--) {
            __asm__ volatile ("nop");
        }
    }
}
