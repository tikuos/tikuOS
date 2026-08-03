/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - STM32N6 clock tree: state, measurement, control.
 *
 * Measures the core rate with the DWT cycle counter against LPTIM1, and moves
 * it between 10 MHz and 800 MHz, raising the core rail when overdrive needs it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_stm32n6_regs.h"

/* Bounded so a dead oscillator surfaces as a fault rather than a hang. */
#define HSI_READY_SPINS     1000000UL

void tiku_cpu_boot_stm32n6_init(void) {
    TIKU_REG32(STM32N6_RCC_CR) |= STM32N6_RCC_CR_HSION;

    unsigned long spins = HSI_READY_SPINS;
    while ((TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_HSIRDY) == 0UL) {
        if (--spins == 0UL) {
            return;
        }
    }
}

/** @brief CPU rate measured against LPTIM1; 0 until measured once. */
static unsigned long stm32n6_measured_hz;

/** @brief Delay-loop iterations per millisecond, measured alongside the rate. */
static unsigned long stm32n6_spin_per_ms;

/**
 * @brief Burn a fixed number of delay-loop iterations.
 *
 * How many cycles the pair costs depends on alignment and whether the fetch
 * hits a cache, which is why the loop rate is measured rather than assumed.
 *
 * @param iters  Iterations to run
 */
static void cpu_cal_spin(unsigned long iters) {
    __asm__ volatile (
        "1: subs %0, %0, #1\n"
        "   bne  1b\n"
        : "+r" (iters)
        :
        : "cc");
}

/** @brief Two agreeing reads of the LPTIM counter, which is asynchronous. */
static uint32_t cpu_lptim_count(void) {
    uint32_t a, b;
    do {
        a = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
        b = TIKU_REG32(STM32N6_LPTIM_CNT(STM32N6_LPTIM1_BASE));
    } while (a != b);
    return a;
}

/**
 * @brief Measure the core clock and the delay-loop rate against LPTIM1.
 *
 * Counts DWT core cycles and loop iterations over the same window, so the
 * clock comes out exact and the loop rate comes out measured, not derived.
 *
 * @return Core rate in Hz, or 0 when LPTIM1 or the cycle counter is unusable
 */
static unsigned long cpu_measure_hz(void) {
    if ((TIKU_REG32(STM32N6_LPTIM_CR(STM32N6_LPTIM1_BASE)) &
         STM32N6_LPTIM_CR_ENABLE) == 0UL) {
        return 0UL;
    }

    TIKU_REG32(STM32N6_DWT_LAR)   = STM32N6_DWT_LAR_KEY;   /* harmless if RAZ/WI */
    TIKU_REG32(STM32N6_SCB_DEMCR) |= STM32N6_SCB_DEMCR_TRCENA;
    TIKU_REG32(STM32N6_DWT_CTRL)  |= STM32N6_DWT_CTRL_CYCCNTENA;

    uint32_t start = cpu_lptim_count();
    cpu_cal_spin(20000UL);              /* confirm the counter is advancing */
    if (cpu_lptim_count() == start) {
        return 0UL;
    }

    /* 2000 counts at 500 kHz is 4 ms, inside the 3906-count reload period so
     * at most one wrap can occur. */
    const uint32_t want  = 2000UL;
    const uint32_t chunk = 20000UL;
    unsigned long  iters = 0UL;
    uint32_t elapsed = 0UL;

    start = cpu_lptim_count();
    uint32_t cyc0 = TIKU_REG32(STM32N6_DWT_CYCCNT);

    while (elapsed < want) {
        cpu_cal_spin(chunk);
        iters += chunk;
        uint32_t now = cpu_lptim_count();
        elapsed = (now >= start) ? (now - start)
                                 : (now + TIKU_CLOCK_ARCH_INTERVAL - start);
    }
    uint32_t cycles = TIKU_REG32(STM32N6_DWT_CYCCNT) - cyc0;

    /* Loop rate first: it holds even when the cycle counter is unavailable. */
    stm32n6_spin_per_ms = (unsigned long)(((unsigned long long)iters *
                                           TIKU_STM32N6_LPTIM_HZ)
                                          / ((unsigned long long)elapsed * 1000ULL));
    if (cycles == 0UL) {
        return 0UL;                     /* DWT not counting: no exact rate */
    }
    return (unsigned long)(((unsigned long long)cycles * TIKU_STM32N6_LPTIM_HZ)
                           / elapsed);
}

