/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - RP2350 CPU bring-up
 *
 * Brings CLK_SYS to 150 MHz from a 12 MHz XOSC via PLL_SYS:
 *
 *   XOSC (12 MHz)
 *     -> REFDIV=1
 *     -> VCO target 1500 MHz (FBDIV = 125)
 *     -> POSTDIV1 = 5, POSTDIV2 = 2  -> 1500 / 10 = 150 MHz
 *     -> CLK_SYS = PLL_SYS / 1
 *     -> CLK_PERI = CLK_SYS / 1 (peripheral clock = 150 MHz)
 *
 * Then releases the peripherals we use from reset and prepares the
 * NVIC for use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Cached clock rates                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Cached CLK_SYS frequency in Hz; updated by init/retune. */
static volatile unsigned long g_clk_sys_hz  = 0UL;
/** @brief Cached CLK_PERI frequency in Hz; tracks CLK_SYS on RP2350. */
static volatile unsigned long g_clk_peri_hz = 0UL;
/** @brief Non-zero when the last clock init or retune hit a fault. */
static volatile uint8_t       g_clock_fault = 0U;

/*---------------------------------------------------------------------------*/
/* Internal helpers                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup rp2350_clock_helpers RP2350 internal clock bring-up helpers
 * @brief Bounded spin and step functions used during PLL bring-up.
 *
 * None of these are part of the public HAL; they are called only from
 * tiku_cpu_boot_rp2350_init() and tiku_cpu_freq_rp2350_init().
 */

/** @brief Maximum spin iterations before declaring a timeout (~10 ms). */
#define RP2350_SPIN_TIMEOUT 1000000U

/**
 * @brief Spin until a register bit-mask is set, with a bounded iteration cap.
 *
 * Polls @p reg until (@p *reg & @p mask) is non-zero, or until
 * RP2350_SPIN_TIMEOUT iterations have elapsed. The caller decides how to
 * handle a timeout — typically by falling back to a safe clock source
 * rather than spinning indefinitely.
 *
 * @param reg   Volatile register address to poll
 * @param mask  Bit mask to test
 * @return 1 when the mask matches, 0 on timeout
 */
static int rp2350_spin_until(volatile uint32_t *reg, uint32_t mask) {
    uint32_t i = RP2350_SPIN_TIMEOUT;
    while (i--) {
        if ((*reg) & mask) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Start the 12 MHz crystal oscillator and wait for it to stabilise.
 *
 * Configures XOSC for the 1–15 MHz range and waits for STATUS.STABLE.
 *
 * @return 1 when stable, 0 on timeout
 */
static int rp2350_xosc_init(void) {
    /* Set start-up delay (~1 ms at 12 MHz, multiplied by 256 internally). */
    _RP2350_REG(RP2350_XOSC_STARTUP) = 47U;

    /* Enable XOSC in 1-15 MHz range. */
    _RP2350_REG(RP2350_XOSC_CTRL) =
        RP2350_XOSC_CTRL_ENABLE | RP2350_XOSC_CTRL_FREQ_1_15;

    return rp2350_spin_until((volatile uint32_t *)RP2350_XOSC_STATUS,
                             RP2350_XOSC_STATUS_STABLE);
}

/**
 * @brief Initialise PLL_SYS for 150 MHz (XOSC * 125 / 5 / 2).
 *
 * Takes PLL_SYS out of reset, programs REFDIV=1, FBDIV=125, then
 * powers up the VCO and waits for PLL lock. On success, sets
 * POSTDIV1=5 / POSTDIV2=2 to produce 1500 / 10 = 150 MHz.
 *
 * @return 1 when the PLL locks, 0 on timeout
 */
static int rp2350_pll_sys_init(void) {
    /* Take PLL_SYS out of reset. */
    rp2350_unreset(RP2350_RESETS_PLL_SYS);

    /* REFDIV = 1: PLL ref = 12 MHz. */
    _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_CS) = 1U;

    /* FBDIV = 125 -> VCO = 12 * 125 = 1500 MHz. */
    _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_FBDIV_INT) = 125U;

    /* Power up VCO + PLL main. Leave POSTDIV powered down for now. */
    _RP2350_REG_CLR(RP2350_PLL_SYS_BASE + RP2350_PLL_PWR,
                    RP2350_PLL_PWR_PD | RP2350_PLL_PWR_VCOPD);

    if (!rp2350_spin_until(
            (volatile uint32_t *)(RP2350_PLL_SYS_BASE + RP2350_PLL_CS),
            RP2350_PLL_CS_LOCK)) {
        return 0;
    }

    /* POSTDIV1 = 5, POSTDIV2 = 2 -> 1500 / 10 = 150 MHz. */
    _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_PRIM) =
        (5U << RP2350_PLL_PRIM_POSTDIV1_S)
        | (2U << RP2350_PLL_PRIM_POSTDIV2_S);

    /* Power up POSTDIV. */
    _RP2350_REG_CLR(RP2350_PLL_SYS_BASE + RP2350_PLL_PWR,
                    RP2350_PLL_PWR_POSTDIVPD);
    return 1;
}

