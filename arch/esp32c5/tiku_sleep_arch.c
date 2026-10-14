/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sleep_arch.c - ESP32-C5 deep and light sleep through the PMU.
 *
 * PMU settings from ESP-IDF 4d59230 (pmu_param.h, pmu_sleep.c), with fused
 * regulator trims; the PMU, LP timer, LP AON and LP watchdog blocks are the
 * C61's, register for register.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_sleep_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_clock_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_mem_arch.h"
#include "tiku_rom_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_debug_arch.h"
#include "tiku_analog_arch.h"
#if defined(TIKU_CONSOLE_JTAG)
#include <services/console/tiku_usb_serial_jtag.h>
#endif

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* Blocks (reg_base.h). */
#define PMU         0x600B0000UL
#define LP_TIMER    0x600B0C00UL
#define LP_AON      0x600B1000UL
#define LP_WDT      0x600B1C00UL
#define EFUSE_SYS2  0x600B484CUL
#define EFUSE_SYS3  0x600B4850UL
#define EFUSE_SYS4  0x600B4854UL
#define GPIO_PIN(n) (0x60091000UL + 0xD4UL + 4UL * (n))
#define UART0       0x60000000UL

/* PMU registers (pmu_reg.h). */
#define HP_ACT_ICG_MODEM   (PMU + 0x00CUL)
#define HP_ACT_CLK_POWER   (PMU + 0x014UL)
#define HP_ACT_SYSCLK      (PMU + 0x024UL)
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
#define LP_ACT_DIG_POWER   (PMU + 0x0A8UL)
#define LP_ACT_CLK_POWER   (PMU + 0x0ACUL)
#define LP_SLP_REGULATOR0  (PMU + 0x0B4UL)
#define LP_SLP_REGULATOR1  (PMU + 0x0B8UL)
#define LP_SLP_XTAL        (PMU + 0x0BCUL)
#define LP_SLP_DIG_POWER   (PMU + 0x0C0UL)
#define LP_SLP_CLK_POWER   (PMU + 0x0C4UL)
#define LP_SLP_BIAS        (PMU + 0x0C8UL)
#define IMM_SLEEP_SYSCLK   (PMU + 0x0D0UL)
#define IMM_MODEM_ICG      (PMU + 0x0DCUL)
#define WAIT_TIMER0        (PMU + 0x0ECUL)
#define WAIT_TIMER1        (PMU + 0x0F0UL)
#define WAIT_TIMER2        (PMU + 0x0F4UL)
#define PD_TOP_CNTL        (PMU + 0x0F8UL)
#define PD_HPAON_CNTL      (PMU + 0x0FCUL)
#define PD_HPCPU_CNTL      (PMU + 0x100UL)
#define PD_HPWIFI_CNTL     (PMU + 0x108UL)
#define PD_LPPERI_CNTL     (PMU + 0x10CUL)
#define PD_MEM_CNTL        (PMU + 0x110UL)
#define CK_WAIT            (PMU + 0x120UL)
#define SLP_CNTL0          (PMU + 0x124UL)
#define SLP_CNTL1          (PMU + 0x128UL)     /* rejects */
#define SLP_CNTL2          (PMU + 0x12CUL)     /* wakes */
#define SLP_CNTL3          (PMU + 0x130UL)
#define SLP_CNTL4          (PMU + 0x134UL)
#define SLP_CNTL5          (PMU + 0x138UL)
#define SLP_CNTL7          (PMU + 0x140UL)
#define WAKE_STATUS        (PMU + 0x144UL)
#define HP_INT_RAW         (PMU + 0x160UL)
#define HP_INT_CLR         (PMU + 0x16CUL)

