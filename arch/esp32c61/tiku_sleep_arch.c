/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sleep_arch.c - ESP32-C61 deep and light sleep through the PMU.
 *
 * PMU sleep defaults, with the factory-fused regulator trims and wake waits
 * counted from the PMU's timing constants at the oscillators' rates.  Light
 * sleep keeps the crystal, so the console and system timer run through it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_sleep_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c61_regs.h"
#include "tiku_mem_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_dma_arch.h"

#define RC_FAST_HZ      17500000UL  /* the PMU's work clock, nominal */

/* A light sleep costs about a millisecond in and out: nearer deadlines idle
 * in wfi, and a sleep ends that far ahead of the one it waits for. */
#define LIGHT_IDLE_MIN_US   3000ULL
#define LIGHT_IDLE_EARLY_US 1000ULL
#define LIGHT_IDLE_MAX_US   1000000ULL
#define SLEEP_MISSED    0x534C5021UL    /* "SLP!" in LP AON store 9 */

/* Deep-sleep trims for a part fused without them. */
#define DSLP_DBG_DEFAULT    13UL
#define DSLP_DBIAS_DEFAULT  23UL
#define HP_DBG_DEEPSLEEP    13UL

static uint32_t lp_hz;

/* Latched at boot by tiku_esp32c61_sleep_boot(). */
static uint32_t wake_cause;
static uint8_t  sleep_missed;
static uint64_t boot_lp;
static volatile uint8_t sleep_holds;    /* takers that need the PLL on */

uint64_t tiku_esp32c61_lp_ticks(void) {
    uint32_t lo, hi;

    TIKU_REG32(ESP32C61_LP_TIMER_UPDATE) = ESP32C61_LP_TIMER_SNAPSHOT;
    lo = TIKU_REG32(ESP32C61_LP_TIMER_BUF0_LO);
    hi = TIKU_REG32(ESP32C61_LP_TIMER_BUF0_HI) & 0xFFFFUL;
    return ((uint64_t)hi << 32) | lo;
}

uint32_t tiku_esp32c61_lp_hz(void) {
    uint64_t t0, t1, l0;

    /* The slow RC drifts from its nominal 136 kHz by several percent: 20 ms
     * against the 16 MHz SYSTIMER pins it to about 0.04 %. */
    if (lp_hz == 0UL) {
        l0 = tiku_esp32c61_lp_ticks();
        t0 = tiku_cpu_esp32c61_systimer();
        do {
            t1 = tiku_cpu_esp32c61_systimer();
        } while (t1 - t0 < 16000000ULL / 50ULL);
        lp_hz = (uint32_t)(((tiku_esp32c61_lp_ticks() - l0) * 16000000ULL) /
                           (t1 - t0));
    }
    return lp_hz;
}

/** @brief Whole cycles of a @p hz clock in @p us, rounded up. */
static uint32_t cycles(uint32_t us, uint32_t hz) {
    return (uint32_t)(((uint64_t)us * hz + 999999ULL) / 1000000ULL);
}

static uint32_t cap(uint32_t v, uint32_t max) {
    return (v > max) ? max : v;
}

/** @brief Set @p msk in @p reg to @p val, already shifted. */
static void field(uint32_t reg, uint32_t msk, uint32_t val) {
    TIKU_REG32(reg) = (TIKU_REG32(reg) & ~msk) | (val & msk);
}

/**
 * @brief What every sleep shares: the domains handed to the state machine,
 *        the HP system as each wake restores it, and the wake path's waits
 *        -- @p min_us asleep at least, @p lp_analog_us for the LP supply.
 */
