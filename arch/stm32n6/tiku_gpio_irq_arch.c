/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - STM32N6 EXTI edge-interrupt backend.
 *
 * One EXTI line per pin NUMBER, shared across ports: line 13 can serve PA13 or
 * PC13 but never both, which is the choice EXTICR records.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <hal/tiku_gpio_irq_hal.h>
#include <interfaces/gpio/tiku_gpio.h>
#include <kernel/process/tiku_process.h>

#include "tiku_gpio_arch.h"
#include "tiku_gpio_irq_arch.h"
#include "tiku_stm32n6_regs.h"

/** @brief Port armed on each line, so the ISR reports what the caller asked. */
static uint8_t exti_port_for_line[16];

/** @brief Bitmap of armed lines, so a disable can refuse another port's line. */
static uint16_t exti_armed;

/** @brief Per-line delivery count; the only proof a handler actually ran. */
static volatile uint32_t exti_hits[16];

uint32_t tiku_stm32n6_exti_hits(uint8_t line) {
    return (line < 16U) ? exti_hits[line] : 0UL;
}

int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge) {
    if (pin > 15U || port > 15U) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    if (edge != TIKU_GPIO_EDGE_RISING && edge != TIKU_GPIO_EDGE_FALLING &&
        edge != TIKU_GPIO_EDGE_BOTH) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }

    tiku_stm32n6_gpio_clock_enable(port);
    TIKU_REG32(STM32N6_RCC_APB4HENR) |= STM32N6_RCC_APB4HENR_SYSCFGEN;
    (void)TIKU_REG32(STM32N6_RCC_APB4HENR);

    /* The line watches the pad, so the pin has to be an input; whatever pull
     * the board fits decides the idle level, so none is forced here. */
    uint32_t moder = TIKU_REG32(STM32N6_GPIO_MODER(port));
    moder &= ~(3UL << (pin * 2U));
    TIKU_REG32(STM32N6_GPIO_MODER(port)) = moder;

    /* Four lines per EXTICR word, one byte each. */
    uint32_t cr_idx  = (uint32_t)pin / 4U;
    uint32_t cr_shft = ((uint32_t)pin % 4U) * 8U;
    uint32_t cr = TIKU_REG32(STM32N6_EXTI_EXTICR(cr_idx));
    cr &= ~(0xFFUL << cr_shft);
    cr |= ((uint32_t)port << cr_shft);
    TIKU_REG32(STM32N6_EXTI_EXTICR(cr_idx)) = cr;

    uint32_t bit = 1UL << pin;

    /* The image runs secure with no non-secure world at all, so a line left
     * non-secure would target a vector table that does not exist. */
    TIKU_REG32(STM32N6_EXTI_SECCFGR1)  |= bit;
    TIKU_REG32(STM32N6_EXTI_PRIVCFGR1) |= bit;

    if (edge == TIKU_GPIO_EDGE_RISING || edge == TIKU_GPIO_EDGE_BOTH) {
        TIKU_REG32(STM32N6_EXTI_RTSR1) |= bit;
    } else {
        TIKU_REG32(STM32N6_EXTI_RTSR1) &= ~bit;
    }
    if (edge == TIKU_GPIO_EDGE_FALLING || edge == TIKU_GPIO_EDGE_BOTH) {
        TIKU_REG32(STM32N6_EXTI_FTSR1) |= bit;
    } else {
        TIKU_REG32(STM32N6_EXTI_FTSR1) &= ~bit;
    }

    /* Drop whatever the pad did while it was being configured, so arming does
     * not deliver an edge nobody caused. */
    TIKU_REG32(STM32N6_EXTI_RPR1) = bit;
    TIKU_REG32(STM32N6_EXTI_FPR1) = bit;

    exti_port_for_line[pin] = port;
    exti_armed |= (uint16_t)bit;

    TIKU_REG32(STM32N6_EXTI_IMR1) |= bit;

    uint32_t irq = (uint32_t)STM32N6_IRQ_EXTI0 + pin;

    /* Claim the interrupt for the secure state before enabling it: left
     * targeting non-secure, it is delivered to a vector table that does not
     * exist and the line just pends forever. */
    TIKU_REG32(STM32N6_NVIC_ITNS(irq / 32U)) &= ~(1UL << (irq % 32U));
    TIKU_REG32(STM32N6_NVIC_ICPR(irq / 32U)) = (1UL << (irq % 32U));
    TIKU_REG32(STM32N6_NVIC_ISER(irq / 32U)) = (1UL << (irq % 32U));
    return TIKU_GPIO_IRQ_OK;
}

int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    if (pin > 15U) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    uint32_t bit = 1UL << pin;

    /* The line is shared between ports, so disarming one the caller does not
     * own would silently break whoever does. */
    if ((exti_armed & bit) != 0U && exti_port_for_line[pin] != port) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }

    TIKU_REG32(STM32N6_EXTI_IMR1)  &= ~bit;
    TIKU_REG32(STM32N6_EXTI_RTSR1) &= ~bit;
    TIKU_REG32(STM32N6_EXTI_FTSR1) &= ~bit;
    TIKU_REG32(STM32N6_EXTI_RPR1)   = bit;
    TIKU_REG32(STM32N6_EXTI_FPR1)   = bit;
    exti_armed &= (uint16_t)~bit;
    return TIKU_GPIO_IRQ_OK;
}

/**
 * @brief Clear the latched edge and broadcast the pin event.
 *
 * @param line  EXTI line that fired, which is also the pin number
 */
static void exti_dispatch(uint8_t line) {
    uint32_t bit = 1UL << line;

    /* Cleared before the post: an edge arriving during the post must leave the
     * flag set so the line is serviced again rather than swallowed. */
    TIKU_REG32(STM32N6_EXTI_RPR1) = bit;
    TIKU_REG32(STM32N6_EXTI_FPR1) = bit;

    exti_hits[line]++;
    tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_EVENT_GPIO,
                      (tiku_event_data_t)
                      TIKU_GPIO_IRQ_PACK(exti_port_for_line[line], line));
}

/* One vector per line on this part, so each handler knows its own line. */
#define EXTI_ISR(n)                                                           \
    void tiku_stm32n6_exti##n##_isr(void) { exti_dispatch(n); }

EXTI_ISR(0)  EXTI_ISR(1)  EXTI_ISR(2)  EXTI_ISR(3)
EXTI_ISR(4)  EXTI_ISR(5)  EXTI_ISR(6)  EXTI_ISR(7)
EXTI_ISR(8)  EXTI_ISR(9)  EXTI_ISR(10) EXTI_ISR(11)
EXTI_ISR(12) EXTI_ISR(13) EXTI_ISR(14) EXTI_ISR(15)
