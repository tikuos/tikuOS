/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_htimer_arch.c - STM32F411RE TIM5-based htimer backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_stm32f411_regs.h"
#include <kernel/timers/tiku_htimer.h>
#include <stdint.h>

static unsigned long stm32f411_tim5_clock_hz(void)
{
    unsigned long hclk = tiku_cpu_stm32f411_clock_get_hz();
    unsigned long pclk1 = tiku_cpu_stm32f411_pclk1_get_hz();

    if (pclk1 == 0UL) {
        return 16000000UL;
    }
    return (pclk1 == hclk) ? pclk1 : (pclk1 * 2UL);
}

void tiku_htimer_arch_init(void)
{
    unsigned long timclk = stm32f411_tim5_clock_hz();
    uint32_t psc;

    stm32f411_rcc_enable_apb1(STM32F411_RCC_APB1_TIM5);
    stm32f411_rcc_reset_apb1(STM32F411_RCC_APB1_TIM5);

    psc = (uint32_t)(timclk / TIKU_HTIMER_ARCH_SECOND);
    if (psc == 0U) {
        psc = 1U;
    }

    _STM32F411_REG(STM32F411_TIM_CR1(STM32F411_TIM5_BASE)) = 0U;
    _STM32F411_REG(STM32F411_TIM_PSC(STM32F411_TIM5_BASE)) = psc - 1U;
    _STM32F411_REG(STM32F411_TIM_ARR(STM32F411_TIM5_BASE)) = 0xFFFFFFFFUL;
    _STM32F411_REG(STM32F411_TIM_EGR(STM32F411_TIM5_BASE)) = STM32F411_TIM_EGR_UG;
    _STM32F411_REG(STM32F411_TIM_SR(STM32F411_TIM5_BASE)) = 0U;
    _STM32F411_REG(STM32F411_TIM_DIER(STM32F411_TIM5_BASE)) = 0U;
    _STM32F411_REG(STM32F411_TIM_CR1(STM32F411_TIM5_BASE)) = STM32F411_TIM_CR1_CEN;

    stm32f411_nvic_clear_pending(STM32F411_IRQ_TIM5);
    stm32f411_nvic_enable(STM32F411_IRQ_TIM5);
}

void tiku_htimer_arch_schedule(tiku_htimer_clock_t t)
{
    _STM32F411_REG(STM32F411_TIM_CCR1(STM32F411_TIM5_BASE)) = t;
    _STM32F411_REG(STM32F411_TIM_SR(STM32F411_TIM5_BASE)) &= ~STM32F411_TIM_SR_CC1IF;
    _STM32F411_REG(STM32F411_TIM_DIER(STM32F411_TIM5_BASE)) |= STM32F411_TIM_DIER_CC1IE;
}

tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    return (tiku_htimer_clock_t)_STM32F411_REG(STM32F411_TIM_CNT(STM32F411_TIM5_BASE));
}

void tiku_stm32f411_tim5_irq_handler(void)
{
    uint32_t sr = _STM32F411_REG(STM32F411_TIM_SR(STM32F411_TIM5_BASE));

    if (sr & STM32F411_TIM_SR_CC1IF) {
        _STM32F411_REG(STM32F411_TIM_SR(STM32F411_TIM5_BASE)) &= ~STM32F411_TIM_SR_CC1IF;
        tiku_htimer_run_next();
    }
}
