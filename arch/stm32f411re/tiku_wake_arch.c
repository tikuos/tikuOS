/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_wake_arch.c - STM32F411RE backend for the wake-source HAL
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_wake_hal.h>
#include "tiku_stm32f411_regs.h"
#include <stdint.h>
#include <string.h>

static int stm32f411_irq_enabled(uint32_t irq)
{
    uint32_t reg = STM32F411_NVIC_ISER0 + ((irq / 32U) * 4U);
    return (_STM32F411_REG(reg) & STM32F411_BIT(irq & 31U)) ? 1 : 0;
}

void tiku_wake_arch_query(tiku_wake_sources_t *out)
{
    uint32_t imr;
    uint8_t line;

    if (out == 0) {
        return;
    }

    memset(out, 0, sizeof(*out));

    if (_STM32F411_REG(STM32F411_SYST_CSR) & STM32F411_SYST_CSR_TICKINT) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }
    if (stm32f411_irq_enabled(STM32F411_IRQ_TIM5)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }
    if (stm32f411_irq_enabled(STM32F411_IRQ_USART2)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }

    imr = _STM32F411_REG(STM32F411_EXTI_IMR);
    if ((imr & 0xFFFFU) != 0U &&
        (stm32f411_irq_enabled(STM32F411_IRQ_EXTI0)
      || stm32f411_irq_enabled(STM32F411_IRQ_EXTI1)
      || stm32f411_irq_enabled(STM32F411_IRQ_EXTI2)
      || stm32f411_irq_enabled(STM32F411_IRQ_EXTI3)
      || stm32f411_irq_enabled(STM32F411_IRQ_EXTI4)
      || stm32f411_irq_enabled(STM32F411_IRQ_EXTI9_5)
      || stm32f411_irq_enabled(STM32F411_IRQ_EXTI15_10))) {
        out->sources |= TIKU_WAKE_GPIO;
    }

    for (line = 0U; line < 8U; line++) {
        uint32_t exticr = _STM32F411_REG(STM32F411_SYSCFG_EXTICR(line / 4U));
        uint8_t port = (uint8_t)((exticr >> ((line & 3U) * 4U)) & 0x0FU);
        if ((imr & STM32F411_EXTI_LINE(line)) == 0U) {
            continue;
        }
        if (port < TIKU_WAKE_MAX_GPIO_PORTS) {
            out->gpio_ie[port] |= (uint8_t)(1U << line);
        }
    }
}