/**
 * @brief Switch CLK_REF to XOSC and CLK_SYS to PLL_SYS via glitch-free mux.
 *
 * Performs the three-step CLK_SYS switch described in RP2350 datasheet
 * §5.5.4: SRC=REF, set AUXSRC=PLL_SYS, then SRC=AUX. Also configures
 * CLK_PERI to follow CLK_SYS and sets the CLK_SYS divider to 1.0.
 *
 * @return 1 on success, 0 if any poll times out
 */
static int rp2350_clock_switch(void) {
    /* CLK_REF -> XOSC (so the rest of the system has a known reference). */
    _RP2350_REG(RP2350_CLK_REF_CTRL) = RP2350_CLK_REF_SRC_XOSC;
    if (!rp2350_spin_until((volatile uint32_t *)RP2350_CLK_REF_SELECTED,
                           0x4U)) {
        return 0;
    }

    /* CLK_SYS: glitch-free aux switch per RP2350 datasheet §5.5.4 —
     *   1. SRC = REF (forces glitch-free deselect of any prior AUX)
     *   2. AUXSRC = PLL_SYS
     *   3. SRC = AUX
     */
    _RP2350_REG(RP2350_CLK_SYS_CTRL) = RP2350_CLK_SYS_SRC_REF;
    if (!rp2350_spin_until((volatile uint32_t *)RP2350_CLK_SYS_SELECTED,
                           0x1U)) {
        return 0;
    }

    _RP2350_REG(RP2350_CLK_SYS_CTRL) =
        RP2350_CLK_SYS_SRC_REF | RP2350_CLK_SYS_AUXSRC_PLL_SYS;

    _RP2350_REG(RP2350_CLK_SYS_CTRL) =
        RP2350_CLK_SYS_SRC_AUX | RP2350_CLK_SYS_AUXSRC_PLL_SYS;

    if (!rp2350_spin_until((volatile uint32_t *)RP2350_CLK_SYS_SELECTED,
                           0x2U)) {
        return 0;
    }

    /* CLK_SYS divider = 1.0 (RP2350 CLK_SYS_DIV is 8.16 fixed-point). */
    _RP2350_REG(RP2350_CLK_SYS_DIV) = 0x00010000U;

    /* CLK_PERI: source = CLK_SYS, enabled. */
    _RP2350_REG(RP2350_CLK_PERI_CTRL) =
        RP2350_CLK_PERI_AUXSRC_CLK_SYS | RP2350_CLK_PERI_ENABLE;
    return 1;
}

/**
 * @brief Fall back to 12 MHz XOSC when PLL bring-up fails.
 *
 * Parks CLK_SYS on CLK_REF (already pointing at XOSC) and routes
 * CLK_PERI directly to XOSC so the UART baud-divisor calculation
 * produces a correct result regardless of the CLK_SYS mux state.
 * Called only when rp2350_pll_sys_init() or rp2350_clock_switch()
 * returns 0.
 */
static void rp2350_clock_fallback_xosc(void) {
    /* CLK_SYS = CLK_REF (i.e. XOSC at 12 MHz). */
    _RP2350_REG(RP2350_CLK_SYS_CTRL) = RP2350_CLK_SYS_SRC_REF;
    _RP2350_REG(RP2350_CLK_SYS_DIV)  = 0x00010000U;

    /* CLK_PERI = XOSC directly (skip the CLK_SYS path so we still
     * have a working clock even if the SYS mux is in an odd state). */
    _RP2350_REG(RP2350_CLK_PERI_CTRL) =
        RP2350_CLK_PERI_AUXSRC_XOSC | RP2350_CLK_PERI_ENABLE;
}

/**
 * @brief Release all kernel-used peripherals from reset.
 *
 * Brings IO_BANK0, PADS_BANK0, UART0, TIMER0, TIMER1, and PLL_SYS
 * out of reset in one call. SPI and I2C are released here too so that
 * bus-probe register reads in the stub arch files do not bus-fault.
 */
