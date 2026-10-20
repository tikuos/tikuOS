/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sleep_arch.c - ESP32-P4 deep and light sleep through the PMU.
 *
 * Settings and order from ESP-IDF 4d59230 pmu_param/pmu_init/pmu_sleep and
 * sleep_modes for v1.x silicon; the LP domain stays powered.  Light sleep
 * keeps every PLL, clock gate and active voltage, where ESP-IDF lowers them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <hal/tiku_cpu.h>
#include <hal/tiku_mem_hal.h>

#include "tiku_sleep_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_clock_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_systimer_arch.h"
#include "tiku_debug_arch.h"
#include "tiku_watchdog_arch.h"
#include <services/console/tiku_usb_serial_jtag.h>
#if TIKU_LPCORE_ENABLE
#include "tiku_lpagent_arch.h"
#endif

/* Blocks (reg_base.h). */
#define LP_SYS      0x50110000UL
#define LP_CLKRST   0x50111000UL
#define LP_TIMER    0x50112000UL
#define PMU         0x50115000UL
#define LP_WDT      0x50116000UL

/* PMU registers (pmu_reg.h, hw_ver1). */
#define HP_ACT_DIG_POWER   (PMU + 0x000UL)
#define HP_ACT_ICG_FUNC    (PMU + 0x004UL)
#define HP_ACT_ICG_APB     (PMU + 0x008UL)
#define HP_ACT_ICG_MODEM   (PMU + 0x00CUL)
#define HP_ACT_SYSCNTL     (PMU + 0x010UL)
#define HP_ACT_CLK_POWER   (PMU + 0x014UL)
#define HP_ACT_BIAS        (PMU + 0x018UL)
#define HP_ACT_SYSCLK      (PMU + 0x024UL)
#define HP_ACT_XTAL        (PMU + 0x030UL)
#define HP_ACT_REGULATOR0  (PMU + 0x028UL)
#define HP_SLP_DIG_POWER   (PMU + 0x068UL)
#define HP_SLP_ICG_FUNC    (PMU + 0x06CUL)
#define HP_SLP_ICG_APB     (PMU + 0x070UL)
#define HP_SLP_ICG_MODEM   (PMU + 0x074UL)
#define HP_SLP_SYSCNTL     (PMU + 0x078UL)
#define HP_SLP_CLK_POWER   (PMU + 0x07CUL)
#define HP_SLP_BIAS        (PMU + 0x080UL)
#define HP_SLP_SYSCLK      (PMU + 0x08CUL)
#define HP_SLP_REGULATOR0  (PMU + 0x090UL)
#define HP_SLP_REGULATOR1  (PMU + 0x094UL)
#define HP_SLP_XTAL        (PMU + 0x098UL)
#define LP_ACT_REGULATOR0  (PMU + 0x09CUL)
#define LP_ACT_REGULATOR1  (PMU + 0x0A0UL)
#define LP_ACT_DIG_POWER   (PMU + 0x0A8UL)
#define LP_ACT_CLK_POWER   (PMU + 0x0ACUL)
#define LP_SLP_REGULATOR0  (PMU + 0x0B4UL)
#define LP_SLP_REGULATOR1  (PMU + 0x0B8UL)
#define LP_SLP_XTAL        (PMU + 0x0BCUL)
#define LP_SLP_DIG_POWER   (PMU + 0x0C0UL)
#define LP_SLP_CLK_POWER   (PMU + 0x0C4UL)
#define LP_SLP_BIAS        (PMU + 0x0C8UL)
#define IMM_PAD_HOLD_ALL   (PMU + 0x0E4UL)
#define WAIT_TIMER0        (PMU + 0x0ECUL)
#define WAIT_TIMER1        (PMU + 0x0F0UL)
#define PD_TOP_CNTL        (PMU + 0x0F4UL)
#define PD_CNNT_CNTL       (PMU + 0x0F8UL)
#define PD_HPMEM_CNTL      (PMU + 0x0FCUL)
#define PD_LPPERI_CNTL     (PMU + 0x110UL)
#define CK_WAIT            (PMU + 0x11CUL)
#define SLP_CNTL0          (PMU + 0x120UL)
#define SLP_CNTL1          (PMU + 0x124UL)     /* rejects */
#define SLP_CNTL2          (PMU + 0x128UL)     /* wakes */
#define SLP_CNTL3          (PMU + 0x12CUL)
#define SLP_CNTL4          (PMU + 0x130UL)
#define SLP_CNTL5          (PMU + 0x134UL)
#define SLP_CNTL7          (PMU + 0x13CUL)
#define WAKE_STATUS0       (PMU + 0x144UL)
#define HP_INT_RAW         (PMU + 0x164UL)
#define HP_INT_CLR         (PMU + 0x170UL)
#define DCM_CTRL           (PMU + 0x204UL)