static void pmu_wake_config(uint32_t slow_hz, uint32_t min_us,
                            uint32_t lp_analog_us) {
    static const uint32_t forced[] = {
        ESP32C61_PMU_PD_TOP_CNTL, ESP32C61_PMU_PD_HPAON_CNTL,
        ESP32C61_PMU_PD_HPCPU_CNTL, ESP32C61_PMU_PD_HPWIFI_CNTL,
        ESP32C61_PMU_PD_LPPERI_CNTL
    };
    uint32_t min_slp = cap(cycles(min_us, slow_hz), 0xFFUL);
    uint32_t up = cycles(2U, RC_FAST_HZ), iso = cycles(1U, RC_FAST_HZ);

    /* Reset leaves every domain forced on; the state machine must own them. */
    for (unsigned i = 0U; i < sizeof forced / sizeof forced[0]; i++) {
        TIKU_REG32(forced[i]) &= ~ESP32C61_PMU_PD_FORCE_MSK;
    }
    TIKU_REG32(ESP32C61_PMU_PD_MEM_CNTL) &= ~ESP32C61_PMU_MEM_NO_ISO_MSK;

    /* HP awake, as the PMU restores it on a wake: reset leaves the system
     * clock gated and the PLL off here, which nothing applies until the
     * first state change -- a wake from sleep. */
    TIKU_REG32(ESP32C61_PMU_HP_ACT_SYSCLK) = ESP32C61_PMU_ICG_SYSCLK_EN;
    TIKU_REG32(ESP32C61_PMU_HP_ACT_CLK_POWER) = ESP32C61_PMU_XPD_PLL_ALL;
    TIKU_REG32(ESP32C61_PMU_HP_ACT_ICG_MODEM) = ESP32C61_PMU_MODEM_CODE_ACTIVE;
    TIKU_REG32(ESP32C61_PMU_IMM_SLEEP_SYSCLK) = ESP32C61_PMU_UPDATE_ICG_SWITCH;
    TIKU_REG32(ESP32C61_PMU_IMM_MODEM_ICG) = ESP32C61_PMU_UPDATE_ICG_MODEM;

    /* LP awake: the unused 32 kHz oscillators off. */
    TIKU_REG32(ESP32C61_PMU_LP_ACT_DIG_POWER) = 0UL;
    TIKU_REG32(ESP32C61_PMU_LP_ACT_CLK_POWER) = ESP32C61_PMU_XPD_FOSC;

    /* The wake path's waits: minimum sleeps and sleep protection, analog
     * settling, supply and power-up, isolation and reset, the oscillators. */
    field(ESP32C61_PMU_SLP_CNTL3, 0x3FFFFUL,
          min_slp | (min_slp << 8) | (2UL << 16));
    field(ESP32C61_PMU_SLP_CNTL5, 0xFF000000UL,
          cap(cycles(lp_analog_us, slow_hz), 0xFFUL) << 24);
    field(ESP32C61_PMU_SLP_CNTL7, 0xFFFF0000UL,
          cap(cycles(154U, RC_FAST_HZ), 0xFFFFUL) << 16);
    field(ESP32C61_PMU_WAIT_TIMER0, 0xFFFFC000UL,
          (up << 14) | (cap(cycles(20U, RC_FAST_HZ), 0x1FFUL) << 23));
    field(ESP32C61_PMU_WAIT_TIMER1, 0xFFFF0000UL, (up << 16) | (up << 23));
    TIKU_REG32(ESP32C61_PMU_WAIT_TIMER2) =
        iso | (iso << 8) | (iso << 16) | (iso << 24);
    TIKU_REG32(ESP32C61_PMU_CK_WAIT) =
        cycles(250U, RC_FAST_HZ) | (cycles(50U, RC_FAST_HZ) << 16);
}