/* PMU fields. */
#define PD_FORCE_MSK       0x3FUL
#define MEM_NO_ISO_MSK     (0xFUL << 24)
#define PD_VDD_SPI         (1UL << 21)         /* HP dig power */
#define PD_WIFI            (1UL << 27)
#define PD_CPU             (1UL << 29)
#define PD_HP_AON          (1UL << 30)
#define PD_TOP             (1UL << 31)
#define LP_PERI_PD         (1UL << 31)         /* LP dig power */
#define UART_WAKE_EN       (1UL << 24)         /* HP syscntl */
#define PAD_SLP_SEL        (1UL << 27)
#define PAUSE_WDT          (1UL << 28)
#define CPU_STALL          (1UL << 29)
#define I2C_ISO_EN         (1UL << 26)         /* HP clk power */
#define I2C_RETENTION      (1UL << 27)
#define XPD_PLL_ALL        (7UL << 28)
#define ICG_SYSCLK_EN      (1UL << 27)         /* HP sysclk */
#define SYSCLK_SLP_SEL     (1UL << 28)
#define ICG_SLP_SEL        (1UL << 29)
#define MODEM_CODE_ACTIVE  (2UL << 30)
#define UPDATE_ICG_SWITCH  (1UL << 28)
#define UPDATE_ICG_MODEM   (1UL << 31)
#define XPD_FOSC           (1UL << 30)         /* LP clk power */
#define XPD_XTAL           (1UL << 31)         /* HP and LP xtal */
#define HP_REG_CONNECT     (1UL << 15)         /* HP sleep regulator0 */
#define HP_REG_XPD         (1UL << 18)
#define LP_REG_XPD         (1UL << 22)         /* LP regulator0 */
#define REG_DBIAS_POS      27U                 /* [31:27], both */
#define REG_DBIAS_MSK      (0x1FUL << REG_DBIAS_POS)
#define DRV_B_MSK          (0xFUL << 28)       /* LP regulator1 */
#define DBG_ATTEN_POS      26U
#define DBG_ATTEN_MSK      (0xFUL << DBG_ATTEN_POS)
#define PD_CUR             (1UL << 30)
#define BIAS_SLEEP         (1UL << 31)
#define SLEEP_REQ          (1UL << 31)
#define REJECT_CLR         (1UL << 31)
#define INT_REJECT         (1UL << 30)
#define INT_WAKE           (1UL << 31)

/* Clock gates kept in light sleep (pmu_icg_mapping.h). */
#define ICG_UART0          (1UL << 3)
#define ICG_USB_FUNC       (1UL << 6)
#define ICG_SYSTIMER       (1UL << 18)
#define ICG_IOMUX          (1UL << 28)
#define ICG_USB_APB        (1UL << 17)

/* LP timer (lp_timer_reg.h). */
#define LPT_TAR0_LO        (LP_TIMER + 0x00UL)
#define LPT_TAR0_HI        (LP_TIMER + 0x04UL)
#define LPT_UPDATE         (LP_TIMER + 0x10UL)
#define LPT_BUF0_LO        (LP_TIMER + 0x14UL)
#define LPT_BUF0_HI        (LP_TIMER + 0x18UL)
#define LPT_INT_CLR        (LP_TIMER + 0x34UL)
#define LPT_SNAPSHOT       (1UL << 27)
#define LPT_TAR_EN         (1UL << 31)
#define LPT_WAKE_CLR       (1UL << 31)

/* LP watchdog (lp_wdt_reg.h). */
#define RWDT_CONFIG0       (LP_WDT + 0x00UL)
#define RWDT_CONFIG1       (LP_WDT + 0x04UL)
#define RWDT_FEED          (LP_WDT + 0x14UL)
#define RWDT_WPROTECT      (LP_WDT + 0x18UL)
#define RWDT_EN            (1UL << 31)
#define RWDT_STG0_SYS      (3UL << 28)         /* HP only: LP kept */
#define RWDT_STG0_CHIP     (4UL << 28)         /* the whole chip, LP too */
#define WDT_KEY            0x50D83AA1UL

/* UART0, the console when TIKU_CONSOLE=uart (uart_reg.h). */
#define UART_STATUS        (UART0 + 0x1CUL)
#define UART_SLEEP_CONF2   (UART0 + 0x38UL)
#define UART_RXCNT(s)      ((s) & 0xFFUL)
#define UART_WK_MSK        ((3UL << 26) | (0xFFUL << 10))
#define UART_WK_RX_BYTE    ((1UL << 26) | (1UL << 10))

/* The BOOT button's pin, and a GPIO pin register's wake fields. */
#define NAP_GPIO           28U
#define PIN_TYPE_MSK       (7UL << 7)
#define PIN_LOW_LEVEL      (4UL << 7)
#define PIN_WAKEUP         (1UL << 10)

#define LP_AON_STORE(n)    (LP_AON + 4UL * (n))
#define RC_FAST_HZ         17500000UL  /* the PMU's work clock, nominal */
#define RESET_DEEPSLEEP    5U          /* ROM reset code of a deep wake */
#define SLEEP_MISSED       0x534C5021UL    /* "SLP!" in LP AON store 0 */