static void rp2350_unreset_peripherals(void) {
    /* Bring up the peripherals the kernel uses. SPI/I2C are touched
     * by the stub arch files but not actually used; bring them out
     * anyway so register reads in the bus probe code don't bus-fault. */
    rp2350_unreset(RP2350_RESETS_IO_BANK0
                 | RP2350_RESETS_PADS_BANK0
                 | RP2350_RESETS_UART0
                 | RP2350_RESETS_TIMER0
                 | RP2350_RESETS_TIMER1
                 | RP2350_RESETS_PLL_SYS);
}

/**
 * @brief Configure TIMER0 and the watchdog tick generator for 1 us resolution.
 *
 * Programs the TICKS block with CYCLES = 12 (XOSC at 12 MHz = 1 us per
 * 12 cycles), enabling both the TIMER0 tick and the watchdog tick. Also
 * clears TIMER0_PAUSE so the counter starts running immediately.
 */
static void rp2350_setup_1us_tick(void) {
    /* TIMER0 needs a 1 MHz tick from the TICKS block (datasheet §10.6).
     * Pick CYCLES = CLK_REF_HZ / 1_000_000 so the same code works
     * whether we ended up at 150 MHz CLK_SYS (CLK_REF=12 MHz here too,
     * since both are XOSC-derived) or fell back to 12 MHz directly. */
    uint32_t cycles = 12U;       /* XOSC = 12 MHz -> 1 us per 12 cycles */

    _RP2350_REG(RP2350_TICKS_TIMER0_CYCLES) = cycles;
    _RP2350_REG(RP2350_TICKS_TIMER0_CTRL)   = RP2350_TICK_ENABLE;
    (void)rp2350_spin_until((volatile uint32_t *)RP2350_TICKS_TIMER0_CTRL,
                            RP2350_TICK_RUNNING);

    _RP2350_REG(RP2350_TICKS_WATCHDOG_CYCLES) = cycles;
    _RP2350_REG(RP2350_TICKS_WATCHDOG_CTRL)   = RP2350_TICK_ENABLE;
    (void)rp2350_spin_until((volatile uint32_t *)RP2350_TICKS_WATCHDOG_CTRL,
                            RP2350_TICK_RUNNING);

    /* Ensure TIMER0 is actually counting by clearing PAUSE. */
    _RP2350_REG(RP2350_TIMER0_PAUSE) = 0U;
}

/*---------------------------------------------------------------------------*/
/* Public HAL entry points                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Perform RP2350 hardware bring-up: XOSC, PLL_SYS, clocks, peripherals.
 *
 * Called once from the reset handler before main(). Attempts to bring
 * CLK_SYS to 150 MHz via XOSC -> PLL_SYS. If any step times out the
 * system falls back to 12 MHz XOSC so the UART still comes up with a
 * deterministic peripheral clock. Releases all kernel peripherals from
 * reset and starts the 1 us TIMER0 tick. Updates the cached
 * g_clk_sys_hz / g_clk_peri_hz so later callers see the actual rate.
 */
void tiku_cpu_boot_rp2350_init(void) {
    /* Order matters: XOSC up before PLL, PLL locked before CLK_SYS
     * switch, CLK_SYS running before peripherals see their clocks.
     * If anything along the way times out we silently fall back to
     * running CLK_PERI directly off XOSC at 12 MHz so the UART
     * driver still gets a deterministic peripheral clock — much
     * better than infinite-spinning before we can even print.
     *
     * On a fresh ROM hand-off CLK_REF and CLK_SYS are already
     * sourced from ROSC (~12 MHz nominal) so the spin loops start
     * with a working clock either way. */
    int xosc_ok  = rp2350_xosc_init();
    int pll_ok   = xosc_ok && rp2350_pll_sys_init();
    int sys_ok   = pll_ok  && rp2350_clock_switch();

    if (sys_ok) {
        g_clk_sys_hz  = 150000000UL;
        g_clk_peri_hz = 150000000UL;
        g_clock_fault = 0U;
    } else {
        rp2350_clock_fallback_xosc();
        g_clk_sys_hz  = 12000000UL;
        g_clk_peri_hz = 12000000UL;
        g_clock_fault = 1U;
    }

    rp2350_unreset_peripherals();
    rp2350_setup_1us_tick();
}

