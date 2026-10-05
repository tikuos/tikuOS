/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - RP2350 CPU bring-up.
 *
 * Brings CLK_SYS to 150 MHz from the 12 MHz XOSC through PLL_SYS (VCO 1500 MHz,
 * postdividers 5 and 2), runs CLK_PERI from CLK_SYS, releases the kernel's
 * peripherals from reset and starts the 1 us tick; also retunes PLL_SYS.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CACHED CLOCK RATES                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Cached CLK_SYS frequency in Hz; updated by init/retune. */
static volatile unsigned long g_clk_sys_hz  = 0UL;
/** @brief Cached CLK_PERI frequency in Hz; tracks CLK_SYS on RP2350. */
static volatile unsigned long g_clk_peri_hz = 0UL;
/** @brief Non-zero when the last clock init or retune hit a fault. */
static volatile uint8_t       g_clock_fault = 0U;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup rp2350_clock_helpers RP2350 internal clock bring-up helpers
 * @brief Bounded spin and step functions used during PLL bring-up.
 *
 * None of these are part of the public HAL; they are called only from
 * tiku_cpu_boot_rp2350_init() and tiku_cpu_freq_rp2350_init().
 */

/** @brief Iteration cap of rp2350_spin_until(). */
#define RP2350_SPIN_TIMEOUT 1000000U

/**
 * @brief Spin until a register bit-mask is set, with a bounded iteration cap.
 *
 * Polls @p reg until any bit of @p mask is set, for at most
 * RP2350_SPIN_TIMEOUT iterations.
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

    /* Power up the VCO and the PLL; POSTDIV stays powered down until lock. */
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
 * Switches CLK_SYS in three steps: SRC=REF, AUXSRC=PLL_SYS, then SRC=AUX, so
 * the aux mux changes while the glitchless mux holds REF.  Also sets the
 * CLK_SYS divider to 1.0 and runs CLK_PERI from CLK_SYS.
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

    /* CLK_SYS: glitch-free aux switch --
     *   1. SRC = REF (the glitchless mux leaves any prior AUX)
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

    /* CLK_SYS divider = 1.0 (CLK_SYS_DIV is 16.16 fixed-point). */
    _RP2350_REG(RP2350_CLK_SYS_DIV) = 0x00010000U;

    /* CLK_PERI: source = CLK_SYS, enabled. */
    _RP2350_REG(RP2350_CLK_PERI_CTRL) =
        RP2350_CLK_PERI_AUXSRC_CLK_SYS | RP2350_CLK_PERI_ENABLE;
    return 1;
}

/**
 * @brief Fall back to 12 MHz XOSC when PLL bring-up fails.
 *
 * Runs CLK_SYS from CLK_REF and CLK_PERI straight from XOSC, so the UART baud
 * divisor holds whatever state the CLK_SYS mux is in.  Called only when the
 * XOSC start, the PLL init or the clock switch fails.
 */
static void rp2350_clock_fallback_xosc(void) {
    /* CLK_SYS = CLK_REF: the XOSC once rp2350_clock_switch() has moved
     * CLK_REF there, else still the ROSC the boot ROM left. */
    _RP2350_REG(RP2350_CLK_SYS_CTRL) = RP2350_CLK_SYS_SRC_REF;
    _RP2350_REG(RP2350_CLK_SYS_DIV)  = 0x00010000U;

    /* CLK_PERI from XOSC directly, independent of the CLK_SYS mux. */
    _RP2350_REG(RP2350_CLK_PERI_CTRL) =
        RP2350_CLK_PERI_AUXSRC_XOSC | RP2350_CLK_PERI_ENABLE;
}

/**
 * @brief Release the kernel's peripherals from reset.
 *
 * Brings IO_BANK0, PADS_BANK0, UART0, TIMER0, TIMER1 and PLL_SYS out of
 * reset in one call.  The SPI, I2C, ADC and DMA drivers release their own
 * blocks in their init functions.
 */
static void rp2350_unreset_peripherals(void) {
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
 * 12 cycles), enabling both the TIMER0 tick and the watchdog tick.  Also
 * clears TIMER0_PAUSE so the counter starts running immediately.
 */