/* PMU fields (pmu_struct.h, hw_ver1). */
#define PD_FORCE_MSK       0x3FUL
#define DCDC_SWITCH_PD     (1UL << 21)         /* HP dig power */
#define MEM_PD             (1UL << 23)
#define CNNT_PD            (1UL << 30)
#define TOP_PD             (1UL << 31)
#define UART_WAKE_EN       (1UL << 24)         /* HP syscntl */
#define PAUSE_WDT          (1UL << 28)
#define CPU_STALL          (1UL << 29)
#define I2C_ISO_EN         (1UL << 21)         /* HP clk power */
#define I2C_RETENTION      (1UL << 22)
#define SYSCLK_SLP_SEL     (1UL << 28)         /* HP sysclk, XTAL in [31:30] */
#define ICG_SLP_SEL        (1UL << 29)
#define DCM_VSET_POS       18U                 /* HP bias */
#define DCM_VSET_MSK       (0x1FUL << DCM_VSET_POS)
#define DCM_MODE_POS       23U
#define DCM_MODE_MSK       (3UL << DCM_MODE_POS)
#define XPD_BIAS           (1UL << 25)         /* HP and LP bias */
#define DBG_ATTEN_POS      26U
#define PD_CUR             (1UL << 30)
#define BIAS_SLEEP         (1UL << 31)
#define HP_SLP_VO1_ON      (8UL << 19)         /* HP regulator0: VO1 on */
#define HP_REG_XPD         (1UL << 18)
#define REG_DBIAS_POS      27U                 /* [31:27], HP and LP */
#define REG_DBIAS_MSK      (0x1FUL << REG_DBIAS_POS)
#define LP_REG_XPD         (1UL << 22)         /* LP regulator0 */
#define DRV_B_POS          26U                 /* regulator1, HP and LP */
#define XPD_LPPLL          (1UL << 27)         /* LP clk power */
#define XPD_FOSC           (1UL << 30)
#define XPD_PLL_ALL        (0xFFUL << 23)      /* HP clk power: PLLs, I2C */
#define XPD_XTAL           (1UL << 31)         /* HP and LP xtal */
#define ICG_SYSCLK_EN      (1UL << 27)         /* HP sysclk */
#define TIE_HIGH_PAD_SLP   (1UL << 26)         /* IMM pad hold */
#define TIE_LOW_PAD_SLP    (1UL << 27)
#define SLEEP_REQ          (1UL << 31)
#define REJECT_EN          (1UL << 31)
#define REJECT_CLR         (1UL << 31)
#define INT_LP_EXCEPTION   (1UL << 27)         /* HP int: LP core trap */
#define INT_LP_TRIGGER     (1UL << 29)         /* HP int: LP_TRIGGER_HP */
#define INT_REJECT         (1UL << 30)
#define INT_WAKE           (1UL << 31)
#define DCM_OFF_REQ        (1UL << 1)
#define DCM_DONE_FORCE     (1UL << 7)
#define PROTECT_HP_LP      (2UL << 16)         /* sleep_prt_sel */

/* Regulator and DCDC values (pmu_param.h, pmu_sleep.c). */
#define HP_DBIAS_LDO_TAKEOVER  30UL
#define HP_DCM_VSET_TAKEOVER   24UL
#define HP_DBIAS_DEFAULT       24UL
#define LP_ACT_DBIAS_DSLP      0x1AUL
#define LP_ACT_DRVB_DSLP       0x7UL
#define LP_SLP_DBIAS_DSLP      23UL
#define LP_SLP_DBG_ATTEN_DSLP  12UL
#define LDO_TAKEOVER_US        185U