/* Light idle: a deadline nearer than LIGHT_IDLE_MIN_US idles in wfi, and a
 * light sleep ends LIGHT_IDLE_EARLY_US before its deadline and lasts at most
 * LIGHT_IDLE_MAX_US. */
#define LIGHT_IDLE_MIN_US   3000ULL
#define LIGHT_IDLE_EARLY_US 1000ULL
#define LIGHT_IDLE_MAX_US   1000000ULL

/* Deep-sleep LP trims for a part fused without them (pmu_param.h). */
#define DSLP_DBG_DEFAULT   9UL
#define DSLP_DBIAS_DEFAULT 15UL
/* The supply wait's low bits keep the power-up inrush down on v1.2 and
 * later (pmu_sleep.c). */
#define INRUSH_WORKAROUND  3UL

static uint32_t lp_hz;

/* Light sleeps that ended on a wake, and their SYSTIMER time. */
static uint32_t light_count;
static uint64_t light_counts;

/* Latched at boot by tiku_c5_sleep_boot(). */
static uint32_t wake_cause;
static uint8_t  sleep_missed;
static uint64_t boot_lp;

/** @brief SYSTIMER's count, 16 MHz; 0 if the read failed. */
static uint64_t systimer(void)
{
    uint64_t v = 0;

    (void)tiku_c5_systimer_read(&v);
    return v;
}

uint64_t tiku_c5_lp_ticks(void)
{
    uint32_t lo, hi;

    REG(LPT_UPDATE) = LPT_SNAPSHOT;
    lo = REG(LPT_BUF0_LO);
    hi = REG(LPT_BUF0_HI) & 0xFFFFUL;
    return ((uint64_t)hi << 32) | lo;
}