/*---------------------------------------------------------------------------*/
/* Frequency scaling                                                         */
/*                                                                            */
/* tiku_cpu_boot_rp2350_init() always brings clk_sys up to 150 MHz so the    */
/* rest of the boot sequence has a deterministic clock. tiku_cpu_freq_init  */
/* then optionally retunes PLL_SYS to a different target.                    */
/*                                                                            */
/* Constraints (RP2350 datasheet §8.6.4 PLL_SYS):                            */
/*   VCO in [750, 1600] MHz                                                  */
/*   POSTDIV1, POSTDIV2 each in [1, 7]; recommend POSTDIV1 >= POSTDIV2       */
/*   FBDIV in [16, 320]; with REFDIV=1, ref = XOSC = 12 MHz                  */
/*                                                                            */
/* The default core voltage (1.10 V) supports clk_sys up to ~150 MHz.        */
/* Higher frequencies would need a voltage-regulator bump that is out of     */
/* scope for this port -- the table refuses anything above 150 MHz.          */
/*---------------------------------------------------------------------------*/

/**
 * @brief PLL configuration parameters for one supported CLK_SYS frequency.
 *
 * Used by the rp2350_freq_table look-up. A @c fbdiv of 0 is a sentinel
 * meaning "bypass PLL — use XOSC directly at 12 MHz".
 */
struct rp2350_pll_params {
    unsigned int target_mhz; /**< Target CLK_SYS frequency in MHz */
    uint16_t     fbdiv;      /**< PLL feedback divider; 0 = XOSC bypass */
    uint8_t      postdiv1;   /**< PLL post-divider 1 (1..7) */
    uint8_t      postdiv2;   /**< PLL post-divider 2 (1..7) */
};

/**
 * @brief Lookup table of supported CLK_SYS frequencies.
 *
 * Six entries covering 12, 48, 100, 125, 133, and 150 MHz. 150 MHz is
 * the boot default. The 12 MHz entry (fbdiv == 0) bypasses the PLL and
 * sources CLK_SYS directly from CLK_REF / XOSC; no PLL setting produces
 * a useful 12 MHz output, and the chip is most efficient on XOSC at low
 * frequencies. All other entries use PLL_SYS with REFDIV = 1.
 */
static const struct rp2350_pll_params rp2350_freq_table[] = {
    /* MHz    FBDIV  POSTDIV1  POSTDIV2  -- VCO = 12 * FBDIV */
    {  12,      0,    0,    0  },   /* bypass PLL, clk_sys = XOSC */
    {  48,    100,    5,    5  },   /* VCO 1200, /25 */
    { 100,    100,    4,    3  },   /* VCO 1200, /12 */
    { 125,    125,    4,    3  },   /* VCO 1500, /12 */
    { 133,    133,    6,    2  },   /* VCO 1596, /12 */
    { 150,    125,    5,    2  },   /* VCO 1500, /10 -- default */
};
#define RP2350_FREQ_TABLE_LEN \
    (sizeof(rp2350_freq_table) / sizeof(rp2350_freq_table[0]))

/**
 * @brief Look up PLL parameters for a requested CLK_SYS frequency.
 *
 * @param target_mhz  Desired CLK_SYS in MHz (must match a table entry)
 * @return Pointer to the matching rp2350_pll_params, or NULL if not found
 */
static const struct rp2350_pll_params *
rp2350_lookup_freq(unsigned int target_mhz) {
    unsigned int i;
    for (i = 0; i < RP2350_FREQ_TABLE_LEN; i++) {
        if (rp2350_freq_table[i].target_mhz == target_mhz) {
            return &rp2350_freq_table[i];
        }
    }
    return NULL;
}

/**
 * @brief Park CLK_SYS on CLK_REF so PLL_SYS can be safely reconfigured.
 *
 * Per RP2350 datasheet §5.5.4: set CLK_SYS_SRC = REF and wait for
 * CLK_SYS_SELECTED to confirm. The CPU continues to run off XOSC
 * (~12 MHz) while PLL_SYS is being reprogrammed.
 */
static void rp2350_park_clk_sys_on_ref(void) {
    _RP2350_REG(RP2350_CLK_SYS_CTRL) = RP2350_CLK_SYS_SRC_REF;
    (void)rp2350_spin_until((volatile uint32_t *)RP2350_CLK_SYS_SELECTED,
                            0x1U);
}

