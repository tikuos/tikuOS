/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - ESP32-C61 boot: watchdogs and the core clock.
 *
 * A flash boot leaves watchdogs armed that a RAM load does not; the boot
 * stops every one it finds and records which were running.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_esp32c61_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_sleep_arch.h"

static unsigned long cpu_hz = TIKU_ESP32C61_CPU_HZ;
static int           cpu_hz_fault = 1;
static uint8_t       wdt_found;

/** @brief Stop one TIMG main watchdog, noting whether it was running. */
static void mwdt_off(uint32_t base, uint8_t bit) {
    uint32_t cfg = TIKU_REG32(ESP32C61_TIMG_WDTCONFIG0(base));

    if (cfg & ESP32C61_TIMG_WDT_EN) {
        wdt_found |= bit;
    }
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(base)) = ESP32C61_WDT_WKEY;
    cfg &= ~(ESP32C61_TIMG_WDT_EN | ESP32C61_TIMG_WDT_FLASHBOOT);
    TIKU_REG32(ESP32C61_TIMG_WDTCONFIG0(base)) = cfg;
    /* The new config only takes effect once latched across clock domains. */
    TIKU_REG32(ESP32C61_TIMG_WDTCONFIG0(base)) = cfg | ESP32C61_TIMG_WDT_UPDATE;
    TIKU_REG32(ESP32C61_TIMG_WDTWPROTECT(base)) = 0UL;
}

/**
 * @brief Stop the RTC watchdog and put the super watchdog on auto-feed.
 *
 * The ROM leaves the super watchdog's disable locked, so it is put on
 * hardware auto-feed.
 */
static void lp_wdt_off(void) {
    uint32_t cfg = TIKU_REG32(ESP32C61_RWDT_CONFIG0);

    if (cfg & ESP32C61_RWDT_EN) {
        wdt_found |= TIKU_ESP32C61_WDT_RWDT;
    }
    TIKU_REG32(ESP32C61_RWDT_WPROTECT) = ESP32C61_WDT_WKEY;
    TIKU_REG32(ESP32C61_RWDT_CONFIG0) =
        cfg & ~(ESP32C61_RWDT_EN | ESP32C61_RWDT_FLASHBOOT);
    TIKU_REG32(ESP32C61_RWDT_WPROTECT) = 0UL;

    cfg = TIKU_REG32(ESP32C61_SWD_CONFIG);
    if ((cfg & (ESP32C61_SWD_DISABLE | ESP32C61_SWD_AUTO_FEED)) == 0UL) {
        wdt_found |= TIKU_ESP32C61_WDT_SWD;
    }
    TIKU_REG32(ESP32C61_SWD_WPROTECT) = ESP32C61_WDT_WKEY;
    TIKU_REG32(ESP32C61_SWD_CONFIG) = cfg | ESP32C61_SWD_AUTO_FEED;
    TIKU_REG32(ESP32C61_SWD_WPROTECT) = 0UL;
}

uint64_t tiku_cpu_esp32c61_systimer(void) {
    /* One snapshot serves both halves, so an interrupt that took its own
     * between them would tear the value: hold MIE off across the pair. */
    uint32_t s = tiku_esp32c61_mie_off();
    uint64_t v = 0ULL;

    TIKU_REG32(ESP32C61_SYSTIMER_UNIT0_OP) = ESP32C61_SYSTIMER_UPDATE;
    for (unsigned spins = 1000U; spins > 0U; spins--) {
        if (TIKU_REG32(ESP32C61_SYSTIMER_UNIT0_OP) & ESP32C61_SYSTIMER_VALID) {
            v = (uint64_t)(TIKU_REG32(ESP32C61_SYSTIMER_UNIT0_HI) & 0xFFFFFUL);
            v = (v << 32) | TIKU_REG32(ESP32C61_SYSTIMER_UNIT0_LO);
            break;
        }
    }
    tiku_esp32c61_mie_restore(s);
    return v;
}

/**
 * @brief Count core cycles across 2 ms of SYSTIMER time.
 *
 * If SYSTIMER never latches, or never moves within the bounded wait, cpu_hz
 * keeps the rate the clock tree implies and the fault flag stays set.
 */
