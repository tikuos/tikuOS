/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - RA8P1 clock tree and operating points.
 *
 * Moves the tree from the 8 MHz MOCO boot clock to one of three PLL1P rungs
 * (240, 480 and 1000 MHz) and reports each clock's rate from the established
 * rung and the live divider registers.
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

#include <stdint.h>

#include <kernel/memory/tiku_mem.h>

#ifndef TIKU_RA8P1_FREQ_UNPROVEN
#define TIKU_RA8P1_FREQ_UNPROVEN 0
#endif

#if (TIKU_RA8P1_FREQ_UNPROVEN + 0)
/*
 * The last step a rung change reached, in retained SRAM so it survives a
 * reset during the change.  Only a TIKU_RA8P1_FREQ_UNPROVEN build has it.
 */
TIKU_RETAINED volatile uint32_t tiku_ra8p1_freq_step;
/* Why the last rung change failed, in bits 15:8: 0xF1 MOSC, 0xF2 MRCFREQ,
 * 0xF3 PLL lock timeout (bits 7:0 hold OSCSF), 0xF4 MREFREQ, 0xF5 VDD
 * transition.  A fallback that succeeds leaves it in place. */
TIKU_RETAINED volatile uint32_t tiku_ra8p1_freq_fail;
#define STEP(n)  do { tiku_ra8p1_freq_step = (n); } while (0)
#define FAILREC(v) do { tiku_ra8p1_freq_fail = (v); } while (0)
#else
#define STEP(n)  do { } while (0)
#define FAILREC(v) do { } while (0)
#endif

/** @brief Empty default for builds without tiku_htimer_arch.c, which
 *         overrides it. */
__attribute__((weak)) void tiku_ra8p1_htimer_arch_retune(void)
{
}

/** @brief Core clock rate now: the rung's rate, or MOCO's while on MOCO. */
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
 * @brief Rate of the selected system clock source.
 *
 * MOCO, LOCO and the board's crystals report their nominal rates; PLL1P
 * reports the rate this driver last established.  HOCO (trimmed by option
 * bytes) and PLL2P (not programmed by this port) report 0.
 *
 * @param cksel  SCKSCR.CKSEL value
 * @return Source rate in Hz, or 0 for HOCO, PLL2P and unknown codes
 */
static unsigned long src_hz_of(uint8_t cksel)
{
    switch (cksel) {
    case TIKU_RA8P1_CKSEL_MOCO:   return TIKU_RA8P1_MOCO_HZ;
    case TIKU_RA8P1_CKSEL_LOCO:   return 32768UL;
    case TIKU_RA8P1_CKSEL_MAIN:   return TIKU_BOARD_MOSC_HZ;
    case TIKU_RA8P1_CKSEL_SUBCLK: return TIKU_BOARD_SUBCLK_HZ;
    /* CPUCK0 divides PLL1P by one at every rung, so the established core
     * rate is PLL1P's rate.  Before the first rung change the tree is on
     * MOCO, and this case is not reached. */
    case TIKU_RA8P1_CKSEL_PLL1P:  return cpu_hz_now;
    default:                      return 0UL;
    }
}