/* LP timer (lp_timer_reg.h). */
#define LPT_TAR0_LO        (LP_TIMER + 0x00UL)
#define LPT_TAR0_HI        (LP_TIMER + 0x04UL)
#define LPT_UPDATE         (LP_TIMER + 0x10UL)
#define LPT_BUF0_LO        (LP_TIMER + 0x14UL)
#define LPT_BUF0_HI        (LP_TIMER + 0x18UL)
#define LPT_INT_CLR        (LP_TIMER + 0x34UL)
#define LPT_SNAPSHOT       (1UL << 28)
#define LPT_TAR_EN         (1UL << 31)
#define LPT_WAKE_CLR       (1UL << 31)

/* LP watchdog (lp_wdt_reg.h). */
#define RWDT_CONFIG0       (LP_WDT + 0x00UL)
#define RWDT_CONFIG1       (LP_WDT + 0x04UL)
#define RWDT_FEED          (LP_WDT + 0x14UL)
#define RWDT_WPROTECT      (LP_WDT + 0x18UL)
#define RWDT_EN            (1UL << 31)
#define RWDT_STG0_CHIP     (4UL << 28)         /* the whole chip, LP too */
#define RWDT_RESET_LENGTHS ((1UL << 13) | (1UL << 16))

/* LP system stores (rom/rtc.h): 6 the wake stub's entry, 8 bit 0 the sleep
 * mode the ROM reads on the wake.  Store 12 is not used by the ROM or
 * ESP-IDF. */
#define LP_STORE(n)        (LP_SYS + 0x2CUL + 4UL * (n))
#define SLEEP_MISSED_STORE LP_STORE(12)
#define SLEEP_MISSED       0x534C5021UL        /* "SLP!" */

/* LP_CLKRST_LP_CLK_CONF: the LP fast clock, which clocks the PMU and the LP
 * core. */
#define LP_CLK_CONF        (LP_CLKRST + 0x000UL)
#define LP_FAST_SEL_MSK    (3UL << 2)          /* 0 RC_FAST, 1 XTAL */

#define RC_FAST_HZ         20000000UL  /* the PMU's work clock, at most */
#define RESET_DEEPSLEEP    5U          /* ROM reset code of a deep wake */
#define ROM_RESET_REASON   0x4fc00018UL

static uint32_t lp_hz;

/* Light sleeps that ended on a wake, and their SYSTIMER time. */
static uint32_t light_count;
static uint64_t light_counts;

/* Latched at boot by tiku_p4_sleep_boot(). */
static uint32_t rom_code;
static uint32_t wake_cause;
static uint8_t  sleep_missed;
static uint64_t boot_lp;

static uint32_t rd(uint32_t address)
{
    return TIKU_P4_REG_READ(address);
}

static void wr(uint32_t address, uint32_t value)
{
    TIKU_P4_REG_WRITE(address, value);
}

/** @brief Set @p msk in @p reg to @p val, already shifted. */
static void field(uint32_t reg, uint32_t msk, uint32_t val)
{
    wr(reg, (rd(reg) & ~msk) | (val & msk));
}

/** @brief SYSTIMER's count, 16 MHz; 0 if the read failed. */
static uint64_t systimer(void)
{
    uint64_t v = 0;

    (void)tiku_p4_systimer_read(&v);
    return v;
}

uint64_t tiku_p4_lp_ticks(void)
{
    uint32_t lo, hi;

    wr(LPT_UPDATE, LPT_SNAPSHOT);
    lo = rd(LPT_BUF0_LO);
    hi = rd(LPT_BUF0_HI) & 0xFFFFUL;
    return ((uint64_t)hi << 32) | lo;
}

uint32_t tiku_p4_lp_hz(void)
{
    uint64_t t0, t1, l0;

    /* The slow RC drifts from its nominal rate by several percent: 20 ms
     * against the 16 MHz SYSTIMER pins it to about 0.05 %. */
    if (lp_hz == 0UL) {
        l0 = tiku_p4_lp_ticks();
        t0 = systimer();
        do {
            t1 = systimer();
        } while (t1 - t0 < 16000000ULL / 50ULL);
        lp_hz = (uint32_t)(((tiku_p4_lp_ticks() - l0) * 16000000ULL) /
                           (t1 - t0));
    }
    return lp_hz;
}