static void measure_cpu_hz(void) {
    uint64_t t0, t;
    uint32_t c0, c1;
    unsigned long spins;
    tiku_esp32c61_clock_t tree;

    cpu_hz_fault = 1;
    tiku_cpu_esp32c61_clock_probe(&tree);
    if (tree.cpu_hz != 0UL) {
        cpu_hz = tree.cpu_hz;
    }
    TIKU_REG32(ESP32C61_SYSTIMER_CONF) |=
        ESP32C61_SYSTIMER_CLK_EN | ESP32C61_SYSTIMER_UNIT0_EN;
    t0 = tiku_cpu_esp32c61_systimer();
    c0 = ESP32C61_CSR_READ(mcycle);
    spins = 50000000UL;
    do {
        t = tiku_cpu_esp32c61_systimer();
    } while (t != 0ULL && t - t0 < 2UL * (ESP32C61_SYSTIMER_HZ / 1000UL) &&
             --spins > 0UL);
    c1 = ESP32C61_CSR_READ(mcycle);
    if (t0 == 0ULL || t == 0ULL || spins == 0UL || t == t0) {
        return;
    }
    cpu_hz = (unsigned long)(((uint64_t)(c1 - c0) * ESP32C61_SYSTIMER_HZ) /
                             (t - t0));
    cpu_hz_fault = 0;
}

void tiku_cpu_boot_esp32c61_init(void) {
    mwdt_off(ESP32C61_TIMG0_BASE, TIKU_ESP32C61_WDT_MWDT0);
    mwdt_off(ESP32C61_TIMG1_BASE, TIKU_ESP32C61_WDT_MWDT1);
    lp_wdt_off();
    /* The LP fast clock, which clocks the PMU, from RC_FAST.  Its reset
     * source is the crystal, which stops in sleep and would stop the PMU
     * that ends the sleep. */
    TIKU_REG32(ESP32C61_LP_CLK_CONF) =
        (TIKU_REG32(ESP32C61_LP_CLK_CONF) & ~ESP32C61_LP_FAST_SEL_MSK) |
        ESP32C61_LP_FAST_RC_FAST;
    /* Every bus master reaches all memory; the core's own fences are the
     * PMP and PMA entries. */
    TIKU_REG32(ESP32C61_HP_APM_FUNC_CTRL) = 0UL;
    TIKU_REG32(ESP32C61_LP_APM_FUNC_CTRL) = 0UL;
    TIKU_REG32(ESP32C61_CPU_APM_FUNC_CTRL) = 0UL;
#ifndef TIKU_MINIMAL
    tiku_esp32c61_sleep_boot();
#endif
    tiku_esp32c61_irq_init();
    measure_cpu_hz();
}

void tiku_cpu_boot_esp32c61_power_wfi_enter(void) {
    /* Entered with MIE off by the scheduler: a pending enabled line still
     * ends the wfi, and is taken once the caller's atomic section closes. */
    __asm__ volatile ("wfi" ::: "memory");
}

unsigned long tiku_cpu_esp32c61_clock_get_hz(void) {
    return cpu_hz;
}

unsigned long tiku_cpu_esp32c61_smclk_get_hz(void) {
    /* The core rate capped at 40 MHz, without reading the PCR's APB
     * divider; this matches the tree while that divider is 1. */
    return cpu_hz < 40000000UL ? cpu_hz : 40000000UL;
}

int tiku_cpu_esp32c61_clock_has_fault(void) {
    return cpu_hz_fault;
}

uint8_t tiku_cpu_esp32c61_wdt_found(void) {
    return wdt_found;
}

/*---------------------------------------------------------------------------*/
/* CORE FREQUENCY                                                            */
/*---------------------------------------------------------------------------*/