/** @brief The PMU's deep-sleep settings, for a slow clock of @p slow_hz. */
static void pmu_deep_config(uint32_t slow_hz) {
    uint32_t dbg = DSLP_DBG_DEFAULT, dbias = DSLP_DBIAS_DEFAULT;
    uint32_t sys3 = TIKU_REG32(ESP32C61_EFUSE_SYS3);

    if (((TIKU_REG32(ESP32C61_EFUSE_SYS2) >> ESP32C61_EFUSE_BLK_VER_POS) &
         0x1FUL) != 0UL) {
        dbg = (sys3 >> ESP32C61_EFUSE_DSLP_DBG_POS) & 0xFUL;
        dbias = (sys3 >> ESP32C61_EFUSE_DSLP_DBIAS_POS) & 0x1FUL;
    }
    pmu_wake_config(slow_hz, 450U, 500U);

    /* HP asleep, each register whole as IDF's boot and deep-sleep settings
     * leave it: every domain down, the flash's supply too; no clocks, the
     * core stalled, and the system clock moved to the crystal and gated on
     * the way down -- left unset, the PMU stops the PLL under a system still
     * running from it, and the part freezes half asleep. */
    TIKU_REG32(ESP32C61_PMU_HP_SLP_DIG_POWER) =
        ESP32C61_PMU_PD_VDD_SPI | ESP32C61_PMU_PD_WIFI | ESP32C61_PMU_PD_CPU |
        ESP32C61_PMU_PD_HP_AON | ESP32C61_PMU_PD_TOP;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_ICG_FUNC) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_ICG_APB) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_ICG_MODEM) = 0UL;        /* the sleep code */
    TIKU_REG32(ESP32C61_PMU_HP_SLP_SYSCNTL) =
        ESP32C61_PMU_UART_WAKE_EN | ESP32C61_PMU_PAD_SLP_SEL |
        ESP32C61_PMU_PAUSE_WDT | ESP32C61_PMU_CPU_STALL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_CLK_POWER) =
        ESP32C61_PMU_I2C_ISO_EN | ESP32C61_PMU_I2C_RETENTION;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_BIAS) =
        (HP_DBG_DEEPSLEEP << ESP32C61_PMU_DBG_ATTEN_POS) |
        ESP32C61_PMU_PD_CUR | ESP32C61_PMU_BIAS_SLEEP;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_SYSCLK) =
        ESP32C61_PMU_SYSCLK_SLP_SEL | ESP32C61_PMU_ICG_SLP_SEL;  /* to XTAL */
    TIKU_REG32(ESP32C61_PMU_HP_SLP_REGULATOR0) = ESP32C61_PMU_HP_REG_CONNECT;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_REGULATOR1) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_XTAL) = 0UL;

    /* LP asleep: its peripherals down, the slow RC alone running, the
     * regulator at its fused trim. */
    TIKU_REG32(ESP32C61_PMU_LP_SLP_DIG_POWER) = ESP32C61_PMU_LP_PERI_PD;
    TIKU_REG32(ESP32C61_PMU_LP_SLP_CLK_POWER) = 0UL;
    TIKU_REG32(ESP32C61_PMU_LP_SLP_XTAL) &= ~ESP32C61_PMU_XPD_XTAL;
    TIKU_REG32(ESP32C61_PMU_LP_SLP_REGULATOR0) =
        ESP32C61_PMU_LP_REG_XPD | (dbias << ESP32C61_PMU_REG_DBIAS_POS);
    field(ESP32C61_PMU_LP_SLP_REGULATOR1, 0xF0000000UL, 0UL);
    field(ESP32C61_PMU_LP_SLP_BIAS, ESP32C61_PMU_DBG_ATTEN_MSK |
          ESP32C61_PMU_PD_CUR | ESP32C61_PMU_BIAS_SLEEP,
          (dbg << ESP32C61_PMU_DBG_ATTEN_POS) |
          ESP32C61_PMU_PD_CUR | ESP32C61_PMU_BIAS_SLEEP);
}

/**
 * @brief The PMU's light-sleep settings: every domain stays powered and the
 *        core stalls with the PLL off, while the crystal, UART0, SYSTIMER and
 *        the pads keep their clocks -- so both regulators stay at the
 *        voltages the system runs on now, as IDF does with the crystal up.
 */