/** @brief Whole cycles of a @p hz clock in @p us, rounded up. */
static uint32_t cycles(uint32_t us, uint32_t hz)
{
    return (uint32_t)(((uint64_t)us * hz + 999999ULL) / 1000000ULL);
}

/** @brief @p v, limited to @p max. */
static uint32_t cap(uint32_t v, uint32_t max)
{
    return (v > max) ? max : v;
}

/**
 * @brief Program what every sleep shares (pmu_init, and the waits of
 *        pmu_sleep_param_config_default).
 *
 * The power domains go to the PMU's state machine, and the wake path's
 * waits are set: minimum sleeps and the protection mode, analog settling
 * (HP, @p hp_analog_us; LP), supply and power-up, the PLL.
 *
 * @param slow_hz       LP slow clock rate
 * @param hp_analog_us  HP regulator settling, longer when TOP stays up
 */
static void pmu_wake_config(uint32_t slow_hz, uint32_t hp_analog_us)
{
    static const uint32_t forced[] = {
        PD_TOP_CNTL, PD_CNNT_CNTL, PD_HPMEM_CNTL, PD_LPPERI_CNTL
    };
    uint32_t min_slp = cap(cycles(450U, slow_hz), 0xFFUL);
    unsigned i;

    /* The state machine owns the domains, and memory is isolated asleep. */
    for (i = 0U; i < sizeof forced / sizeof forced[0]; i++) {
        wr(forced[i], rd(forced[i]) & ~PD_FORCE_MSK);
    }

    /* HP active state, which the PMU applies on each wake (pmu_init's
     * defaults; reset leaves the system clock gated, the PLLs and the bias
     * off): every domain and clock gate on, the PLLs and the crystal
     * powered, the system clock on the crystal, the DCDC's bias on. */
    wr(HP_ACT_DIG_POWER, 0UL);
    wr(HP_ACT_ICG_FUNC, 0xFFFFFFFFUL);
    wr(HP_ACT_ICG_APB, 0xFFFFFFFFUL);
    wr(HP_ACT_ICG_MODEM, 0UL);
    wr(HP_ACT_SYSCNTL, 0UL);
    wr(HP_ACT_CLK_POWER, XPD_PLL_ALL);
    wr(HP_ACT_XTAL, XPD_XTAL);
    wr(HP_ACT_SYSCLK, ICG_SYSCLK_EN);
    field(HP_ACT_BIAS, XPD_BIAS | DCM_MODE_MSK | (0xFUL << DBG_ATTEN_POS) |
          PD_CUR | BIAS_SLEEP, XPD_BIAS | (1UL << DCM_MODE_POS));

    field(SLP_CNTL3, 0x3FFFFUL, min_slp | (min_slp << 8) | PROTECT_HP_LP);
    field(SLP_CNTL7, 0xFFFF0000UL,
          cap(cycles(hp_analog_us, slow_hz), 0xFFFFUL) << 16);
    field(SLP_CNTL5, 0xFF000000UL,
          cap(cycles(154U, slow_hz), 0xFFUL) << 24);
    field(WAIT_TIMER0, 0xFFFFC000UL,
          (cap(cycles(26U, RC_FAST_HZ), 0x1FFUL) << 14) |
          (cap(cycles(2U, RC_FAST_HZ), 0x1FFUL) << 23));
    field(WAIT_TIMER1, 0xFFFFC000UL,
          (cap(cycles(2U, RC_FAST_HZ), 0x1FFUL) << 14) |
          (cap(cycles(2U, RC_FAST_HZ), 0x1FFUL) << 23));
    field(CK_WAIT, 0xFFFF0000UL, cap(cycles(50U, RC_FAST_HZ), 0xFFFFUL) << 16);
}

/**
 * @brief The HP sleep state's system control: UART wake, the core stalled,
 *        and the watchdogs paused unless the LP watchdog guards the sleep
 *        (@p guarded), as with ESP-IDF's RTC_SLEEP_USE_RTC_WDT.
 */
static uint32_t sleep_syscntl(int guarded)
{
    return UART_WAKE_EN | CPU_STALL | (guarded ? 0UL : PAUSE_WDT);
}