/**
 * @brief Unlock or relock the clock-generation registers.
 *
 * PRCR_S ignores a write without its key, and the clock registers it guards
 * then ignore theirs: the oscillator never starts.
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
     * Up to three attempts: SYSRESETREQ re-asserts MOSTP while the crystal
     * is still ringing, and a decaying resonator does not always reach the
     * stability flag within one wait.  A failed attempt stops the oscillator
     * and waits 500 us for it to ring down.  A board with no crystal gets -1.
     */
    for (tries = 0U; tries < 3U; tries++) {
        clock_protect(1);
        /* MOMCR is written before MOSTP is cleared (UM 9, MOSCCR
         * note 1). */
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
    /* Widest bounds: CAULVR and CALLVR only set FERRF, which this function
     * does not use. */
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
 * Operating points: one table entry per rung, holding its PLL settings, bus
 * divider step, SCICLK divider, MRAM frequency and VDD range.  The values
 * follow UM Table 9.2; tiku_ra8p1_regs.h lists the constraints beside the
 * encodings.
 *
 * The bus divider codes step with the rung, so the 240 and 480 rungs give
 * the same rates below the core: ICLK 240, PCLKA 120 and PCLKB 60 MHz, and
 * SCICLK 120 MHz.  At 1000 the same step gives 250, 125 and 62.5 MHz, and
 * SCICLK 100 MHz.  OCTACLK divides PLL1P on its own, so a rung change is
 * refused while the octal flash is in OPI mode.
 */
#define DIV1  0U
#define DIV2  1U
#define DIV4  2U
#define DIV8  3U
#define DIV16 4U

/**
 * @brief SCKDIVCR word for a tree whose bus divisor codes step with @p s.
 *
 * The annotated rates are the 240 and 480 rungs; at s = 2 (1000 MHz) every
 * code steps once more, and the fields read 250, 125 and 62.5.
 *
 * @param s  Divider-code step: 0 at PLL1P 240, 1 at 480, 2 at 1000
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
 * SCKDIVCR2 word.  MRICK, NPUCK and CPUCK1 take ICLK's divider code: UM
 * Table 9.2 requires NPUCLK >= ICLK, and an NPU clocked below ICLK raises
 * no error.  CPUCK0 takes its own divider and runs at the core rate.  The
 * annotated rates are the 240 and 480 rungs; at s = 2 they read 250, the
 * rung's mrc_mhz.
 */
#define SCKDIVCR2_TREE(s, cpu0div)                                                    \
    (((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR2_MRICK_SHIFT)  |  /* 240 */   \
     ((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR2_NPUCK_SHIFT)  |  /* 240 */   \
     ((uint32_t)(DIV1 + (s)) << RA8P1_SCKDIVCR2_CPUCK1_SHIFT) |  /* 240 */   \
     ((uint32_t)(cpu0div)     << RA8P1_SCKDIVCR2_CPUCK0_SHIFT))

/*
 * Only entries marked proven are selectable; all three are.  Building with
 * -DTIKU_RA8P1_FREQ_UNPROVEN=1 makes unproven entries selectable too and
 * records each rung change's progress in tiku_ra8p1_freq_step.
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
    uint8_t       vscm;       /**< VSCR.VSCM, the rung's VDD range         */
    unsigned long sci_hz;     /**< what the divider makes SCICLK           */
} ra8p1_opoint_t;

/*
 * x40 is the smallest PLL multiplier, so the 24 MHz crystal at PLIDIV /1
 * gives a 960 MHz VCO: 240 is 960/4, and 480 is 960/2 with every bus divisor
 * doubled.  PLODIVP has no /1, so 1000 needs a 2000 MHz VCO: 24/3 = 8 MHz
 * (PLIDIV /3) times 250, an integer multiplier inside the 960-2400 MHz range.
 */
/*
 * There is no MOCO rung: with SCICLK at 8 MHz the console's 115200 baud is
 * 8.5% off, which a UART cannot frame.  The part boots at 8 MHz, before any
 * rung is selected.
 */
static const ra8p1_opoint_t opoints[] = {
    /*
     * Every rung uses VSCR_1, though VSCR_2 covers up to 600 MHz: the VDD
     * transition does not complete while CPU1 is active, and with one value
     * for all rungs at most the first rung change after reset moves VDD.
     */
    { 240U, 1U, RA8P1_PLIDIV_1, 40U,  RA8P1_PLLMULNF_0, RA8P1_PLODIV_4,
      0U, RA8P1_CKDIV_2,  240U, DIV1, RA8P1_VSCR_VSCM_1, 120000000UL },
    { 480U, 1U, RA8P1_PLIDIV_1, 40U,  RA8P1_PLLMULNF_0, RA8P1_PLODIV_2,
      1U, RA8P1_CKDIV_4,  240U, DIV1, RA8P1_VSCR_VSCM_1, 120000000UL },
    /* SCICLK /10 = 100 MHz, the fastest setting under its 120 MHz ceiling
     * at this rung; the baud divisor follows sci_hz. */
    { 1000U, 1U, RA8P1_PLIDIV_3, 250U, RA8P1_PLLMULNF_0, RA8P1_PLODIV_2,
      2U, RA8P1_CKDIV_10, 250U, DIV1, RA8P1_VSCR_VSCM_1, 100000000UL },
};

/** @brief Core rate at the rung the port boots into. */
#define RA8P1_PLL_CPU_HZ    240000000UL

/**
 * @brief Bring PLL1 up on the main oscillator and switch the tree onto it.
 *
 * Follows UM Table 9.7: the MRAM is told its new frequency before the clock
 * rises, the dividers are set before the switch, and the source moves last.
 *
 * @param op  Rung to enter
 * @return 0 on success; -1 when the MOSC start, an MRAM frequency write, the
 *         VDD transition or the PLL lock fails
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
     * The MRAM prefetch buffer is off across the change (UM 60.4.3).  The
     * manual's three read-backs make the disable reach the MRAM controller
     * before the clock moves; a posted write still in flight would not.
     */
    TIKU_REG8(RA8P1_MRCPFB) = 0U;
    for (i = 0U; i < 3U; i++) {
        (void)TIKU_REG8(RA8P1_MRCPFB);
    }
    STEP(3);

    /*
     * Park on MOCO before touching the PLL: PLLCCR is not writable while the
     * PLL runs, and the PLL cannot stop while it sources the system clock.
     * SCICLK moves to MOCO /1 as well, so the console keeps a clock; bytes in
     * flight are lost, and the divisor is recomputed afterwards.
     */
    TIKU_REG8(RA8P1_SCICKCR) |= (uint8_t)RA8P1_SCICKCR_SREQ;
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) == 0U) { }
    TIKU_REG8(RA8P1_SCICKDIVCR) = (uint8_t)RA8P1_CKDIV_1;
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)(RA8P1_SCICKCR_SREQ |
                                         RA8P1_SCICKSEL_MOCO);
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)RA8P1_SCICKSEL_MOCO;
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) != 0U) { }
    STEP(4);

    /*
     * The source moves to MOCO before the dividers clear; this order keeps a
     * running CPU1 alive across the change.  Clearing the dividers while on
     * PLL1P would run ICLK at the core rate (1 GHz against its 250 MHz
     * ceiling) and put MRICLK and CPUCLK1 below ICLK, which UM 9.2.3 forbids;
     * neither raises a fault, and fetches are corrupted.  On MOCO the old
     * divisors give at most 8 MHz.
     */
    TIKU_REG8(RA8P1_SCKSCR) = (uint8_t)TIKU_RA8P1_CKSEL_MOCO;
    while ((TIKU_REG8(RA8P1_SCKSCR) & RA8P1_SCKSCR_CKSEL_MASK) !=
           TIKU_RA8P1_CKSEL_MOCO) { }
    /* On MOCO the tree can run undivided: every ceiling in Table 9.2 is
     * above 8 MHz. */
    TIKU_REG32(RA8P1_SCKDIVCR)  = 0UL;
    TIKU_REG16(RA8P1_SCKDIVCR2) = 0U;
    cpu_hz_now = TIKU_RA8P1_MOCO_HZ;
    STEP(5);

    /* MRAM picks its read wait states from the frequency written to
     * MRCFREQ, so the new frequency goes in before the clock rises.  Every
     * rung is entered from MOCO, so this is always the speed-up order.
     *
     * Each write repeats until it reads back (UM Figure 60.5): a first
     * attempt can miss.  MREFREQ, for the extra MRAM, gets half the rate:
     * MRPCLK is half of MRICLK at every rung. */
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
    /* ICLK exceeds half of SRAM's 250 MHz ceiling at every rung, so SRAM
     * gets its one wait state. */
    STEP(6);
    TIKU_REG8(RA8P1_SRAMWTSC) = (uint8_t)RA8P1_SRAMWTSC_WTEN;

    TIKU_REG8(RA8P1_PLLCR) = (uint8_t)RA8P1_PLLCR_PLLSTP;   /* stop first */
    /*
     * Wait for PLLSF to clear, the PLL reporting itself stopped, then 100 us
     * more: a VCO still spinning down from the old multiplier does not relock
     * when reprogrammed.
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
     * VDD next, while the PLL is stopped and the tree runs at 8 MHz, where
     * both voltage ranges are in spec.  The CM85 caches must be off during
     * the asynchronous transition (UM 11.7); the rule also covers the TCM,
     * which this port does not use.
     */
    if ((TIKU_REG8(RA8P1_VSCR) & RA8P1_VSCR_VSCM_MASK) != op->vscm) {
        tiku_ra8p1_cache_disable();
        TIKU_REG8(RA8P1_VSCR) = op->vscm;
        /* Bounded: a transition that does not finish fails the rung
         * change. */
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
        /* The tree stays parked on MOCO, undivided, for the caller's
         * fallback. */
        TIKU_REG8(RA8P1_MRCPFB) = (uint8_t)RA8P1_MRCPFB_MPFBEN;
        clock_protect(0);
        FAILREC(0xF300UL | TIKU_REG8(RA8P1_OSCSF));
        return -1;
    }

    STEP(8);
    TIKU_REG32(RA8P1_SCKDIVCR)  = SCKDIVCR_TREE(op->scale);
    /*
     * A 16-bit store: SCKDIVCR2 is 16 bits at +0x024, and SCKSCR, the system
     * clock source, is the byte at +0x026.  A 32-bit store would also write 0
     * to SCKSCR, selecting the HOCO, which is stopped.
     */
    TIKU_REG16(RA8P1_SCKDIVCR2) =
        (uint16_t)SCKDIVCR2_TREE(op->scale, op->cpuck0div);
    TIKU_REG8(RA8P1_SCKSCR) = (uint8_t)TIKU_RA8P1_CKSEL_PLL1P;
    /* Wait for CKSEL to read back PLL1P: the switch crosses clock domains,
     * and the writes after it must run at the new rate. */
    while ((TIKU_REG8(RA8P1_SCKSCR) & RA8P1_SCKSCR_CKSEL_MASK) !=
           TIKU_RA8P1_CKSEL_PLL1P) { }

    /*
     * Only NOPs for 30 us while the DCDC settles into the higher load (UM
     * Figure 9.15): no loads, stores or peripheral accesses.  An iteration
     * takes at least one cycle, so 120 x mhz iterations take at least 120 us
     * at the new core rate.
     */
    {
        unsigned long settle = 120UL * (unsigned long)op->mhz;

        while (settle-- != 0UL) {
            __asm__ volatile ("nop");
        }
    }
    STEP(9);

    /* SCICLK moves from MOCO to PLL1P with the rung's divider, through the
     * manual's handshake: request, wait for ready, program, release, wait. */
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