static void pmu_light_config(uint32_t slow_hz) {
    uint32_t hp = TIKU_REG32(ESP32C61_PMU_HP_ACT_REGULATOR0) &
                  ESP32C61_PMU_REG_DBIAS_MSK;
    uint32_t lp = TIKU_REG32(ESP32C61_PMU_LP_ACT_REGULATOR0) &
                  ESP32C61_PMU_REG_DBIAS_MSK;

    pmu_wake_config(slow_hz, 65U, 154U);

    /* The flash's supply stays up: the PSRAM in the package shares it. */
    TIKU_REG32(ESP32C61_PMU_HP_SLP_DIG_POWER) = ESP32C61_PMU_PD_WIFI;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_ICG_FUNC) =
        ESP32C61_ICG_UART0 | ESP32C61_ICG_SYSTIMER | ESP32C61_ICG_IOMUX;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_ICG_APB) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_ICG_MODEM) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_SYSCNTL) =
        ESP32C61_PMU_UART_WAKE_EN | ESP32C61_PMU_PAD_SLP_SEL |
        ESP32C61_PMU_PAUSE_WDT | ESP32C61_PMU_CPU_STALL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_CLK_POWER) =
        ESP32C61_PMU_I2C_ISO_EN | ESP32C61_PMU_I2C_RETENTION;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_BIAS) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_SYSCLK) =
        ESP32C61_PMU_ICG_SYSCLK_EN | ESP32C61_PMU_SYSCLK_SLP_SEL |
        ESP32C61_PMU_ICG_SLP_SEL;                           /* on XTAL */
    TIKU_REG32(ESP32C61_PMU_HP_SLP_REGULATOR0) =
        ESP32C61_PMU_HP_REG_CONNECT | ESP32C61_PMU_HP_REG_XPD | hp;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_REGULATOR1) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_SLP_XTAL) = ESP32C61_PMU_XPD_XTAL;

    TIKU_REG32(ESP32C61_PMU_LP_SLP_DIG_POWER) = ESP32C61_PMU_LP_PERI_PD;
    TIKU_REG32(ESP32C61_PMU_LP_SLP_CLK_POWER) = 0UL;
    TIKU_REG32(ESP32C61_PMU_LP_SLP_XTAL) |= ESP32C61_PMU_XPD_XTAL;
    TIKU_REG32(ESP32C61_PMU_LP_SLP_REGULATOR0) = ESP32C61_PMU_LP_REG_XPD | lp;
    field(ESP32C61_PMU_LP_SLP_REGULATOR1, ESP32C61_PMU_DRV_B_MSK, 0UL);
    field(ESP32C61_PMU_LP_SLP_BIAS, ESP32C61_PMU_DBG_ATTEN_MSK |
          ESP32C61_PMU_PD_CUR | ESP32C61_PMU_BIAS_SLEEP, 0UL);
}

/**
 * @brief The safety net: should the wake not come, the LP watchdog resets the
 *        HP system @p hold slow ticks from now.  The LP domain, and what the
 *        PMU last recorded, outlive that reset.
 */
static void sleep_safety_net(uint64_t hold) {
    TIKU_REG32(ESP32C61_RWDT_WPROTECT) = ESP32C61_WDT_WKEY;
    TIKU_REG32(ESP32C61_RWDT_CONFIG1) =
        (hold > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)hold;
    TIKU_REG32(ESP32C61_RWDT_CONFIG0) =
        ESP32C61_RWDT_EN | ESP32C61_RWDT_STG0_SYS_RESET |
        (1UL << 13) | (1UL << 16);          /* reset lengths, as at reset */
    TIKU_REG32(ESP32C61_RWDT_FEED) = 1UL << 31;
    TIKU_REG32(ESP32C61_RWDT_WPROTECT) = 0UL;
}

/** @brief Stand the safety net down once a light sleep has come back. */
static void sleep_safety_off(void) {
    TIKU_REG32(ESP32C61_RWDT_WPROTECT) = ESP32C61_WDT_WKEY;
    TIKU_REG32(ESP32C61_RWDT_CONFIG0) = 0UL;
    TIKU_REG32(ESP32C61_RWDT_WPROTECT) = 0UL;
}

/** @brief Arm the LP timer's wake @p us from now; 0 disarms it. */
static void lp_timer_wake(uint64_t us, uint32_t hz) {
    uint64_t target;

    TIKU_REG32(ESP32C61_LP_TIMER_TAR0_HI) &= ~ESP32C61_LP_TIMER_TAR_EN;
    TIKU_REG32(ESP32C61_LP_TIMER_INT_CLR) = ESP32C61_LP_TIMER_WAKE_CLR;
    if (us != 0ULL) {
        target = tiku_esp32c61_lp_ticks() + (us * hz) / 1000000ULL;
        TIKU_REG32(ESP32C61_LP_TIMER_TAR0_LO) = (uint32_t)target;
        TIKU_REG32(ESP32C61_LP_TIMER_TAR0_HI) =
            ((uint32_t)(target >> 32) & 0xFFFFUL) | ESP32C61_LP_TIMER_TAR_EN;
    }
}