uint32_t tiku_c5_lp_hz(void)
{
    uint64_t t0, t1, l0;

    /* The slow RC drifts from its nominal 136 kHz by several percent: 20 ms
     * against the 16 MHz SYSTIMER pins it to about 0.04 %. */
    if (lp_hz == 0UL) {
        l0 = tiku_c5_lp_ticks();
        t0 = systimer();
        do {
            t1 = systimer();
        } while (t1 - t0 < 16000000ULL / 50ULL);
        lp_hz = (uint32_t)(((tiku_c5_lp_ticks() - l0) * 16000000ULL) /
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

/** @brief Set @p msk in @p reg to @p val, already shifted. */
static void field(uint32_t reg, uint32_t msk, uint32_t val)
{
    REG(reg) = (REG(reg) & ~msk) | (val & msk);
}

/** @brief The wafer revision, major * 100 + minor (efuse_hal). */
static uint32_t chip_revision(void)
{
    uint32_t sys2 = REG(EFUSE_SYS2);

    return ((sys2 >> 4) & 3UL) * 100UL + (sys2 & 15UL);
}

/** @brief The block-2 eFuse version, major * 100 + minor. */
static uint32_t block_version(void)
{
    uint32_t sys2 = REG(EFUSE_SYS2);

    return ((sys2 >> 11) & 3UL) * 100UL + ((sys2 >> 8) & 7UL);
}

/**
 * @brief Program what every sleep shares.
 *
 * The power domains go to the PMU's state machine, the HP active state is
 * the one each wake restores, and the wake path's waits are set.
 *
 * @param slow_hz       LP slow clock rate
 * @param min_us        Shortest sleep
 * @param lp_analog_us  Settling time for the LP supply
 */
static void pmu_wake_config(uint32_t slow_hz, uint32_t min_us,
                            uint32_t lp_analog_us)
{
    static const uint32_t forced[] = {
        PD_TOP_CNTL, PD_HPAON_CNTL, PD_HPCPU_CNTL, PD_HPWIFI_CNTL,
        PD_LPPERI_CNTL
    };
    uint32_t min_slp = cap(cycles(min_us, slow_hz), 0xFFUL);
    uint32_t up = cycles(2U, RC_FAST_HZ), iso = cycles(1U, RC_FAST_HZ);
    uint32_t supply = cap(cycles(20U, RC_FAST_HZ), 0x1FFUL);

    if (chip_revision() >= 102UL) {
        supply |= INRUSH_WORKAROUND;
    }

    /* Reset leaves every domain forced on; the state machine must own them. */
    for (unsigned i = 0U; i < sizeof forced / sizeof forced[0]; i++) {
        REG(forced[i]) &= ~PD_FORCE_MSK;
    }
    REG(PD_MEM_CNTL) &= ~MEM_NO_ISO_MSK;

    /* HP active state, which the PMU applies on each wake: the system clock
     * running from the crystal, the PLL powered. */
    REG(HP_ACT_SYSCLK) = ICG_SYSCLK_EN;
    REG(HP_ACT_CLK_POWER) = XPD_PLL_ALL;
    REG(HP_ACT_ICG_MODEM) = MODEM_CODE_ACTIVE;
    REG(IMM_SLEEP_SYSCLK) = UPDATE_ICG_SWITCH;
    REG(IMM_MODEM_ICG) = UPDATE_ICG_MODEM;

    /* LP awake: the unused 32 kHz oscillators off. */
    REG(LP_ACT_DIG_POWER) = 0UL;
    REG(LP_ACT_CLK_POWER) = XPD_FOSC;

    /* The wake path's waits: minimum sleeps and sleep protection, analog
     * settling, supply and power-up, isolation and reset, the oscillators. */
    field(SLP_CNTL3, 0x3FFFFUL, min_slp | (min_slp << 8) | (2UL << 16));
    field(SLP_CNTL5, 0xFF000000UL,
          cap(cycles(lp_analog_us, slow_hz), 0xFFUL) << 24);
    field(SLP_CNTL7, 0xFFFF0000UL,
          cap(cycles(154U, RC_FAST_HZ), 0xFFFFUL) << 16);
    field(WAIT_TIMER0, 0xFFFFC000UL, (up << 14) | (supply << 23));
    field(WAIT_TIMER1, 0xFFFF0000UL, (up << 16) | (up << 23));
    REG(WAIT_TIMER2) = iso | (iso << 8) | (iso << 16) | (iso << 24);
    REG(CK_WAIT) = cycles(250U, RC_FAST_HZ) | (cycles(50U, RC_FAST_HZ) << 16);
}

/** @brief The PMU's deep-sleep settings, for a slow clock of @p slow_hz. */
static void pmu_deep_config(uint32_t slow_hz)
{
    uint32_t dbg = DSLP_DBG_DEFAULT, dbias = DSLP_DBIAS_DEFAULT;
    uint32_t chip = chip_revision(), block = block_version();

    /* get_dslp_dbg / get_dslp_lp_dbias: fused on v0.1 with block v0.1, and
     * on v1.0 and later with block v0.2 and later. */
    if ((chip == 1UL && block >= 1UL) || (chip >= 100UL && block >= 2UL)) {
        dbg = (REG(EFUSE_SYS3) >> 24) & 0xFUL;
        dbias = ((REG(EFUSE_SYS4) & 1UL) << 4) | (REG(EFUSE_SYS3) >> 28);
    }
    pmu_wake_config(slow_hz, 450U, 500U);

    /* HP sleep state, each register written whole: every domain down, the
     * flash's supply too, no clocks, the core stalled, and the system clock
     * moved to the crystal and gated on the way down.  The HP regulator is
     * off with its bias left on, as PMU_SLEEP_ANALOG_DSLP_CONFIG_DEFAULT has
     * it. */
    REG(HP_SLP_DIG_POWER) = PD_VDD_SPI | PD_WIFI | PD_CPU | PD_HP_AON | PD_TOP;
    REG(HP_SLP_ICG_FUNC) = 0UL;
    REG(HP_SLP_ICG_APB) = 0UL;
    REG(HP_SLP_ICG_MODEM) = 0UL;                        /* the sleep code */
    REG(HP_SLP_SYSCNTL) = UART_WAKE_EN | PAD_SLP_SEL | PAUSE_WDT | CPU_STALL;
    REG(HP_SLP_CLK_POWER) = I2C_ISO_EN | I2C_RETENTION;
    REG(HP_SLP_BIAS) = 0UL;
    REG(HP_SLP_SYSCLK) = SYSCLK_SLP_SEL | ICG_SLP_SEL;     /* to XTAL */
    REG(HP_SLP_REGULATOR0) = HP_REG_CONNECT;
    REG(HP_SLP_REGULATOR1) = 0UL;
    REG(HP_SLP_XTAL) = 0UL;

    /* LP asleep: its peripherals down, the slow RC alone running, the
     * regulator at its fused trim. */
    REG(LP_SLP_DIG_POWER) = LP_PERI_PD;
    REG(LP_SLP_CLK_POWER) = 0UL;
    REG(LP_SLP_XTAL) &= ~XPD_XTAL;
    REG(LP_SLP_REGULATOR0) = LP_REG_XPD | (dbias << REG_DBIAS_POS);
    field(LP_SLP_REGULATOR1, DRV_B_MSK, 0UL);
    field(LP_SLP_BIAS, DBG_ATTEN_MSK | PD_CUR | BIAS_SLEEP,
          (dbg << DBG_ATTEN_POS) | PD_CUR | BIAS_SLEEP);
}

/**
 * @brief Program the PMU's light-sleep state.
 *
 * Every domain but the Wi-Fi one stays powered and the core stalls with the
 * PLL off; the crystal, UART0, SYSTIMER and the pads keep their clocks, so
 * both regulators stay at their active voltages.
 */
static void pmu_light_config(uint32_t slow_hz)
{
    uint32_t hp = REG(HP_ACT_REGULATOR0) & REG_DBIAS_MSK;
    uint32_t lp = REG(LP_ACT_REGULATOR0) & REG_DBIAS_MSK;

    pmu_wake_config(slow_hz, 65U, 154U);

    /* The flash's supply stays up: the code above the SRAM image runs from
     * it the moment the core wakes. */
    REG(HP_SLP_DIG_POWER) = PD_WIFI;
#if defined(TIKU_CONSOLE_JTAG)
    /* The USB console's controller keeps its clocks and the PLL its 48 MHz,
     * so the host's transfers are answered while the core sleeps. */
    REG(HP_SLP_ICG_FUNC) = ICG_USB_FUNC | ICG_SYSTIMER | ICG_IOMUX;
    REG(HP_SLP_ICG_APB) = ICG_USB_APB;
    REG(HP_SLP_CLK_POWER) = I2C_ISO_EN | I2C_RETENTION | XPD_PLL_ALL;
#else
    REG(HP_SLP_ICG_FUNC) = ICG_UART0 | ICG_SYSTIMER | ICG_IOMUX;
    REG(HP_SLP_ICG_APB) = 0UL;
    REG(HP_SLP_CLK_POWER) = I2C_ISO_EN | I2C_RETENTION;
#endif
    REG(HP_SLP_ICG_MODEM) = 0UL;
    REG(HP_SLP_SYSCNTL) = UART_WAKE_EN | PAD_SLP_SEL | PAUSE_WDT | CPU_STALL;
    REG(HP_SLP_BIAS) = 0UL;
    REG(HP_SLP_SYSCLK) = ICG_SYSCLK_EN | SYSCLK_SLP_SEL | ICG_SLP_SEL;
    REG(HP_SLP_REGULATOR0) = HP_REG_CONNECT | HP_REG_XPD | hp;
    REG(HP_SLP_REGULATOR1) = 0UL;
    REG(HP_SLP_XTAL) = XPD_XTAL;

    REG(LP_SLP_DIG_POWER) = LP_PERI_PD;
    REG(LP_SLP_CLK_POWER) = 0UL;
    REG(LP_SLP_XTAL) |= XPD_XTAL;
    REG(LP_SLP_REGULATOR0) = LP_REG_XPD | lp;
    field(LP_SLP_REGULATOR1, DRV_B_MSK, 0UL);
    field(LP_SLP_BIAS, DBG_ATTEN_MSK | PD_CUR | BIAS_SLEEP, 0UL);
}

/**
 * @brief Arm the LP watchdog to reset @p hold slow ticks from now, for a
 *        wake that never comes.
 *
 * @param action RWDT_STG0_SYS resets the HP system and keeps the LP domain
 *               and what the PMU recorded; RWDT_STG0_CHIP resets the whole
 *               chip, which also ends a PMU stuck between states.
 */
static void sleep_safety_net(uint64_t hold, uint32_t action)
{
    REG(RWDT_WPROTECT) = WDT_KEY;
    REG(RWDT_CONFIG1) = (hold > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)hold;
    REG(RWDT_CONFIG0) = RWDT_EN | action |
                        (1UL << 13) | (1UL << 16);  /* reset lengths */
    REG(RWDT_FEED) = 1UL << 31;
    REG(RWDT_WPROTECT) = 0UL;
}

/** @brief Stand the safety net down once a light sleep has come back. */
static void sleep_safety_off(void)
{
    REG(RWDT_WPROTECT) = WDT_KEY;
    REG(RWDT_CONFIG0) = 0UL;
    REG(RWDT_WPROTECT) = 0UL;
}

/** @brief Arm the LP timer's wake @p us from now; 0 disarms it. */
static void lp_timer_wake(uint64_t us, uint32_t hz)
{
    uint64_t target;

    REG(LPT_TAR0_HI) &= ~LPT_TAR_EN;
    REG(LPT_INT_CLR) = LPT_WAKE_CLR;
    if (us != 0ULL) {
        target = tiku_c5_lp_ticks() + (us * hz) / 1000000ULL;
        REG(LPT_TAR0_LO) = (uint32_t)target;
        REG(LPT_TAR0_HI) = ((uint32_t)(target >> 32) & 0xFFFFUL) | LPT_TAR_EN;
    }
}

/** @brief Give the console up to @p ms to hand its last bytes to the host. */
static void console_drain(unsigned ms)
{
    while (ms-- > 0U) {
        tiku_debug_arch_poll();
        tiku_cpu_c5_delay_us(1000U);
    }
}

void tiku_c5_deep_sleep(uint64_t us)
{
    uint32_t hz = tiku_c5_lp_hz();
    uint64_t target;

    /* The checkpoint: SRAM is about to lose power, the mirror is not. */
    (void)tiku_mem_arch_nvm_flush_status();
    console_drain(50U);
    (void)TIKU_C5_IRQ_SAVE();

    /* Off the PLL first: it powers down with the HP domain. */
    (void)tiku_c5_clock_park();
    pmu_deep_config(hz);

    /* A deep sleep left half entered (2026-10-13: no wake, the USB link
     * held, an HP reset not enough) needs the whole chip reset. */
    if (us != 0ULL) {
        sleep_safety_net((us * hz) / 1000000ULL + hz, RWDT_STG0_CHIP);
    }
    lp_timer_wake(us, hz);

    /* The ROM, on the way back: the sleep was deep, and there is no stub. */
    REG(LP_AON_STORE(6)) = 0UL;
    REG(LP_AON_STORE(8)) = 1UL;
    REG(LP_AON_STORE(0)) = SLEEP_MISSED;

    REG(SLP_CNTL2) = (us != 0ULL) ? TIKU_C5_WAKE_TIMER : 0UL;
    REG(SLP_CNTL1) = 0UL;
    REG(HP_INT_CLR) = INT_WAKE | INT_REJECT;
    REG(SLP_CNTL4) = REJECT_CLR;
    REG(SLP_CNTL0) = SLEEP_REQ;

    /* Powered down within a millisecond.  A second awake means the sleep
     * never took the core; the store says so to the boot this reset starts. */
    target = systimer();
    while (systimer() - target < 16000000ULL) {
    }
    tiku_cpu_c5_restart();
}

uint32_t tiku_c5_light_sleep(uint64_t us, unsigned flags)
{
    uint32_t hz = tiku_c5_lp_hz();
    uint32_t s, wakes = 0UL, pin = 0UL, cause = 0UL;
    uint32_t tree;
    uint64_t t0;

    /* A radio's MAC runs from the PLL this sleep stops. */
    if (tiku_c5_analog_owner() == TIKU_C5_ANALOG_PHY) {
        return 0UL;
    }
    s = TIKU_C5_IRQ_SAVE();

    /* The PLL stops asleep: the core leaves it first, back after. */
    tree = tiku_c5_clock_park();
    pmu_light_config(hz);

#if !defined(TIKU_CONSOLE_JTAG)
    field(UART_SLEEP_CONF2, UART_WK_MSK, UART_WK_RX_BYTE);
    wakes |= TIKU_C5_WAKE_UART0;
#endif
    if ((flags & TIKU_C5_NAP_PIN) != 0U) {
        pin = REG(GPIO_PIN(NAP_GPIO));
        REG(GPIO_PIN(NAP_GPIO)) =
            (pin & ~PIN_TYPE_MSK) | PIN_LOW_LEVEL | PIN_WAKEUP;
        wakes |= TIKU_C5_WAKE_GPIO;
    }
    if (us != 0ULL) {
        sleep_safety_net((us * hz) / 1000000ULL + hz, RWDT_STG0_SYS);
        wakes |= TIKU_C5_WAKE_TIMER;
    }
    lp_timer_wake(us, hz);

    /* A light sleep: the deep flag is cleared so the ROM does not take a
     * later reset for a deep wake.  A UART byte or pin wake already pending
     * rejects the sleep. */
    REG(LP_AON_STORE(8)) &= ~1UL;
    REG(SLP_CNTL2) = wakes;
    REG(SLP_CNTL1) = wakes & (TIKU_C5_WAKE_UART0 | TIKU_C5_WAKE_GPIO);
    REG(HP_INT_CLR) = INT_WAKE | INT_REJECT;
    REG(SLP_CNTL4) = REJECT_CLR;

    /* The core stalls as the request is written, so the time is read first.
     * SYSTIMER counts on, so the bound is real time. */
    t0 = systimer();
    REG(SLP_CNTL0) = SLEEP_REQ;
    while ((REG(HP_INT_RAW) & (INT_WAKE | INT_REJECT)) == 0UL &&
           systimer() - t0 < (us + 2000000ULL) * 16ULL) {
    }
    if ((REG(HP_INT_RAW) & INT_WAKE) != 0UL) {
        cause = REG(WAKE_STATUS);
        light_count++;
        light_counts += systimer() - t0;
    }
    REG(HP_INT_CLR) = INT_WAKE | INT_REJECT;

    lp_timer_wake(0ULL, hz);
    if (us != 0ULL) {
        sleep_safety_off();
    }
    if ((flags & TIKU_C5_NAP_PIN) != 0U) {
        REG(GPIO_PIN(NAP_GPIO)) = pin;
    }
    tiku_c5_clock_unpark(tree);
    TIKU_C5_IRQ_RESTORE(s);
    return cause;
}

/** @brief Whether the console has bytes to send or received bytes waiting. */
static int console_busy(void)
{
#if defined(TIKU_CONSOLE_JTAG)
    return tiku_serial_jtag_pending() != 0U || tiku_debug_arch_rx_ready();
#else
    return UART_RXCNT(REG(UART_STATUS)) != 0UL;
#endif
}

void tiku_c5_light_idle(void)
{
    uint64_t now = systimer();
    uint64_t due = tiku_c5_alarm_due(now), us;

    /* Queued console bytes and a radio's PLL clocks would stop, and a byte
     * already received would reject the sleep; these, and a deadline nearer
     * than LIGHT_IDLE_MIN_US, idle in wfi. */
    tiku_debug_arch_poll();
    if (now == 0ULL || console_busy() ||
        tiku_c5_analog_owner() == TIKU_C5_ANALOG_PHY ||
        due < now + LIGHT_IDLE_MIN_US * 16ULL) {
        __asm__ volatile("wfi" ::: "memory");
        return;
    }
    us = (due - now) / 16ULL - LIGHT_IDLE_EARLY_US;
    (void)tiku_c5_light_sleep(us < LIGHT_IDLE_MAX_US ? us : LIGHT_IDLE_MAX_US,
                              0U);
}

void tiku_c5_sleep_boot(void)
{
    uint32_t code = tiku_c5_rom_reset_reason(0);

    boot_lp = tiku_c5_lp_ticks();

    /* The store outlives a successful sleep too, so it is read and cleared
     * on every boot, or a later reset would be taken for a missed sleep. */
    wake_cause = (code == RESET_DEEPSLEEP) ? REG(WAKE_STATUS) : 0UL;
    sleep_missed = (uint8_t)(code != RESET_DEEPSLEEP &&
                             REG(LP_AON_STORE(0)) == SLEEP_MISSED);
    REG(LP_AON_STORE(0)) = 0UL;
}

void tiku_c5_light_stats(uint32_t *count, uint64_t *us)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();

    *count = light_count;
    *us = light_counts / 16ULL;
    TIKU_C5_IRQ_RESTORE(state);
}

uint32_t tiku_c5_wake_cause(void)
{
    return wake_cause;
}

uint64_t tiku_c5_boot_lp_ticks(void)
{
    return boot_lp;
}

int tiku_c5_sleep_missed(void)
{
    return sleep_missed;
}