/**
 * @brief Reprogram PLL_SYS to a new FBDIV and POSTDIV configuration.
 *
 * Powers down the full PLL, programs the new dividers, powers up the VCO,
 * waits for lock, then powers up the post-divider to produce the output
 * frequency. The caller must park CLK_SYS on CLK_REF before calling this
 * so that the CPU keeps running off XOSC during the retune window.
 *
 * @param fbdiv    PLL feedback divider (new target; REFDIV = 1)
 * @param postdiv1 PLL post-divider 1 (1..7)
 * @param postdiv2 PLL post-divider 2 (1..7)
 * @return 1 when PLL locks, 0 on timeout
 */
static int rp2350_pll_sys_retune(uint16_t fbdiv,
                                 uint8_t postdiv1, uint8_t postdiv2) {
    /* Power down the whole PLL so we can reprogram safely. */
    _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_PWR) =
        RP2350_PLL_PWR_PD | RP2350_PLL_PWR_VCOPD |
        RP2350_PLL_PWR_POSTDIVPD | RP2350_PLL_PWR_DSMPD;

    /* REFDIV = 1: PLL ref = 12 MHz. */
    _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_CS) = 1U;

    /* New FBDIV. */
    _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_FBDIV_INT) = fbdiv;

    /* Power up VCO + main, leave postdiv off until VCO locks. */
    _RP2350_REG_CLR(RP2350_PLL_SYS_BASE + RP2350_PLL_PWR,
                    RP2350_PLL_PWR_PD | RP2350_PLL_PWR_VCOPD);

    if (!rp2350_spin_until(
            (volatile uint32_t *)(RP2350_PLL_SYS_BASE + RP2350_PLL_CS),
            RP2350_PLL_CS_LOCK)) {
        return 0;
    }

    /* New POSTDIV1/POSTDIV2. */
    _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_PRIM) =
        ((uint32_t)postdiv1 << RP2350_PLL_PRIM_POSTDIV1_S) |
        ((uint32_t)postdiv2 << RP2350_PLL_PRIM_POSTDIV2_S);

    /* Power up the post-divider so the configured output is generated. */
    _RP2350_REG_CLR(RP2350_PLL_SYS_BASE + RP2350_PLL_PWR,
                    RP2350_PLL_PWR_POSTDIVPD);
    return 1;
}

/**
 * @brief Switch CLK_SYS back to PLL_SYS via the glitch-free aux mux.
 *
 * Completes the three-step sequence: AUX source = PLL_SYS, then SRC = AUX,
 * then poll CLK_SYS_SELECTED bit 1.
 *
 * @return 1 when the switch is confirmed, 0 on timeout
 */
static int rp2350_clk_sys_back_on_pll(void) {
    _RP2350_REG(RP2350_CLK_SYS_CTRL) =
        RP2350_CLK_SYS_SRC_REF | RP2350_CLK_SYS_AUXSRC_PLL_SYS;
    _RP2350_REG(RP2350_CLK_SYS_CTRL) =
        RP2350_CLK_SYS_SRC_AUX | RP2350_CLK_SYS_AUXSRC_PLL_SYS;
    return rp2350_spin_until(
        (volatile uint32_t *)RP2350_CLK_SYS_SELECTED, 0x2U);
}

/**
 * @brief Scale CLK_SYS to @p target_mhz at runtime.
 *
 * Looks up the requested frequency in rp2350_freq_table. If found,
 * parks CLK_SYS on XOSC, reprograms PLL_SYS (or bypasses it for 12 MHz),
 * then switches CLK_SYS back to PLL_SYS. Updates the cached clock rates
 * and clears g_clock_fault on success. On any failure the system is left
 * on XOSC at 12 MHz and g_clock_fault is set.
 *
 * Maximum supported frequency is 150 MHz (limited by the default 1.10 V
 * core voltage). An unsupported target leaves the boot clock in place
 * and sets g_clock_fault without retrying.
 *
 * @param target_mhz  Desired CLK_SYS frequency in MHz (12, 48, 100, 125,
 *                    133, or 150)
 */
