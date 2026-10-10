/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_boot_arch.c - C5 watchdog handoff using peripheral registers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_boot_arch.h"
#include "tiku_esp32c5_regs.h"

/** @brief Unlock, clear the watchdog enable bits, and restore write protection.
 */
static void disable(uintptr_t config, uintptr_t protect, uint32_t mask)
{
    TIKU_C5_REG_WRITE(protect, TIKU_C5_WDT_KEY);
    TIKU_C5_REG_WRITE(config, TIKU_C5_REG_READ(config) & ~mask);
    TIKU_C5_REG_WRITE(protect, 0u);
}

void tiku_esp32c5_boot_watchdogs_disable(void)
{
    disable(TIKU_C5_TG0_WDT_CONFIG, TIKU_C5_TG0_WDT_PROTECT,
            TIKU_C5_WDT_ENABLE | TIKU_C5_TG_WDT_FLASHBOOT);
    disable(TIKU_C5_TG1_WDT_CONFIG, TIKU_C5_TG1_WDT_PROTECT,
            TIKU_C5_WDT_ENABLE | TIKU_C5_TG_WDT_FLASHBOOT);
    disable(TIKU_C5_LP_WDT_CONFIG, TIKU_C5_LP_WDT_PROTECT,
            TIKU_C5_WDT_ENABLE | TIKU_C5_LP_WDT_FLASHBOOT);
    TIKU_C5_REG_WRITE(TIKU_C5_SWD_PROTECT, TIKU_C5_WDT_KEY);
    TIKU_C5_REG_WRITE(TIKU_C5_SWD_CONFIG, TIKU_C5_REG_READ(TIKU_C5_SWD_CONFIG) |
                                              TIKU_C5_SWD_AUTO_FEED);
    TIKU_C5_REG_WRITE(TIKU_C5_SWD_PROTECT, 0u);
}