static void rp2350_setup_1us_tick(void) {
    /* The TICKS block divides clk_ref into TIMER0's 1 MHz tick.  CYCLES =
     * 12 assumes clk_ref is the 12 MHz XOSC, which rp2350_clock_switch()
     * selects; the CLK_SYS frequency does not enter into it. */
    uint32_t cycles = 12U;       /* XOSC = 12 MHz -> 1 us per 12 cycles */

    _RP2350_REG(RP2350_TICKS_TIMER0_CYCLES) = cycles;
    _RP2350_REG(RP2350_TICKS_TIMER0_CTRL)   = RP2350_TICK_ENABLE;
    (void)rp2350_spin_until((volatile uint32_t *)RP2350_TICKS_TIMER0_CTRL,
                            RP2350_TICK_RUNNING);

    _RP2350_REG(RP2350_TICKS_WATCHDOG_CYCLES) = cycles;
    _RP2350_REG(RP2350_TICKS_WATCHDOG_CTRL)   = RP2350_TICK_ENABLE;
    (void)rp2350_spin_until((volatile uint32_t *)RP2350_TICKS_WATCHDOG_CTRL,
                            RP2350_TICK_RUNNING);

    /* Clear PAUSE so TIMER0 counts. */
    _RP2350_REG(RP2350_TIMER0_PAUSE) = 0U;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HAL ENTRY POINTS                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Perform RP2350 hardware bring-up: XOSC, PLL_SYS, clocks, peripherals.
 *
 * Takes CLK_SYS to 150 MHz via XOSC -> PLL_SYS, releases the kernel
 * peripherals from reset, starts the 1 us TIMER0 tick and caches the clock
 * rates.  A step that times out leaves CLK_PERI on XOSC at 12 MHz.
 *
 * @note Call it once at boot, before any peripheral driver starts.
 */
void tiku_cpu_boot_rp2350_init(void) {
    /* XOSC runs before the PLL starts, the PLL locks before CLK_SYS
     * switches, and CLK_SYS runs before the peripherals leave reset.
     * A timeout at any step sends the boot to the 12 MHz fallback, with
     * CLK_PERI on XOSC for the UART.
     *
     * The boot ROM hands over with CLK_REF and CLK_SYS on the ROSC
     * (about 12 MHz), so the spin loops run on a working clock. */
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
/* FREQUENCY SCALING                                                         */
/*---------------------------------------------------------------------------*/

/*
 * tiku_cpu_boot_rp2350_init() brings clk_sys to 150 MHz, or to 12 MHz on its
 * fallback; tiku_cpu_freq_rp2350_init() then retunes PLL_SYS to a table entry.
 *
 * Constraints (RP2350 datasheet §8.6.4 PLL_SYS):
 *   VCO in [750, 1600] MHz
 *   POSTDIV1, POSTDIV2 each in [1, 7]; recommend POSTDIV1 >= POSTDIV2
 *   FBDIV in [16, 320]; with REFDIV=1, ref = XOSC = 12 MHz
 *
 * The default core voltage (1.10 V) supports clk_sys up to 150 MHz.  This
 * file never raises the core voltage, and the table stops at 150 MHz.
 */

/**
 * @brief PLL configuration parameters for one supported CLK_SYS frequency.
 *
 * Used by the rp2350_freq_table look-up.  A @c fbdiv of 0 means the PLL is
 * bypassed and CLK_SYS runs from XOSC at 12 MHz.
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
 * Six entries covering 12, 48, 100, 125, 133 and 150 MHz, with 150 the boot
 * default.  The 12 MHz entry (fbdiv == 0) runs CLK_SYS from CLK_REF (XOSC)
 * with the PLL off: PLL_SYS cannot go below 750 / 49, about 15.3 MHz.
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
 * @brief Park CLK_SYS on CLK_REF while PLL_SYS is reconfigured.
 *
 * Sets CLK_SYS_SRC = REF and waits for CLK_SYS_SELECTED to confirm.  The CPU
 * runs from CLK_REF, the 12 MHz XOSC, while PLL_SYS is reprogrammed.
 */
static void rp2350_park_clk_sys_on_ref(void) {
    _RP2350_REG(RP2350_CLK_SYS_CTRL) = RP2350_CLK_SYS_SRC_REF;
    (void)rp2350_spin_until((volatile uint32_t *)RP2350_CLK_SYS_SELECTED,
                            0x1U);
}

/**
 * @brief Reprogram PLL_SYS to a new FBDIV and POSTDIV configuration.
 *
 * Powers down the full PLL, programs the new dividers, powers up the VCO, waits
 * for lock, then powers up the post-divider.
 *
 * @note Park CLK_SYS on CLK_REF first, so the CPU runs from XOSC meanwhile.
 * @param fbdiv    PLL feedback divider (new target; REFDIV = 1)
 * @param postdiv1 PLL post-divider 1 (1..7)
 * @param postdiv2 PLL post-divider 2 (1..7)
 * @return 1 when PLL locks, 0 on timeout
 */
static int rp2350_pll_sys_retune(uint16_t fbdiv,
                                 uint8_t postdiv1, uint8_t postdiv2) {
    /* Power down the whole PLL before reprogramming it. */
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
 * @brief Switch CLK_SYS back to PLL_SYS.
 *
 * Selects PLL_SYS on the aux mux while SRC is still REF, then moves the
 * glitchless SRC mux to AUX and polls CLK_SYS_SELECTED bit 1.
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
 * Looks the frequency up in rp2350_freq_table, parks CLK_SYS on XOSC,
 * reprograms PLL_SYS (or bypasses it for 12 MHz), then switches back and
 * updates the cached rates, clearing g_clock_fault.
 *
 * @note A failed retune leaves the system on XOSC at 12 MHz with
 *       g_clock_fault set; an unsupported target sets g_clock_fault and
 *       leaves the clock as it was.  150 MHz is the maximum.
 * @param target_mhz  Desired CLK_SYS frequency in MHz (12, 48, 100, 125,
 *                    133, or 150)
 */
void tiku_cpu_freq_rp2350_init(unsigned int target_mhz) {
    const struct rp2350_pll_params *p = rp2350_lookup_freq(target_mhz);

    if (p == NULL) {
        /* Unsupported target: the clock stays as it is and the fault flag,
         * which /sys/clock reports, is set. */
        g_clock_fault = 1U;
        return;
    }

    /* CLK_SYS already runs at the target, as it does for 150 MHz after
     * boot: nothing to retune. */
    if (g_clk_sys_hz == (unsigned long)target_mhz * 1000000UL) {
        g_clock_fault = 0U;
        return;
    }

    if (p->fbdiv == 0U) {
        /* Bypass PLL: clk_sys = clk_ref = XOSC = 12 MHz. */
        rp2350_park_clk_sys_on_ref();

        /* Power the PLL down: nothing runs from it at 12 MHz.  CLK_PERI
         * moves to XOSC directly. */
        _RP2350_REG(RP2350_PLL_SYS_BASE + RP2350_PLL_PWR) =
            RP2350_PLL_PWR_PD | RP2350_PLL_PWR_VCOPD |
            RP2350_PLL_PWR_POSTDIVPD | RP2350_PLL_PWR_DSMPD;
        _RP2350_REG(RP2350_CLK_PERI_CTRL) =
            RP2350_CLK_PERI_AUXSRC_XOSC | RP2350_CLK_PERI_ENABLE;

        g_clk_sys_hz  = 12000000UL;
        g_clk_peri_hz = 12000000UL;
        return;
    }

    /* Retune: the CPU runs from XOSC while PLL_SYS is reprogrammed. */
    rp2350_park_clk_sys_on_ref();

    if (!rp2350_pll_sys_retune(p->fbdiv, p->postdiv1, p->postdiv2)) {
        /* No lock at the new dividers: CLK_SYS stays on XOSC, CLK_PERI
         * moves to XOSC directly and the fault flag is set. */
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

    /* The caches assume CLK_PERI runs from CLK_SYS, as the boot init sets
     * it, so both take the new rate; the UART and I2C divisors and the
     * clock-rate VFS reads use them. */
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
 * Returns the cached value set by the most recent clock init or retune:
 * 150 000 000 after boot, 12 000 000 after the boot fallback.
 *
 * @return CLK_SYS frequency in Hz
 */
unsigned long tiku_cpu_rp2350_clock_get_hz(void) {
    return g_clk_sys_hz;
}

/**
 * @brief Return the current CLK_PERI (peripheral clock) frequency in Hz.
 *
 * The cache always holds the CLK_SYS rate; both are set together.  The HAL
 * reports it as SMCLK, and the UART and I2C drivers compute their divisors
 * from it.
 *
 * @return CLK_PERI frequency in Hz
 */
unsigned long tiku_cpu_rp2350_smclk_get_hz(void) {
    return g_clk_peri_hz;
}

/**
 * @brief Return the ACLK-equivalent frequency in Hz.
 *
 * This port runs no low-frequency auxiliary clock, so the clock HAL's ACLK
 * query gets 0.
 *
 * @return 0 (no ACLK on RP2350)
 */
unsigned long tiku_cpu_rp2350_aclk_get_hz(void) {
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
