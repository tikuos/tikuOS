/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - STM32N6 boot-time clock state.
 *
 * Starts HSI if it is not already running and reports the rates the rest of
 * the port scales from. The PLL and bus dividers are left untouched.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
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

unsigned long tiku_cpu_stm32n6_clock_get_hz(void) {
    return TIKU_STM32N6_CPU_HZ;
}

unsigned long tiku_cpu_stm32n6_smclk_get_hz(void) {
    uint32_t div = (TIKU_REG32(STM32N6_RCC_HSICFGR) & STM32N6_RCC_HSICFGR_DIV_MSK)
                   >> STM32N6_RCC_HSICFGR_DIV_POS;
    return STM32N6_HSI_HZ >> div;
}

int tiku_cpu_stm32n6_clock_has_fault(void) {
    return ((TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_HSIRDY) == 0UL) ? 1 : 0;
}
