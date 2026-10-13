/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_boot_arch.c - P4 watchdog handoff using peripheral registers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_boot_arch.h"
#include "tiku_esp32p4_regs.h"

void tiku_esp32p4_boot_park_cores(void)
{
    uint32_t value = TIKU_P4_REG_READ(TIKU_P4_HP_RESET0);
    TIKU_P4_REG_WRITE(TIKU_P4_HP_RESET0, value | (1u << 8));
    value = TIKU_P4_REG_READ(TIKU_P4_SOC_CLK0);
    TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK0, value & ~((1u << 4) | (1u << 1)));
    value = TIKU_P4_REG_READ(TIKU_P4_LP_RESET);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_RESET, value | (1u << 31));
    value = TIKU_P4_REG_READ(TIKU_P4_LP_CLK);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_CLK, value & ~(1u << 31));
}

/** @brief Unlock, clear the watchdog enable bits, and restore write protection.
 */
static void disable(uintptr_t config, uintptr_t protect, uint32_t mask)
{
    TIKU_P4_REG_WRITE(protect, TIKU_P4_WDT_KEY);
    TIKU_P4_REG_WRITE(config, TIKU_P4_REG_READ(config) & ~mask);
    TIKU_P4_REG_WRITE(protect, 0u);
}

void tiku_esp32p4_boot_watchdogs_disable(void)
{
    disable(TIKU_P4_TG0_WDT_CONFIG, TIKU_P4_TG0_WDT_PROTECT,
            TIKU_P4_WDT_ENABLE | TIKU_P4_TG_WDT_FLASHBOOT);
    disable(TIKU_P4_TG1_WDT_CONFIG, TIKU_P4_TG1_WDT_PROTECT,
            TIKU_P4_WDT_ENABLE | TIKU_P4_TG_WDT_FLASHBOOT);
    disable(TIKU_P4_LP_WDT_CONFIG, TIKU_P4_LP_WDT_PROTECT,
            TIKU_P4_WDT_ENABLE | TIKU_P4_LP_WDT_FLASHBOOT);
    TIKU_P4_REG_WRITE(TIKU_P4_SWD_PROTECT, TIKU_P4_WDT_KEY);
    TIKU_P4_REG_WRITE(TIKU_P4_SWD_CONFIG, TIKU_P4_REG_READ(TIKU_P4_SWD_CONFIG) |
                                              TIKU_P4_SWD_AUTO_FEED);
    TIKU_P4_REG_WRITE(TIKU_P4_SWD_PROTECT, 0u);
}