void tiku_esp32c61_deep_sleep(uint64_t us) {
    uint32_t hz = tiku_esp32c61_lp_hz();
    uint64_t target;

    /* The checkpoint: SRAM is about to lose power, the mirror is not. */
    (void)tiku_mem_arch_nvm_flush_status();
    for (unsigned long spins = 2000000UL; spins > 0UL; spins--) {
        if (ESP32C61_UART_TXCNT(TIKU_REG32(ESP32C61_UART_STATUS(
                ESP32C61_UART0_BASE))) == 0UL) {
            break;
        }
    }
    tiku_cpu_esp32c61_delay_us(200U);   /* the last byte leaves the shifter */
    (void)tiku_esp32c61_mie_off();

    /* Off the PLL first: it powers down with the HP domain. */
    (void)tiku_cpu_freq_esp32c61_set(40U);
    pmu_deep_config(hz);

    if (us != 0ULL) {
        sleep_safety_net((us * hz) / 1000000ULL + hz);
    }
    lp_timer_wake(us, hz);

    /* The ROM, on the way back: the sleep was deep, and there is no stub. */
    TIKU_REG32(ESP32C61_LP_AON_STORE(6)) = 0UL;
    TIKU_REG32(ESP32C61_LP_AON_STORE(8)) = 1UL;
    TIKU_REG32(ESP32C61_LP_AON_STORE(9)) = SLEEP_MISSED;

    TIKU_REG32(ESP32C61_PMU_SLP_CNTL2) =
        (us != 0ULL) ? ESP32C61_PMU_WAKE_TIMER : 0UL;
    TIKU_REG32(ESP32C61_PMU_SLP_CNTL1) = 0UL;
    TIKU_REG32(ESP32C61_PMU_HP_INT_CLR) = ESP32C61_PMU_INT_WAKE_REJECT;
    TIKU_REG32(ESP32C61_PMU_SLP_CNTL4) = ESP32C61_PMU_REJECT_CLR;
    TIKU_REG32(ESP32C61_PMU_SLP_CNTL0) = ESP32C61_PMU_SLEEP_REQ;

    /* Powered down within a millisecond.  A second awake means the sleep
     * never took the core; the store says so to the boot this reset starts. */
    target = tiku_cpu_esp32c61_systimer();
    while (tiku_cpu_esp32c61_systimer() - target < 16000000ULL) {
    }
    tiku_cpu_esp32c61_restart(0);
}

uint32_t tiku_esp32c61_light_sleep(uint64_t us, unsigned flags) {
    uint32_t hz = tiku_esp32c61_lp_hz();
    uint32_t s = tiku_esp32c61_mie_off();
    uint32_t wakes = ESP32C61_PMU_WAKE_UART0, pin = 0UL, cause = 0UL;
    uint32_t tree;
    uint64_t t0;

    /* The PLL stops asleep: the core leaves it first, back after. */
    tree = tiku_cpu_esp32c61_clock_park();
    pmu_light_config(hz);

    TIKU_REG32(ESP32C61_UART_SLEEP_CONF2(ESP32C61_UART0_BASE)) =
        (TIKU_REG32(ESP32C61_UART_SLEEP_CONF2(ESP32C61_UART0_BASE)) &
         ~ESP32C61_UART_WK_MSK) | ESP32C61_UART_WK_RX_BYTES(1U);
    if ((flags & TIKU_ESP32C61_NAP_PIN) != 0U) {
        pin = TIKU_REG32(ESP32C61_GPIO_PIN(9U));
        TIKU_REG32(ESP32C61_GPIO_PIN(9U)) =
            (pin & ~ESP32C61_GPIO_PIN_TYPE_MSK) |
            ESP32C61_GPIO_PIN_LOW_LEVEL | ESP32C61_GPIO_PIN_WAKEUP;
        wakes |= ESP32C61_PMU_WAKE_GPIO;
    }
    if (us != 0ULL) {
        sleep_safety_net((us * hz) / 1000000ULL + hz);
        wakes |= ESP32C61_PMU_WAKE_TIMER;
    }
    lp_timer_wake(us, hz);

    /* Light, so the ROM must not take a later reset for a deep wake.  A
     * byte or a pin already waiting refuses the sleep instead of ending it. */
    TIKU_REG32(ESP32C61_LP_AON_STORE(8)) &= ~1UL;
    TIKU_REG32(ESP32C61_PMU_SLP_CNTL2) = wakes;
    TIKU_REG32(ESP32C61_PMU_SLP_CNTL1) =
        wakes & (ESP32C61_PMU_WAKE_UART0 | ESP32C61_PMU_WAKE_GPIO);
    TIKU_REG32(ESP32C61_PMU_HP_INT_CLR) = ESP32C61_PMU_INT_WAKE_REJECT;
    TIKU_REG32(ESP32C61_PMU_SLP_CNTL4) = ESP32C61_PMU_REJECT_CLR;
    TIKU_REG32(ESP32C61_PMU_SLP_CNTL0) = ESP32C61_PMU_SLEEP_REQ;

    /* The core stalls here.  SYSTIMER counts on, so the bound is real time. */
    t0 = tiku_cpu_esp32c61_systimer();
    while ((TIKU_REG32(ESP32C61_PMU_INT_RAW) &
            (ESP32C61_PMU_INT_WAKE | ESP32C61_PMU_INT_REJECT)) == 0UL &&
           tiku_cpu_esp32c61_systimer() - t0 < (us + 2000000ULL) * 16ULL) {
    }
    if ((TIKU_REG32(ESP32C61_PMU_INT_RAW) & ESP32C61_PMU_INT_WAKE) != 0UL) {
        cause = TIKU_REG32(ESP32C61_PMU_WAKE_CAUSE);
    }
    TIKU_REG32(ESP32C61_PMU_HP_INT_CLR) = ESP32C61_PMU_INT_WAKE_REJECT;

    lp_timer_wake(0ULL, hz);
    if (us != 0ULL) {
        sleep_safety_off();
    }
    if ((flags & TIKU_ESP32C61_NAP_PIN) != 0U) {
        TIKU_REG32(ESP32C61_GPIO_PIN(9U)) = pin;
    }
    tiku_cpu_esp32c61_clock_unpark(tree);
    tiku_esp32c61_mie_restore(s);
    return cause;
}