/**
 * @brief Program the PMU's deep-sleep state (pmu_init, then
 *        pmu_sleep_init for a deep sleep).
 *
 * @param slow_hz  LP slow clock rate
 * @param guarded  Nonzero when the LP watchdog guards the sleep
 * @param sar      Nonzero when LP jobs convert on the SAR asleep: the HP
 *                 bias stays on, as with ESP-IDF's ADC monitor mode
 */
static void pmu_deep_config(uint32_t slow_hz, int guarded, int sar)
{
    pmu_wake_config(slow_hz, 260U);

    /* HP asleep: TOP, CNNT and HP memory down with the DCDC switch, the PLLs
     * and the crystal off, no clocks, the core stalled, the system clock
     * moved to the crystal; the HP regulator off, its bias asleep unless
     * the SAR converts (with it asleep every pad reads full scale).  The
     * flash keeps LDO VO1: one powered down on v1.x silicon is found busy
     * by the ROM after the wake. */
    wr(HP_SLP_DIG_POWER, DCDC_SWITCH_PD | MEM_PD | CNNT_PD | TOP_PD);
    wr(HP_SLP_CLK_POWER, I2C_ISO_EN | I2C_RETENTION);
    wr(HP_SLP_XTAL, 0UL);
    wr(HP_SLP_ICG_FUNC, 0UL);
    wr(HP_SLP_ICG_APB, 0UL);
    wr(HP_SLP_ICG_MODEM, 0UL);
    wr(HP_SLP_SYSCNTL, sleep_syscntl(guarded));
    wr(HP_SLP_SYSCLK, SYSCLK_SLP_SEL | ICG_SLP_SEL);
    wr(HP_SLP_BIAS, sar ? 0UL : PD_CUR | BIAS_SLEEP);
    wr(HP_SLP_REGULATOR0, HP_SLP_VO1_ON);
    wr(HP_SLP_REGULATOR1, 0UL);
    field(HP_ACT_BIAS, DCM_MODE_MSK, 0UL);

    /* LP awake while the HP sleeps: the regulator at the deep-sleep trim,
     * the fast RC and the LP PLL on.  LP asleep: its peripherals kept, so a
     * payload's LP IO and timers hold, the oscillators off. */
    wr(LP_ACT_REGULATOR0, LP_REG_XPD | (LP_ACT_DBIAS_DSLP << REG_DBIAS_POS));
    wr(LP_ACT_REGULATOR1, LP_ACT_DRVB_DSLP << DRV_B_POS);
    wr(LP_ACT_DIG_POWER, 0UL);
    wr(LP_ACT_CLK_POWER, XPD_LPPLL | XPD_FOSC);
    wr(LP_SLP_REGULATOR0, LP_REG_XPD | (LP_SLP_DBIAS_DSLP << REG_DBIAS_POS));
    wr(LP_SLP_REGULATOR1, 0UL);
    wr(LP_SLP_XTAL, 0UL);
    wr(LP_SLP_DIG_POWER, 0UL);
    wr(LP_SLP_CLK_POWER, 0UL);
    wr(LP_SLP_BIAS, PD_CUR | BIAS_SLEEP |
                    (LP_SLP_DBG_ATTEN_DSLP << DBG_ATTEN_POS));

    /* The crystal is off asleep: its restart is counted in fast cycles. */
    field(CK_WAIT, 0xFFFFUL, cap(cycles(250U, RC_FAST_HZ), 0xFFFFUL));
}

/**
 * @brief Program the PMU's light-sleep state.
 *
 * Every HP domain, PLL, clock gate and the crystal stay on, the core stalls
 * on the crystal, both regulators hold their active voltages, and the LP
 * domain sleeps as it runs.  @p guarded as for pmu_deep_config().
 */
