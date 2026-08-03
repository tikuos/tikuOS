/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - STM32N6 busy-wait delays.
 *
 * Scaled by the measured CPU rate once LPTIM1 is running, falling back to the
 * compile-time estimate before that -- the inherited clock varies per boot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_stm32n6_regs.h"

/**
 * @brief Spin for a given number of loop iterations.
 *
 * One subs/bne pair. The core retires it at about one cycle per iteration,
 * so the count is calibrated directly rather than converted from cycles.
 *
 * @param iters  Iterations to run; zero still costs one pass
 */
static void cpu_spin(unsigned long iters) {
    if (iters == 0UL) {
        iters = 1UL;
    }
    __asm__ volatile (
        "1: subs %0, %0, #1\n"
        "   bne  1b\n"
        : "+r" (iters)
        :
        : "cc");
}

/** @brief Spin iterations per millisecond, measured rather than derived. */
static unsigned long cpu_iters_per_ms(void) {
    return tiku_cpu_stm32n6_spin_per_ms();
}

void tiku_cpu_stm32n6_delay_us(unsigned int us) {
    unsigned long per_ms = cpu_iters_per_ms();
    /* Split so the multiply cannot overflow on long waits. */
    while (us >= 1000U) {
        cpu_spin(per_ms);
        us -= 1000U;
    }
    cpu_spin(((unsigned long)us * per_ms) / 1000UL);
}

void tiku_cpu_stm32n6_delay_ms(unsigned int ms) {
    unsigned long per_ms = cpu_iters_per_ms();
    while (ms-- > 0U) {
        cpu_spin(per_ms);
    }
}

uint8_t tiku_cpu_stm32n6_unique_id(uint8_t *buf, uint8_t len) {
    (void)buf;
    (void)len;
    return 0U;
}

uint16_t tiku_cpu_stm32n6_reset_reason(void) {
    uint32_t rsr = TIKU_REG32(STM32N6_RCC_RSR);
    uint16_t out = 0U;

    if (rsr & STM32N6_RCC_RSR_PINRSTF)  out |= TIKU_STM32N6_RESET_PIN;
    if (rsr & STM32N6_RCC_RSR_PORRSTF)  out |= TIKU_STM32N6_RESET_POWER;
    if (rsr & STM32N6_RCC_RSR_SFTRSTF)  out |= TIKU_STM32N6_RESET_SOFT;
    if (rsr & (STM32N6_RCC_RSR_IWDGRSTF | STM32N6_RCC_RSR_WWDGRSTF)) {
        out |= TIKU_STM32N6_RESET_WATCHDOG;
    }
    if (rsr & STM32N6_RCC_RSR_LPWRRSTF) out |= TIKU_STM32N6_RESET_LOWPOWER;
    return out;
}