unsigned long tiku_cpu_stm32n6_clock_get_hz(void) {
    if (stm32n6_measured_hz == 0UL) {
        stm32n6_measured_hz = cpu_measure_hz();
    }
    /* Before LPTIM1 runs there is nothing to measure against; report the
     * compile-time figure and try again on the next call. */
    return (stm32n6_measured_hz != 0UL) ? stm32n6_measured_hz
                                        : TIKU_STM32N6_CPU_HZ;
}

unsigned long tiku_cpu_stm32n6_spin_per_ms(void) {
    if (stm32n6_spin_per_ms == 0UL) {
        (void)tiku_cpu_stm32n6_clock_get_hz();      /* measures both */
    }
    return (stm32n6_spin_per_ms != 0UL) ? stm32n6_spin_per_ms
                                        : TIKU_STM32N6_SPIN_ITERS_PER_MS;
}

unsigned long tiku_cpu_stm32n6_smclk_get_hz(void) {
    uint32_t div = (TIKU_REG32(STM32N6_RCC_HSICFGR) & STM32N6_RCC_HSICFGR_DIV_MSK)
                   >> STM32N6_RCC_HSICFGR_DIV_POS;
    return STM32N6_HSI_HZ >> div;
}

int tiku_cpu_stm32n6_clock_has_fault(void) {
    return ((TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_HSIRDY) == 0UL) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/
/* Core frequency                                                            */
/*---------------------------------------------------------------------------*/

/* PLL1 runs from HSI with a fixed /4 reference, so the VCO is 16 MHz x N.
 * N=75 gives the 1200 MHz ST ships on this board; N=100 gives 1600 MHz, the
 * only VCO from which 800 MHz falls out on an integer IC1 divider. */
#define PLL_REF_DIV_M       4U
#define PLL_N_NOMINAL       75U      /* 1200 MHz VCO */
#define PLL_N_OVERDRIVE     100U     /* 1600 MHz VCO */
#define PLL_VCO_NOMINAL_MHZ 1200U
#define PLL_OVERDRIVE_MHZ   800U

/* Buses are held at ST's proven rates whatever the core does, so raising the
 * core never over-clocks a peripheral: SYSCLK 400 MHz, AHB half of it. */
#define BUS_IC2_AT_1200     3U       /* 400 MHz */
#define BUS_IC6_AT_1200     4U       /* 300 MHz */
#define BUS_IC11_AT_1200    3U       /* 400 MHz */
#define BUS_IC2_AT_1600     4U       /* 400 MHz */
#define BUS_IC6_AT_1600     6U       /* 267 MHz, under ST's 300 */
#define BUS_IC11_AT_1600    4U       /* 400 MHz */

/* Bounded so a PLL that never locks costs a boot message, not the system. */
#define CLK_SWITCH_SPINS    1000000UL

/** @brief Wait for a register bit, returning 0 if it never appears. */
static int clk_wait_set(uint32_t reg, uint32_t mask) {
    for (unsigned long spins = CLK_SWITCH_SPINS; spins > 0UL; spins--) {
        if (TIKU_REG32(reg) & mask) {
            return 1;
        }
    }
    return 0;
}

/** @brief Wait for a register bit to clear, returning 0 if it never does. */
static int clk_wait_clear(uint32_t reg, uint32_t mask) {
    for (unsigned long spins = CLK_SWITCH_SPINS; spins > 0UL; spins--) {
        if ((TIKU_REG32(reg) & mask) == 0UL) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Point the CPU, and optionally the buses, at a clock source.
 *
 * @param src      One of the STM32N6_CLKSRC_* encodings
 * @param with_sys Non-zero to move the system buses to the same source
 */
static void clk_select_source(uint32_t src, int with_sys) {
    uint32_t cfgr = TIKU_REG32(STM32N6_RCC_CFGR1);
    cfgr &= ~STM32N6_CFGR1_CPUSW_MSK;
    cfgr |= (src << STM32N6_CFGR1_CPUSW_POS);
    if (with_sys) {
        cfgr &= ~STM32N6_CFGR1_SYSSW_MSK;
        cfgr |= (src << STM32N6_CFGR1_SYSSW_POS);
    }
    TIKU_REG32(STM32N6_RCC_CFGR1) = cfgr;

    /* Wait for the switch to be acknowledged in the status field. */
    for (unsigned long spins = CLK_SWITCH_SPINS; spins > 0UL; spins--) {
        uint32_t now = TIKU_REG32(STM32N6_RCC_CFGR1);
        uint32_t cpu_ok = ((now >> STM32N6_CFGR1_CPUSWS_POS) & 3UL) == src;
        uint32_t sys_ok = !with_sys ||
                          (((now >> STM32N6_CFGR1_SYSSWS_POS) & 3UL) == src);
        if (cpu_ok && sys_ok) {
            break;
        }
    }
}

/**
 * @brief Program one IC divider from PLL1 and enable it.
 *
 * @param ic   Divider index, 1-based
 * @param div  Division factor, 1 to 256
 */
static void clk_set_ic(unsigned ic, unsigned div) {
    uint32_t cfg = TIKU_REG32(STM32N6_RCC_ICCFGR(ic));
    cfg &= ~(STM32N6_IC_INT_MSK | STM32N6_IC_SEL_MSK);
    cfg |= (STM32N6_IC_SEL_PLL1 << STM32N6_IC_SEL_POS);
    cfg |= (((uint32_t)div - 1UL) << STM32N6_IC_INT_POS);
    TIKU_REG32(STM32N6_RCC_ICCFGR(ic)) = cfg;
    TIKU_REG32(STM32N6_RCC_DIVENR) |= (1UL << (ic - 1U));
}

/**
 * @brief Move the core supply between the nominal and overdrive rails.
 *
 * The board's SMPS is driven from PB12 and the regulator range from PWR, and
 * both must settle before the core may run above the nominal range.
 *
 * @param high  Non-zero to select overdrive
 */
static void clk_set_voltage(int high) {
    tiku_stm32n6_gpio_init_output(STM32N6_GPIO_PORT_B, STM32N6_SMPS_OVD_PIN);
    tiku_stm32n6_gpio_set(STM32N6_GPIO_PORT_B, STM32N6_SMPS_OVD_PIN,
                          high ? 1U : 0U);

    if (high) {
        TIKU_REG32(STM32N6_PWR_VOSCR) |= STM32N6_PWR_VOSCR_VOS;
    } else {
        TIKU_REG32(STM32N6_PWR_VOSCR) &= ~STM32N6_PWR_VOSCR_VOS;
    }
    (void)clk_wait_set(STM32N6_PWR_VOSCR, STM32N6_PWR_VOSCR_VOSRDY);
}

int tiku_cpu_freq_stm32n6_supported(unsigned int mhz) {
    if (mhz == (STM32N6_HSI_HZ / 1000000UL)) {
        return 1;                                   /* HSI direct */
    }
    if (mhz == PLL_OVERDRIVE_MHZ) {
        return 1;
    }
    return (mhz != 0U && mhz <= 600U &&
            (PLL_VCO_NOMINAL_MHZ % mhz) == 0U) ? 1 : 0;
}

void tiku_cpu_freq_stm32n6_init(unsigned int mhz) {
    if (!tiku_cpu_freq_stm32n6_supported(mhz)) {
        return;
    }

    /* HSI backs every transition, so it must be up before anything moves. */
    TIKU_REG32(STM32N6_RCC_CR) |= STM32N6_RCC_CR_HSION;
    if (!clk_wait_set(STM32N6_RCC_SR, STM32N6_RCC_SR_HSIRDY)) {
        return;
    }

    if (mhz == (STM32N6_HSI_HZ / 1000000UL)) {
        /* Straight to HSI. PLL1 is left running because the buses may still
         * be drawing from it; only the core moves. */
        clk_select_source(STM32N6_CLKSRC_HSI, 0);
        clk_set_voltage(0);
        stm32n6_measured_hz = 0UL;
    stm32n6_spin_per_ms = 0UL;
        return;
    }

    unsigned pll_n   = (mhz == PLL_OVERDRIVE_MHZ) ? PLL_N_OVERDRIVE : PLL_N_NOMINAL;
    unsigned ic1_div = (mhz == PLL_OVERDRIVE_MHZ) ? 2U
                                                  : (PLL_VCO_NOMINAL_MHZ / mhz);
    int overdrive    = (mhz == PLL_OVERDRIVE_MHZ);

    /* Raise the rail before the frequency, and lower it only afterwards. */
    if (overdrive) {
        clk_set_voltage(1);
    }

    /* Park core and buses on HSI: PLL1 cannot be reconfigured while anything
     * is drawing from it. */
    clk_select_source(STM32N6_CLKSRC_HSI, 1);

    TIKU_REG32(STM32N6_RCC_CR) &= ~STM32N6_RCC_CR_PLL1ON;
    (void)clk_wait_clear(STM32N6_RCC_SR, STM32N6_RCC_SR_PLL1RDY);

    uint32_t cfg1 = TIKU_REG32(STM32N6_RCC_PLL1CFGR1);
    cfg1 &= ~(STM32N6_PLL1_DIVN_MSK | STM32N6_PLL1_DIVM_MSK |
              STM32N6_PLL1_SEL_MSK | STM32N6_PLL1_BYP);
    cfg1 |= ((uint32_t)pll_n << STM32N6_PLL1_DIVN_POS);
    cfg1 |= ((uint32_t)PLL_REF_DIV_M << STM32N6_PLL1_DIVM_POS);
    /* PLL1SEL 0 selects HSI, matching the reference divider above. */
    TIKU_REG32(STM32N6_RCC_PLL1CFGR1) = cfg1;
    TIKU_REG32(STM32N6_RCC_PLL1CFGR2) = 0UL;        /* integer mode */

    uint32_t cfg3 = TIKU_REG32(STM32N6_RCC_PLL1CFGR3);
    cfg3 &= ~(STM32N6_PLL1_PDIV1_MSK | STM32N6_PLL1_PDIV2_MSK);
    cfg3 |= (1UL << STM32N6_PLL1_PDIV1_POS) | (1UL << STM32N6_PLL1_PDIV2_POS);
    cfg3 |= STM32N6_PLL1_PDIVEN;
    TIKU_REG32(STM32N6_RCC_PLL1CFGR3) = cfg3;

    TIKU_REG32(STM32N6_RCC_CR) |= STM32N6_RCC_CR_PLL1ON;
    if (!clk_wait_set(STM32N6_RCC_SR, STM32N6_RCC_SR_PLL1RDY)) {
        /* No lock: stay on HSI rather than switch to a dead clock. */
        clk_set_voltage(0);
        stm32n6_measured_hz = 0UL;
    stm32n6_spin_per_ms = 0UL;
        return;
    }

    clk_set_ic(1U, ic1_div);
    clk_set_ic(2U,  overdrive ? BUS_IC2_AT_1600  : BUS_IC2_AT_1200);
    clk_set_ic(6U,  overdrive ? BUS_IC6_AT_1600  : BUS_IC6_AT_1200);
    clk_set_ic(11U, overdrive ? BUS_IC11_AT_1600 : BUS_IC11_AT_1200);

    /* AHB at half of SYSCLK, APB buses undivided -- ST's ratios. */
    uint32_t cfgr2 = TIKU_REG32(STM32N6_RCC_CFGR2);
    cfgr2 &= ~(STM32N6_CFGR2_HPRE_MSK | STM32N6_CFGR2_PPRE1_MSK |
               STM32N6_CFGR2_PPRE2_MSK | STM32N6_CFGR2_PPRE4_MSK |
               STM32N6_CFGR2_PPRE5_MSK);
    cfgr2 |= (1UL << STM32N6_CFGR2_HPRE_POS);
    TIKU_REG32(STM32N6_RCC_CFGR2) = cfgr2;

    clk_select_source(STM32N6_CLKSRC_IC, 1);

    if (!overdrive) {
        clk_set_voltage(0);
    }
    stm32n6_measured_hz = 0UL;
    stm32n6_spin_per_ms = 0UL;      /* re-measure against the new rate */
}

void tiku_cpu_stm32n6_clock_probe(tiku_stm32n6_clock_t *out) {
    if (out == NULL) {
        return;
    }
    uint32_t cfgr1 = TIKU_REG32(STM32N6_RCC_CFGR1);
    uint32_t cfgr2 = TIKU_REG32(STM32N6_RCC_CFGR2);
    uint32_t p1    = TIKU_REG32(STM32N6_RCC_PLL1CFGR1);
    uint32_t p3    = TIKU_REG32(STM32N6_RCC_PLL1CFGR3);

    out->cpu_src    = (uint8_t)((cfgr1 >> STM32N6_CFGR1_CPUSWS_POS) & 3UL);
    out->sys_src    = (uint8_t)((cfgr1 >> STM32N6_CFGR1_SYSSWS_POS) & 3UL);
    out->pll1_on    = (TIKU_REG32(STM32N6_RCC_CR) & STM32N6_RCC_CR_PLL1ON) ? 1U : 0U;
    out->pll1_ready = (TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_PLL1RDY) ? 1U : 0U;
    out->pll1_src   = (uint8_t)((p1 >> STM32N6_PLL1_SEL_POS) & 7UL);
    out->pll1_m     = (uint16_t)((p1 >> STM32N6_PLL1_DIVM_POS) & 0x3FUL);
    out->pll1_n     = (uint16_t)((p1 >> STM32N6_PLL1_DIVN_POS) & 0xFFFUL);
    out->pll1_frac  = TIKU_REG32(STM32N6_RCC_PLL1CFGR2) & 0xFFFFFFUL;
    out->pll1_p1    = (uint8_t)((p3 >> STM32N6_PLL1_PDIV1_POS) & 7UL);
    out->pll1_p2    = (uint8_t)((p3 >> STM32N6_PLL1_PDIV2_POS) & 7UL);
    out->vos_high   = (TIKU_REG32(STM32N6_PWR_VOSCR) & STM32N6_PWR_VOSCR_VOS) ? 1U : 0U;
    out->ic1_sel    = (uint8_t)((TIKU_REG32(STM32N6_RCC_ICCFGR(1)) >> STM32N6_IC_SEL_POS) & 3UL);
    out->ic1_div    = (uint16_t)(((TIKU_REG32(STM32N6_RCC_ICCFGR(1)) >> STM32N6_IC_INT_POS)
                                  & 0xFFUL) + 1UL);
    out->ic2_div    = (uint16_t)(((TIKU_REG32(STM32N6_RCC_ICCFGR(2)) >> STM32N6_IC_INT_POS)
                                  & 0xFFUL) + 1UL);
    out->ahb_div    = 1UL << ((cfgr2 >> STM32N6_CFGR2_HPRE_POS) & 7UL);

    /* Reference is the selected source divided by M; only HSI is used here,
     * so anything else reports a zero VCO rather than a guess. */
    unsigned long ref = (out->pll1_src == 0U && out->pll1_m != 0U)
                        ? (tiku_cpu_stm32n6_smclk_get_hz() / out->pll1_m) : 0UL;
    unsigned long post = (unsigned long)(out->pll1_p1 ? out->pll1_p1 : 1U) *
                         (unsigned long)(out->pll1_p2 ? out->pll1_p2 : 1U);
    out->pll1_hz = (ref * out->pll1_n) / post;

    switch (out->cpu_src) {
    case 0:  out->cpu_hz = tiku_cpu_stm32n6_smclk_get_hz(); break;
    case 3:  out->cpu_hz = (out->ic1_div != 0U) ? (out->pll1_hz / out->ic1_div) : 0UL; break;
    default: out->cpu_hz = 0UL; break;   /* MSI/HSE rates are not tracked */
    }
}