/** @brief The established rung, the fallback for a failed change; NULL at
 *         boot. */
static const ra8p1_opoint_t *cur_op;

/** @brief Set by a rung change that failed, cleared by one that succeeds. */
static uint8_t clock_fault;

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

    /* A rate with no table entry is ignored. */
    if (op == 0) {
        return;
    }

    /*
     * The octal flash sets its bus divider once, at OPI entry, from the PLL1P
     * rate then.  A higher PLL1P would overclock OM_SCLK past the device's
     * rating and corrupt reads without a fault, so a change to another rung
     * is refused while the flash is in OPI mode.
     */
    if (mhz != (unsigned int)(cpu_hz_now / 1000000UL) &&
        tiku_ra8p1_xflash_in_opi()) {
        return;
    }

    /*
     * A failed pll_up() leaves the tree parked on MOCO with every consumer
     * still tuned for the old rate.  The established rung is entered again;
     * if that also fails, or there is none, the tree stays on MOCO and the
     * delay loop, tick, htimer and console are retuned to it.
     *
     * Interrupts stay masked throughout: an exception mid-change would run
     * on a half-reprogrammed tree (parked clock, caches off for the VDD
     * transition, MRAM mid-notification).
     */
    {
        uint32_t primask;

        __asm__ volatile ("mrs %0, primask" : "=r" (primask));
        __asm__ volatile ("cpsid i" ::: "memory");

        if (pll_up(op) != 0) {
            clock_fault = 1U;
            if (cur_op == 0 || pll_up(cur_op) != 0) {
                tiku_cpu_ra8p1_spin_invalidate();
                (void)tiku_ra8p1_clock_arch_retune(tiku_cpu_ra8p1_iclk_get_hz());
                tiku_ra8p1_htimer_arch_retune();
                tiku_uart_init();
            }
            if (primask == 0UL) {
                __asm__ volatile ("cpsie i" ::: "memory");
            }
            return;
        }
        cur_op = op;
        clock_fault = 0U;

        /* Still masked: until the retune and UART re-init below finish, a
         * tick would use the stale reload and an RX would reach a
         * half-initialised SCI. */
        tiku_cpu_ra8p1_spin_invalidate();
        STEP(12);
        (void)tiku_ra8p1_clock_arch_retune(tiku_cpu_ra8p1_iclk_get_hz());
        tiku_ra8p1_htimer_arch_retune();
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

int tiku_cpu_ra8p1_clock_has_fault(void)
{
    return clock_fault;
}

void tiku_cpu_boot_ra8p1_init(void)
{
    /* Empty: tiku_cpu_freq_ra8p1_init() raises the tree. */
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

unsigned long tiku_cpu_ra8p1_iclk_get_hz(void)
{
    unsigned long src = cpu_hz_now *
        div_of(TIKU_REG16(RA8P1_SCKDIVCR2) >> RA8P1_SCKDIVCR2_CPUCK0_SHIFT);

    return src / div_of(TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_ICK_SHIFT);
}

unsigned long tiku_cpu_ra8p1_aclk_get_hz(void)
{
    /* The LOCO is this part's always-on low-speed source: it runs from reset,
     * no rung change touches it, and the IWDT counts it divided by two. */
    return RA8P1_IWDTCLK_HZ * 2UL;
}

unsigned long tiku_cpu_ra8p1_pclka_get_hz(void)
{
    /* PCLKA and CPUCLK0 divide one source: recover it from the core rate and
     * CPUCK0's divider, then apply PCKA's. */
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
     * Sleep mode: WFI with SBYCR.SSBY clear, the reset state.  The core stops
     * and every clock keeps running, so any unmasked interrupt (the tick,
     * console RX, an armed htimer) wakes it.  Software Standby is not
     * entered: it stops the clocks, and no ICU wake source is set up for it.
     *
     * Above 240 MHz all four SCKDIVCR2 fields take ICLK's divider for the
     * sleep and are restored on wake; the vendor HAL requires CPUCLK at or
     * below 240 MHz in sleep.  Equal to ICLK, every field meets the ordering
     * rules and stays under its ceiling, CPUCK1 and NPUCK included.  An
     * interrupt handler that runs before the restore runs at the reduced
     * clock.
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
        /* NOPs while the rail settles, as on a rung change; short, since
         * the PLL did not move. */
        for (settle = 2000UL; settle != 0UL; settle--) {
            __asm__ volatile ("nop");
        }
    }
}
