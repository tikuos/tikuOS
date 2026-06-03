/*
 * Tiku Operating System v0.05
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

static volatile unsigned long g_clk_sys_hz  = 0UL;
static volatile unsigned long g_clk_peri_hz = 0UL;
static volatile uint8_t       g_clock_fault = 0U;

/*---------------------------------------------------------------------------*/
/* Internal helpers                                                          */
/*---------------------------------------------------------------------------*/

/* Bounded spin: returns 1 on success, 0 if the loop body never matched
 * within ~1 M iterations (~10 ms at any reasonable boot clock). The
 * caller decides what to do on timeout — typically: silently fall back
 * and keep going so the rest of the system still tries to come up. */
#define RP2350_SPIN_TIMEOUT 1000000U

static int rp2350_spin_until(volatile uint32_t *reg, uint32_t mask) {
    uint32_t i = RP2350_SPIN_TIMEOUT;
    while (i--) {
        if ((*reg) & mask) {
            return 1;
        }
    }
    return 0;
}

static int rp2350_xosc_init(void) {
    /* Set start-up delay (~1 ms at 12 MHz, multiplied by 256 internally). */
    _RP2350_REG(RP2350_XOSC_STARTUP) = 47U;

    /* Enable XOSC in 1-15 MHz range. */
    _RP2350_REG(RP2350_XOSC_CTRL) =
        RP2350_XOSC_CTRL_ENABLE | RP2350_XOSC_CTRL_FREQ_1_15;

    return rp2350_spin_until((volatile uint32_t *)RP2350_XOSC_STATUS,
                             RP2350_XOSC_STATUS_STABLE);
}

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

/* Fallback when the PLL bring-up fails: leave CLK_SYS sourced from
 * CLK_REF (which we already pointed at the 12 MHz XOSC) and reflect
 * the actual rate so baud-divisor calculation in the UART driver
 * matches. */
static void rp2350_clock_fallback_xosc(void) {
    /* CLK_SYS = CLK_REF (i.e. XOSC at 12 MHz). */
    _RP2350_REG(RP2350_CLK_SYS_CTRL) = RP2350_CLK_SYS_SRC_REF;
    _RP2350_REG(RP2350_CLK_SYS_DIV)  = 0x00010000U;

    /* CLK_PERI = XOSC directly (skip the CLK_SYS path so we still
     * have a working clock even if the SYS mux is in an odd state). */
    _RP2350_REG(RP2350_CLK_PERI_CTRL) =
        RP2350_CLK_PERI_AUXSRC_XOSC | RP2350_CLK_PERI_ENABLE;
}

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

struct rp2350_pll_params {
    unsigned int target_mhz;
    uint16_t     fbdiv;     /* 0 = special: bypass PLL, use XOSC directly */
    uint8_t      postdiv1;
    uint8_t      postdiv2;
};

/* Six supported clk_sys frequencies; 150 MHz is the default. The 12 MHz
 * entry marks "bypass PLL" via fbdiv == 0 -- there's no useful PLL setting
 * that produces 12 MHz output and the chip is happiest sourcing clk_sys
 * directly from clk_ref/XOSC at low frequencies. */
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

/* Park clk_sys on clk_ref (XOSC) so PLL_SYS can be safely reconfigured.
 * Sequence per datasheet §5.5.4: clear AUX selection (CLK_SYS_SRC=REF),
 * wait for SELECTED to confirm. */
static void rp2350_park_clk_sys_on_ref(void) {
    _RP2350_REG(RP2350_CLK_SYS_CTRL) = RP2350_CLK_SYS_SRC_REF;
    (void)rp2350_spin_until((volatile uint32_t *)RP2350_CLK_SYS_SELECTED,
                            0x1U);
}

/* Reprogram PLL_SYS for the new VCO + POSTDIV values. Caller must have
 * already parked clk_sys on clk_ref so the in-flight clk_sys consumers
 * (CPU, peripherals) ride on XOSC during the retune window. */
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

/* Switch clk_sys back to PLL_SYS via the glitch-free aux mux sequence. */
static int rp2350_clk_sys_back_on_pll(void) {
    _RP2350_REG(RP2350_CLK_SYS_CTRL) =
        RP2350_CLK_SYS_SRC_REF | RP2350_CLK_SYS_AUXSRC_PLL_SYS;
    _RP2350_REG(RP2350_CLK_SYS_CTRL) =
        RP2350_CLK_SYS_SRC_AUX | RP2350_CLK_SYS_AUXSRC_PLL_SYS;
    return rp2350_spin_until(
        (volatile uint32_t *)RP2350_CLK_SYS_SELECTED, 0x2U);
}

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

void tiku_cpu_boot_rp2350_power_wfi_enter(void) {
    __asm__ volatile ("wfi" ::: "memory");
}

unsigned long tiku_cpu_rp2350_clock_get_hz(void) {
    return g_clk_sys_hz;
}

unsigned long tiku_cpu_rp2350_smclk_get_hz(void) {
    return g_clk_peri_hz;
}

unsigned long tiku_cpu_rp2350_aclk_get_hz(void) {
    /* No always-on low-frequency clock on RP2350. */
    return 0UL;
}

int tiku_cpu_rp2350_clock_has_fault(void) {
    return g_clock_fault ? 1 : 0;
}