static void pmu_light_config(uint32_t slow_hz, int guarded)
{
    uint32_t hp_bias = rd(HP_ACT_BIAS) & DCM_VSET_MSK;
    uint32_t hp = rd(HP_ACT_REGULATOR0) & REG_DBIAS_MSK;
    uint32_t lp = rd(LP_ACT_REGULATOR0) & REG_DBIAS_MSK;

    pmu_wake_config(slow_hz, 945U);

    wr(HP_SLP_DIG_POWER, 0UL);
    wr(HP_SLP_CLK_POWER, I2C_ISO_EN | I2C_RETENTION | XPD_PLL_ALL);
    wr(HP_SLP_XTAL, XPD_XTAL);
    wr(HP_SLP_ICG_FUNC, 0xFFFFFFFFUL);
    wr(HP_SLP_ICG_APB, 0xFFFFFFFFUL);
    wr(HP_SLP_ICG_MODEM, 0UL);
    wr(HP_SLP_SYSCNTL, sleep_syscntl(guarded));
    wr(HP_SLP_SYSCLK, ICG_SYSCLK_EN | SYSCLK_SLP_SEL | ICG_SLP_SEL);
    wr(HP_SLP_BIAS, hp_bias | (1UL << DCM_MODE_POS));
    wr(HP_SLP_REGULATOR0, HP_SLP_VO1_ON | hp);
    wr(HP_SLP_REGULATOR1, 0UL);

    /* The crystal stays on: its wait is counted in slow cycles. */
    field(CK_WAIT, 0xFFFFUL, cap(cycles(250U, slow_hz), 0xFFFFUL));

    wr(LP_SLP_REGULATOR0, LP_REG_XPD | lp);
    wr(LP_SLP_REGULATOR1, 0UL);
    wr(LP_SLP_XTAL, XPD_XTAL);
    wr(LP_SLP_DIG_POWER, 0UL);
    wr(LP_SLP_CLK_POWER, XPD_FOSC);
    wr(LP_SLP_BIAS, 0UL);
}

/**
 * @brief Arm the LP watchdog to reset the whole chip @p hold slow ticks
 *        from now, for a wake that never comes.
 */
static void sleep_safety_net(uint64_t hold)
{
    wr(RWDT_WPROTECT, TIKU_P4_WDT_KEY);
    wr(RWDT_CONFIG1, (hold > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)hold);
    wr(RWDT_CONFIG0, RWDT_EN | RWDT_STG0_CHIP | RWDT_RESET_LENGTHS);
    wr(RWDT_FEED, 1UL << 31);
    wr(RWDT_WPROTECT, 0UL);
}

/** @brief Stand the safety net down once a light sleep has come back. */
static void sleep_safety_off(void)
{
    wr(RWDT_WPROTECT, TIKU_P4_WDT_KEY);
    wr(RWDT_CONFIG0, 0UL);
    wr(RWDT_WPROTECT, 0UL);
}

/** @brief Arm the LP timer's target 0 @p us from now; 0 disarms it. */
static void lp_timer_wake(uint64_t us, uint32_t hz)
{
    uint64_t target;

    wr(LPT_TAR0_HI, rd(LPT_TAR0_HI) & ~LPT_TAR_EN);
    wr(LPT_INT_CLR, LPT_WAKE_CLR);
    if (us != 0ULL) {
        target = tiku_p4_lp_ticks() + (us * hz) / 1000000ULL;
        wr(LPT_TAR0_LO, (uint32_t)target);
        wr(LPT_TAR0_HI, ((uint32_t)(target >> 32) & 0xFFFFUL) | LPT_TAR_EN);
    }
}

/** @brief Give the console up to @p ms to hand its last bytes to the host. */
static void console_drain(unsigned ms)
{
    while (ms-- > 0U) {
        tiku_debug_arch_poll();
        tiku_cpu_p4_delay_us(1000U);
    }
}