void tiku_cpu_freq_rp2350_init(unsigned int target_mhz) {
    const struct rp2350_pll_params *p = rp2350_lookup_freq(target_mhz);

    if (p == NULL) {
        /* Unsupported target -- leave the boot default in place and
         * mark the clock-fault flag so /sys/clock observers can see
         * that the requested rate wasn't honoured. */
        g_clock_fault = 1U;
        return;
    }

    /* No-op if the boot init already produced this frequency. The
     * boot path always brings clk_sys up to 150 MHz, so a 150 MHz
     * target hits this fast path; other targets actually retune. */
    if (g_clk_sys_hz == (unsigned long)target_mhz * 1000000UL) {
        g_clock_fault = 0U;
        return;
    }

    if (p->fbdiv == 0U) {
        /* Bypass PLL: clk_sys = clk_ref = XOSC = 12 MHz. */
        rp2350_park_clk_sys_on_ref();

        /* Power the PLL down to save the ~few-mW it would otherwise
         * burn idle. clk_peri retargets to XOSC directly so it doesn't
         * silently fall to whatever clk_sys was before. */
        _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_PWR) =
            RP2350_PLL_PWR_PD | RP2350_PLL_PWR_VCOPD |
            RP2350_PLL_PWR_POSTDIVPD | RP2350_PLL_PWR_DSMPD;
        _RP2350_REG(RP2350_CLK_PERI_CTRL) =
            RP2350_CLK_PERI_AUXSRC_XOSC | RP2350_CLK_PERI_ENABLE;

        g_clk_sys_hz  = 12000000UL;
        g_clk_peri_hz = 12000000UL;
        return;
    }

    /* Re-tune path. Park clk_sys on XOSC so the CPU keeps running off a
     * known-good clock while we touch PLL_SYS. */
    rp2350_park_clk_sys_on_ref();

    if (!rp2350_pll_sys_retune(p->fbdiv, p->postdiv1, p->postdiv2)) {
        /* PLL didn't lock at the new params. Leave clk_sys parked on
         * XOSC and flag the fault. clk_peri stays on clk_sys (= XOSC). */
        _RP2350_REG(RP2350_CLK_PERI_CTRL) =
            RP2350_CLK_PERI_AUXSRC_XOSC | RP2350_CLK_PERI_ENABLE;
        g_clk_sys_hz  = 12000000UL;
        g_clk_peri_hz = 12000000UL;
        g_clock_fault = 1U;
        return;
    }

    if (!rp2350_clk_sys_back_on_pll()) {
        g_clk_sys_hz  = 12000000UL;
        g_clk_peri_hz = 12000000UL;
        g_clock_fault = 1U;
        return;
    }

    /* clk_peri stays sourced from clk_sys (the boot init configured it
     * that way) so it tracks the new clk_sys automatically. Just refresh
     * the cached values so UART baud + SPI baud + clock-rate VFS reads
     * see the new frequency. */
    g_clk_sys_hz  = (unsigned long)target_mhz * 1000000UL;
    g_clk_peri_hz = (unsigned long)target_mhz * 1000000UL;
    g_clock_fault = 0U;
}

/**
 * @brief Enter low-power sleep via WFI (Wait For Interrupt).
 *
 * Issues a single Cortex-M33 WFI instruction. The CPU resumes
 * on the next unmasked interrupt. Used by the TikuOS idle path.
 */
void tiku_cpu_boot_rp2350_power_wfi_enter(void) {
    __asm__ volatile ("wfi" ::: "memory");
}

/**
 * @brief Return the current CLK_SYS frequency in Hz.
 *
 * Returns the cached value set by the most recent clock init or retune.
 * 150 000 000 after a successful PLL bring-up; 12 000 000 on fallback.
 *
 * @return CLK_SYS frequency in Hz
 */
unsigned long tiku_cpu_rp2350_clock_get_hz(void) {
    return g_clk_sys_hz;
}

/**
 * @brief Return the current CLK_PERI (peripheral clock) frequency in Hz.
 *
 * On RP2350 CLK_PERI tracks CLK_SYS; both caches are updated together.
 * Maps to the MSP430 SMCLK abstraction used by the UART baud driver.
 *
 * @return CLK_PERI frequency in Hz
 */
unsigned long tiku_cpu_rp2350_smclk_get_hz(void) {
    return g_clk_peri_hz;
}

/**
 * @brief Return the ACLK-equivalent frequency in Hz.
 *
 * RP2350 has no always-on low-frequency auxiliary clock analogous to
 * MSP430 ACLK. Returns 0 to signal "not available" to callers that
 * query it via the clock HAL.
 *
 * @return 0 (no ACLK on RP2350)
 */
unsigned long tiku_cpu_rp2350_aclk_get_hz(void) {
    /* No always-on low-frequency clock on RP2350. */
    return 0UL;
}

/**
 * @brief Report whether the last clock init or retune encountered a fault.
 *
 * Set when an unsupported frequency was requested, or when any PLL or
 * mux step timed out and the system fell back to XOSC. Cleared on
 * a successful init or retune.
 *
 * @return 1 if a clock fault is recorded, 0 otherwise
 */
int tiku_cpu_rp2350_clock_has_fault(void) {
    return g_clock_fault ? 1 : 0;
}
