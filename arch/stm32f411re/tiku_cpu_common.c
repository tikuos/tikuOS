/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_cpu_common.c - STM32F411RE common helpers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_stm32f411_regs.h"
#include "tiku.h"
#include <stddef.h>
#include <stdint.h>

static void stm32f411_delay_cycles(unsigned long cycles)
{
    while (cycles-- > 0UL) {
        __asm__ volatile ("nop");
    }
}

void tiku_cpu_stm32f411_delay_us(unsigned int us)
{
    unsigned long hclk = tiku_cpu_stm32f411_clock_get_hz();
    unsigned long loops;

    if (hclk == 0UL) {
        hclk = TIKU_MAIN_CPU_HZ;
    }

    loops = (hclk / 6000000UL) * (unsigned long)us;
    if (loops == 0UL) {
        loops = (unsigned long)us;
    }
    stm32f411_delay_cycles(loops);
}

void tiku_cpu_stm32f411_delay_ms(unsigned int ms)
{
    while (ms-- > 0U) {
        tiku_cpu_stm32f411_delay_us(1000U);
    }
}

uint8_t tiku_cpu_stm32f411_unique_id(uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t n;
    const volatile uint8_t *uid = (const volatile uint8_t *)(uintptr_t)STM32F411_UID_BASE;

    if (buf == NULL || len == 0U) {
        return 0U;
    }

    n = (len > 12U) ? 12U : len;
    for (i = 0U; i < n; i++) {
        buf[i] = uid[i];
    }
    return n;
}

uint16_t tiku_cpu_stm32f411_reset_reason(void)
{
    return (uint16_t)((_STM32F411_REG(STM32F411_RCC_CSR) >> 16) & 0xFFFFU);
}
