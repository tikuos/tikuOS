/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - STM32N6 GPIO port driver.
 *
 * Direction, level, toggle and alternate-function select over the standard
 * STM32 GPIO block. Writes go through BSRR so no read-modify-write can race.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "tiku_stm32n6_regs.h"

/* Ports A..Q. AHB4ENR carries one enable bit per port at the port index. */
#define STM32N6_GPIO_PORT_MAX   16U
#define STM32N6_GPIO_PIN_MAX    15U

static uint8_t gpio_valid(uint8_t port, uint8_t pin) {
    return (port <= STM32N6_GPIO_PORT_MAX && pin <= STM32N6_GPIO_PIN_MAX);
}

/* Set the two-bit field for one pin in a MODER/OSPEEDR/PUPDR-shaped register. */
static void gpio_field2(uint32_t reg, uint8_t pin, uint32_t value) {
    uint32_t shift = (uint32_t)pin * 2U;
    uint32_t v = TIKU_REG32(reg);
    v &= ~(3UL << shift);
    v |= (value & 3UL) << shift;
    TIKU_REG32(reg) = v;
}

void tiku_stm32n6_gpio_clock_enable(uint8_t port) {
    if (port > STM32N6_GPIO_PORT_MAX) {
        return;
    }
    TIKU_REG32(STM32N6_RCC_AHB4ENR) |= (1UL << port);
    /* Read back so the clock is running before the first port access. */
    (void)TIKU_REG32(STM32N6_RCC_AHB4ENR);
}

void tiku_stm32n6_gpio_init_output(uint8_t port, uint8_t pin) {
    if (!gpio_valid(port, pin)) {
        return;
    }
    tiku_stm32n6_gpio_clock_enable(port);
    TIKU_REG32(STM32N6_GPIO_BSRR(port)) = (1UL << (pin + 16U));   /* start low */
    TIKU_REG32(STM32N6_GPIO_OTYPER(port)) &= ~(1UL << pin);       /* push-pull */
    gpio_field2(STM32N6_GPIO_PUPDR(port), pin, 0UL);              /* no pull */
    gpio_field2(STM32N6_GPIO_MODER(port), pin, STM32N6_GPIO_MODE_OUTPUT);
}

void tiku_stm32n6_gpio_init_alt(uint8_t port, uint8_t pin, uint8_t af) {
    if (!gpio_valid(port, pin)) {
        return;
    }
    tiku_stm32n6_gpio_clock_enable(port);

    /* AFRL holds pins 0-7, AFRH pins 8-15, four bits each. */
    uint32_t reg   = (pin < 8U) ? STM32N6_GPIO_AFRL(port) : STM32N6_GPIO_AFRH(port);
    uint32_t shift = ((uint32_t)pin & 7U) * 4U;
    uint32_t v = TIKU_REG32(reg);
    v &= ~(0xFUL << shift);
    v |= ((uint32_t)af & 0xFUL) << shift;
    TIKU_REG32(reg) = v;

    TIKU_REG32(STM32N6_GPIO_OTYPER(port)) &= ~(1UL << pin);
    gpio_field2(STM32N6_GPIO_OSPEEDR(port), pin, 2UL);            /* high speed */
    gpio_field2(STM32N6_GPIO_MODER(port), pin, STM32N6_GPIO_MODE_ALT);
}

void tiku_stm32n6_gpio_set(uint8_t port, uint8_t pin, uint8_t value) {
    if (!gpio_valid(port, pin)) {
        return;
    }
    TIKU_REG32(STM32N6_GPIO_BSRR(port)) =
        value ? (1UL << pin) : (1UL << (pin + 16U));
}

void tiku_stm32n6_gpio_toggle(uint8_t port, uint8_t pin) {
    if (!gpio_valid(port, pin)) {
        return;
    }
    uint32_t on = TIKU_REG32(STM32N6_GPIO_ODR(port)) & (1UL << pin);
    TIKU_REG32(STM32N6_GPIO_BSRR(port)) =
        on ? (1UL << (pin + 16U)) : (1UL << pin);
}

/*---------------------------------------------------------------------------*/
/* Kernel-facing contract                                                    */
/*---------------------------------------------------------------------------*/

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin) {
    if (!gpio_valid(port, pin)) {
        return -1;
    }
    tiku_stm32n6_gpio_init_output(port, pin);
    return 0;
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin) {
    if (!gpio_valid(port, pin)) {
        return -1;
    }
    tiku_stm32n6_gpio_clock_enable(port);
    gpio_field2(STM32N6_GPIO_MODER(port), pin, STM32N6_GPIO_MODE_INPUT);
    return 0;
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    if (!gpio_valid(port, pin)) {
        return -1;
    }
    tiku_stm32n6_gpio_set(port, pin, val);
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin) {
    if (!gpio_valid(port, pin)) {
        return -1;
    }
    tiku_stm32n6_gpio_toggle(port, pin);
    return 0;
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin) {
    if (!gpio_valid(port, pin)) {
        return -1;
    }
    return (TIKU_REG32(STM32N6_GPIO_IDR(port)) & (1UL << pin)) ? 1 : 0;
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    if (!gpio_valid(port, pin)) {
        return -1;
    }
    uint32_t mode = (TIKU_REG32(STM32N6_GPIO_MODER(port)) >> ((uint32_t)pin * 2U)) & 3UL;
    return (mode == STM32N6_GPIO_MODE_OUTPUT) ? 1 : 0;
}
