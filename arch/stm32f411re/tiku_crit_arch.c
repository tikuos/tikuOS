/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_crit_arch.c - STM32F411RE IRQ-mask backend for tiku_crit
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include "tiku_stm32f411_regs.h"
#include <stdint.h>

struct stm32f411_crit_state {
    uint32_t iser0_saved;
    uint32_t iser1_saved;
    uint32_t iser2_saved;
    uint32_t syst_csr_saved;
};

static struct stm32f411_crit_state crit_state;

static void stm32f411_irq_keep(uint32_t irq, uint32_t *w0,
                               uint32_t *w1, uint32_t *w2)
{
    if (irq < 32U) {
        *w0 |= STM32F411_BIT(irq);
    } else if (irq < 64U) {
        *w1 |= STM32F411_BIT(irq - 32U);
    } else if (irq < 96U) {
        *w2 |= STM32F411_BIT(irq - 64U);
    }
}

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask)
{
    uint32_t keep0 = 0U;
    uint32_t keep1 = 0U;
    uint32_t keep2 = 0U;
    uint32_t mask;

    crit_state.iser0_saved = _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x0U);
    crit_state.iser1_saved = _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x4U);
    crit_state.iser2_saved = _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x8U);
    crit_state.syst_csr_saved = _STM32F411_REG(STM32F411_SYST_CSR);

    if (preserve_mask & TIKU_CRIT_PRESERVE_HTIMER) {
        stm32f411_irq_keep(STM32F411_IRQ_TIM5, &keep0, &keep1, &keep2);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_UART) {
        stm32f411_irq_keep(STM32F411_IRQ_USART2, &keep0, &keep1, &keep2);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_GPIO) {
        stm32f411_irq_keep(STM32F411_IRQ_EXTI0, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_EXTI1, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_EXTI2, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_EXTI3, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_EXTI4, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_EXTI9_5, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_EXTI15_10, &keep0, &keep1, &keep2);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_ADC) {
        stm32f411_irq_keep(STM32F411_IRQ_ADC, &keep0, &keep1, &keep2);
    }
    if (preserve_mask & TIKU_CRIT_PRESERVE_I2C) {
        stm32f411_irq_keep(STM32F411_IRQ_I2C1_EV, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_I2C1_ER, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_I2C2_EV, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_I2C2_ER, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_I2C3_EV, &keep0, &keep1, &keep2);
        stm32f411_irq_keep(STM32F411_IRQ_I2C3_ER, &keep0, &keep1, &keep2);
    }

    if ((preserve_mask & TIKU_CRIT_PRESERVE_TICK) == 0U) {
        _STM32F411_REG(STM32F411_SYST_CSR) &=
            ~STM32F411_SYST_CSR_TICKINT;
    }

    mask = crit_state.iser0_saved & ~keep0;
    if (mask != 0U) {
        _STM32F411_REG(STM32F411_NVIC_ICER0 + 0x0U) = mask;
    }
    mask = crit_state.iser1_saved & ~keep1;
    if (mask != 0U) {
        _STM32F411_REG(STM32F411_NVIC_ICER0 + 0x4U) = mask;
    }
    mask = crit_state.iser2_saved & ~keep2;
    if (mask != 0U) {
        _STM32F411_REG(STM32F411_NVIC_ICER0 + 0x8U) = mask;
    }

    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}

void tiku_crit_arch_unmask_irqs(void)
{
    _STM32F411_REG(STM32F411_SYST_CSR) =
        crit_state.syst_csr_saved;
    _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x0U) = crit_state.iser0_saved;
    _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x4U) = crit_state.iser1_saved;
    _STM32F411_REG(STM32F411_NVIC_ISER0 + 0x8U) = crit_state.iser2_saved;

    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}