void tiku_cpu_esp32c61_clock_probe(tiku_esp32c61_clock_t *out) {
    static const unsigned long root_hz[4] = {
        40000000UL, 17500000UL, 160000000UL, 0UL};
    uint32_t sys = TIKU_REG32(ESP32C61_PCR_SYSCLK_CONF);
    uint32_t apb = TIKU_REG32(ESP32C61_PCR_APB_FREQ_CONF);

    out->root = (uint8_t)((sys & ESP32C61_PCR_SOC_CLK_MSK) >>
                          ESP32C61_PCR_SOC_CLK_POS);
    out->cpu_div = (uint8_t)((TIKU_REG32(ESP32C61_PCR_CPU_FREQ_CONF) &
                              ESP32C61_PCR_DIV_MSK) + 1UL);
    out->ahb_div = (uint8_t)((TIKU_REG32(ESP32C61_PCR_AHB_FREQ_CONF) &
                              ESP32C61_PCR_DIV_MSK) + 1UL);
    out->apb_div = (uint8_t)(((apb >> ESP32C61_PCR_APB_DIV_POS) &
                              ESP32C61_PCR_DIV_MSK) + 1UL);
    out->cpu_hz = root_hz[out->root] / out->cpu_div;
    out->ahb_hz = root_hz[out->root] / out->ahb_div;
    out->apb_hz = out->ahb_hz / out->apb_div;
}

int tiku_cpu_freq_esp32c61_supported(unsigned int mhz) {
    return mhz == 160U || mhz == 80U || mhz == 40U || mhz == 20U ||
           mhz == 10U;
}

/** @brief Write the root and both dividers, then latch them together. */
static void clock_tree_set(uint32_t root, uint32_t cpu_div, uint32_t ahb_div) {
    uint32_t sys = TIKU_REG32(ESP32C61_PCR_SYSCLK_CONF);

    TIKU_REG32(ESP32C61_PCR_CPU_FREQ_CONF) =
        (TIKU_REG32(ESP32C61_PCR_CPU_FREQ_CONF) & ~ESP32C61_PCR_DIV_MSK) |
        (cpu_div - 1UL);
    TIKU_REG32(ESP32C61_PCR_AHB_FREQ_CONF) =
        (TIKU_REG32(ESP32C61_PCR_AHB_FREQ_CONF) & ~ESP32C61_PCR_DIV_MSK) |
        (ahb_div - 1UL);
    TIKU_REG32(ESP32C61_PCR_SYSCLK_CONF) =
        (sys & ~ESP32C61_PCR_SOC_CLK_MSK) | (root << ESP32C61_PCR_SOC_CLK_POS);
    TIKU_REG32(ESP32C61_PCR_BUS_CLK_UPDATE) = ESP32C61_PCR_BUS_UPDATE;
    for (unsigned spins = 100000U; spins > 0U; spins--) {
        if ((TIKU_REG32(ESP32C61_PCR_BUS_CLK_UPDATE) &
             ESP32C61_PCR_BUS_UPDATE) == 0UL) {
            break;
        }
    }
}

uint32_t tiku_cpu_esp32c61_clock_park(void) {
    tiku_esp32c61_clock_t now;

    tiku_cpu_esp32c61_clock_probe(&now);
    clock_tree_set(ESP32C61_PCR_SOC_CLK_XTAL, 1UL, 1UL);
    ESP32C61_ROM_SET_CPU_MHZ(40U);
    return ((uint32_t)now.root << 16) | ((uint32_t)now.cpu_div << 8) |
           now.ahb_div;
}

void tiku_cpu_esp32c61_clock_unpark(uint32_t saved) {
    tiku_esp32c61_clock_t now;

    clock_tree_set((saved >> 16) & 0xFFUL, (saved >> 8) & 0xFFUL,
                   saved & 0xFFUL);
    tiku_cpu_esp32c61_clock_probe(&now);
    ESP32C61_ROM_SET_CPU_MHZ((uint32_t)(now.cpu_hz / 1000000UL));
}

int tiku_cpu_freq_esp32c61_set(unsigned int mhz) {
    if (!tiku_cpu_freq_esp32c61_supported(mhz)) {
        return -1;
    }
    if (mhz >= 80U) {
        /* AHB stays at 40 MHz: its rate must divide the core rate evenly. */
        clock_tree_set(ESP32C61_PCR_SOC_CLK_PLL160, 160U / mhz, 4UL);
    } else {
        clock_tree_set(ESP32C61_PCR_SOC_CLK_XTAL, 40U / mhz, 40U / mhz);
    }
    ESP32C61_ROM_SET_CPU_MHZ(mhz);
    measure_cpu_hz();
    return 0;
}