void tiku_p4_deep_sleep(uint64_t us)
{
    uint32_t hz = tiku_p4_lp_hz();
    uint32_t wakes = TIKU_P4_WAKE_LP_CORE | TIKU_P4_WAKE_LP_TRAP;
    uint64_t t0;
    int sar = 0;

    /* The checkpoint: HP SRAM is about to lose power, the mirror is not.
     * The LP agent's jobs may wake the part from here on. */
    (void)tiku_mem_arch_nvm_flush_status();
#if TIKU_LPCORE_ENABLE
    tiku_lpagent_arm();
    sar = tiku_lpagent_uses_sar();
#endif
    console_drain(50U);
    (void)TIKU_P4_IRQ_SAVE();

    /* Off the CPLL first: it powers down with the HP domain. */
    (void)tiku_p4_clock_xtal_init();

    /* The LDO takes the HP supply over from the DCDC
     * (pmu_sleep_increase_ldo_volt), and has LDO_TAKEOVER_US to settle. */
    field(HP_ACT_REGULATOR0, REG_DBIAS_MSK | HP_REG_XPD,
          (HP_DBIAS_LDO_TAKEOVER << REG_DBIAS_POS) | HP_REG_XPD);
    field(HP_ACT_BIAS, DCM_VSET_MSK, HP_DCM_VSET_TAKEOVER << DCM_VSET_POS);
    t0 = systimer();

    pmu_deep_config(hz, us != 0ULL, sar);
    if (us != 0ULL) {
        sleep_safety_net((us * hz) / 1000000ULL + hz);
        wakes |= TIKU_P4_WAKE_TIMER;
    }
    lp_timer_wake(us, hz);

    /* The ROM, on the way back: the sleep was deep, and there is no stub. */
    wr(LP_STORE(6), 0UL);
    wr(LP_STORE(8), 1UL);
    wr(SLEEP_MISSED_STORE, SLEEP_MISSED);

    /* The DCDC off with the LDO carrying the load (pmu_sleep_shutdown_dcdc). */
    while (systimer() - t0 < (uint64_t)LDO_TAKEOVER_US * 16ULL) {
    }
    wr(DCM_CTRL, rd(DCM_CTRL) | DCM_OFF_REQ | DCM_DONE_FORCE);
    field(HP_ACT_REGULATOR0, REG_DBIAS_MSK,
          HP_DBIAS_DEFAULT << REG_DBIAS_POS);

    /* pmu_sleep_start: the pads to their sleep settings, then the request.
     * The LP core's trigger and trap stay latched until cleared, and a
     * latched one ends the sleep at once. */
    wr(SLP_CNTL2, wakes);
    wr(SLP_CNTL1, REJECT_EN);
    wr(HP_INT_CLR, INT_WAKE | INT_REJECT | INT_LP_TRIGGER | INT_LP_EXCEPTION);
    wr(SLP_CNTL4, REJECT_CLR);
    wr(IMM_PAD_HOLD_ALL, TIE_HIGH_PAD_SLP);
    wr(SLP_CNTL0, SLEEP_REQ);

    /* Powered down within a millisecond.  A second awake means the sleep
     * never took the core; the store says so to the boot this reset starts. */
    t0 = systimer();
    while (systimer() - t0 < 16000000ULL) {
    }
    tiku_cpu_p4_restart();
}

uint32_t tiku_p4_light_sleep(uint64_t us)
{
    uint32_t hz = tiku_p4_lp_hz();
    uint32_t wakes = TIKU_P4_WAKE_LP_CORE | TIKU_P4_WAKE_LP_TRAP;
    uint32_t s, cause = 0UL;
    unsigned mhz = (unsigned)(tiku_cpu_mclk_hz() / 1000000UL);
    uint64_t t0;

    s = TIKU_P4_IRQ_SAVE();

    /* The PMU moves the system clock to the crystal asleep and leaves it
     * there awake: the core leaves the CPLL first and goes back after.  The
     * kernel's watchdog (MWDT0) holds while the core sleeps; the PMU no
     * longer pauses it in a sleep the LP watchdog guards. */
    (void)tiku_p4_clock_xtal_init();
    tiku_watchdog_arch_pause();
    pmu_light_config(hz, us != 0ULL);
    if (us != 0ULL) {
        /* An HP system reset leaves the PMU asleep with the core stalled
         * (seen on the FireBeetle 2): only the whole chip's reset ends a
         * light sleep whose wake never came. */
        sleep_safety_net((us * hz) / 1000000ULL + hz);
        wakes |= TIKU_P4_WAKE_TIMER;
    }
    lp_timer_wake(us, hz);

    /* A light sleep: the ROM must not take a later reset for a deep wake.
     * The LP core's latched trigger and trap are cleared, and one that
     * comes between here and the request rejects the sleep. */
    wr(LP_STORE(8), rd(LP_STORE(8)) & ~1UL);
    wr(SLP_CNTL2, wakes);
    wr(SLP_CNTL1, REJECT_EN | TIKU_P4_WAKE_LP_CORE | TIKU_P4_WAKE_LP_TRAP);
    wr(HP_INT_CLR, INT_WAKE | INT_REJECT | INT_LP_TRIGGER | INT_LP_EXCEPTION);
    wr(SLP_CNTL4, REJECT_CLR);

    /* The core stalls as the request is written, so the time is read first,
     * and the poll runs once it wakes or the sleep is rejected. */
    t0 = systimer();
    wr(SLP_CNTL0, SLEEP_REQ);
    while ((rd(HP_INT_RAW) & (INT_WAKE | INT_REJECT)) == 0UL &&
           systimer() - t0 < (us + 2000000ULL) * 16ULL) {
    }
    if ((rd(HP_INT_RAW) & INT_WAKE) != 0UL) {
        cause = rd(WAKE_STATUS0);
        light_count++;
        light_counts += systimer() - t0;
    }
    wr(HP_INT_CLR, INT_WAKE | INT_REJECT);

    lp_timer_wake(0ULL, hz);
    if (us != 0ULL) {
        sleep_safety_off();
    }
    tiku_watchdog_arch_resume(1);
    (void)tiku_p4_clock_set_mhz(mhz);
    TIKU_P4_IRQ_RESTORE(s);
    return cause;
}