void tiku_esp32c61_sleep_hold(int on) {
    uint32_t m = tiku_esp32c61_mie_off();

    if (on) {
        sleep_holds++;
    } else if (sleep_holds != 0U) {
        sleep_holds--;
    }
    tiku_esp32c61_mie_restore(m);
}

void tiku_esp32c61_light_idle(void) {
    uint64_t now = tiku_cpu_esp32c61_systimer();
    uint64_t due = tiku_esp32c61_alarm_due(now), us;

    /* A byte already held would refuse the sleep, and a DMA copy or a held
     * radio would stop with its clock: wfi until their interrupts instead. */
    if (ESP32C61_UART_RXCNT(TIKU_REG32(ESP32C61_UART_STATUS(
            ESP32C61_UART0_BASE))) != 0UL || tiku_dma_arch_busy() ||
        sleep_holds != 0U || due < now + LIGHT_IDLE_MIN_US * 16ULL) {
        __asm__ volatile ("wfi" ::: "memory");
        return;
    }
    us = (due - now) / 16ULL - LIGHT_IDLE_EARLY_US;
    (void)tiku_esp32c61_light_sleep(us < LIGHT_IDLE_MAX_US ? us :
                                    LIGHT_IDLE_MAX_US, 0U);
}

void tiku_esp32c61_sleep_boot(void) {
    uint32_t code = tiku_cpu_esp32c61_reset_code();

    boot_lp = tiku_esp32c61_lp_ticks();

    /* The store outlives a successful sleep too, so it is read and cleared
     * on every boot, or a later reset would be taken for a missed sleep. */
    wake_cause = (code == ESP32C61_RESET_DEEPSLEEP) ?
                 TIKU_REG32(ESP32C61_PMU_WAKE_CAUSE) : 0UL;
    sleep_missed = (uint8_t)(code != ESP32C61_RESET_DEEPSLEEP &&
                             TIKU_REG32(ESP32C61_LP_AON_STORE(9)) ==
                                 SLEEP_MISSED);
    TIKU_REG32(ESP32C61_LP_AON_STORE(9)) = 0UL;
}

uint32_t tiku_esp32c61_wake_cause(void) {
    return wake_cause;
}

uint64_t tiku_esp32c61_boot_lp_ticks(void) {
    return boot_lp;
}

int tiku_esp32c61_sleep_missed(void) {
    return sleep_missed;
}

