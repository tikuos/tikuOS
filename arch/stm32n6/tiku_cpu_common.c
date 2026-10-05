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
    static uint16_t captured;
    static uint8_t  captured_valid;
    uint32_t rsr;

    if (captured_valid) {
        return captured;
    }
    rsr = TIKU_REG32(STM32N6_RCC_RSR);

    /* The flags stay set until RMVF clears them, so without this the next
     * boot would see every cause since power-on.  RMVF goes back to 0 in case
     * it holds the flags clear while it is set. */
    TIKU_REG32(STM32N6_RCC_RSR) = STM32N6_RCC_RSR_RMVF;
    TIKU_REG32(STM32N6_RCC_RSR) = 0UL;

    /* Most specific first.  PINRSTF comes last, since a reset from inside the
     * chip can drive NRST and raise it too, and a power-on raises BORRSTF and
     * PINRSTF along with PORRSTF. */
    if (rsr & (STM32N6_RCC_RSR_IWDGRSTF | STM32N6_RCC_RSR_WWDGRSTF)) {
        captured = 0x0016U;     /* wdt-timeout */
    } else if (rsr & (STM32N6_RCC_RSR_SFTRSTF | STM32N6_RCC_RSR_LCKRSTF |
                      STM32N6_RCC_RSR_LPWRRSTF)) {
        /* sw-bor: SYSRESETREQ, a CPU lockup after a fault, or an illegal
         * Stop or Standby entry */
        captured = 0x0006U;
    } else if (rsr & STM32N6_RCC_RSR_PORRSTF) {
        captured = 0x0000U;     /* none: power-on */
    } else if (rsr & STM32N6_RCC_RSR_BORRSTF) {
        captured = 0x0002U;     /* brownout */
    } else if (rsr & STM32N6_RCC_RSR_PINRSTF) {
        captured = 0x0004U;     /* rstnmi: the NRST pin */
    } else {
        captured = 0x0000U;
    }
    captured_valid = 1U;
    return captured;
}