/* Light idle: a deadline nearer than LIGHT_IDLE_MIN_US idles in wfi; a
 * light sleep is asked to end LIGHT_IDLE_EARLY_US before its deadline, the
 * time the way out takes, and lasts at most LIGHT_IDLE_MAX_US. */
#define LIGHT_IDLE_MIN_US   3000ULL
#define LIGHT_IDLE_EARLY_US 1000ULL
#define LIGHT_IDLE_MAX_US   1000000ULL

void tiku_p4_light_idle(void)
{
    uint64_t now = systimer();
    uint64_t due = tiku_p4_alarm_due(now), us;

    /* A sleep would hold the console's queued bytes and leave a received
     * one waiting for the deadline; these, and a deadline nearer than
     * LIGHT_IDLE_MIN_US, idle in wfi. */
    if (now == 0ULL || tiku_serial_jtag_rx_ready() ||
        tiku_serial_jtag_pending() != 0U ||
        due < now + LIGHT_IDLE_MIN_US * 16ULL) {
        __asm__ volatile("wfi" ::: "memory");
        return;
    }
    us = (due - now) / 16ULL - LIGHT_IDLE_EARLY_US;
    (void)tiku_p4_light_sleep(us < LIGHT_IDLE_MAX_US ? us : LIGHT_IDLE_MAX_US);
}

void tiku_p4_light_stats(uint32_t *count, uint64_t *us)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();

    *count = light_count;
    *us = light_counts / 16ULL;
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_p4_sleep_boot(void)
{
    uint32_t code = ((uint32_t (*)(uint32_t))ROM_RESET_REASON)(0);

    rom_code = code;
    boot_lp = tiku_p4_lp_ticks();

    /* The LP fast clock from RC_FAST, as ESP-IDF sets it at boot: its reset
     * source is the crystal, which deep sleep powers down. */
    wr(LP_CLK_CONF, rd(LP_CLK_CONF) & ~LP_FAST_SEL_MSK);

    /* pmu_init's HP active state, which a deep sleep's entry changed: the
     * DCDC's mode, and the pads back to their active settings. */
    field(HP_ACT_BIAS, DCM_MODE_MSK, 1UL << DCM_MODE_POS);
    wr(IMM_PAD_HOLD_ALL, TIE_LOW_PAD_SLP);
    lp_timer_wake(0ULL, 0UL);

    /* The store outlives a successful sleep too, so it is read and cleared
     * on every boot, or a later reset would be taken for a missed sleep. */
    wake_cause = (code == RESET_DEEPSLEEP) ? rd(WAKE_STATUS0) : 0UL;
    sleep_missed = (uint8_t)(code != RESET_DEEPSLEEP &&
                             rd(SLEEP_MISSED_STORE) == SLEEP_MISSED);
    wr(SLEEP_MISSED_STORE, 0UL);
    wr(LP_STORE(8), rd(LP_STORE(8)) & ~1UL);
}

uint32_t tiku_p4_rom_reset_code(void)
{
    return rom_code;
}

uint32_t tiku_p4_wake_cause(void)
{
    return wake_cause;
}

uint64_t tiku_p4_boot_lp_ticks(void)
{
    return boot_lp;
}

int tiku_p4_sleep_missed(void)
{
    return sleep_missed;
}
