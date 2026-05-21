/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_pinmux_arch.c - STM32F411RE pinmux and pad helpers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pinmux_arch.h"
#include "tiku_stm32f411_regs.h"
#include <stdint.h>

int
tiku_stm32f411_pinmux_resolve(uint8_t port, uint8_t pin,
                              uint32_t *gpio_base, uint32_t *rcc_bit)
{
    if (pin > 15U || gpio_base == (uint32_t *)0 || rcc_bit == (uint32_t *)0) {
        return -1;
    }

    switch (port) {
    case 1U:
        *gpio_base = STM32F411_GPIOA_BASE;
        *rcc_bit   = STM32F411_RCC_AHB1_GPIOA;
        return 0;
    case 2U:
        *gpio_base = STM32F411_GPIOB_BASE;
        *rcc_bit   = STM32F411_RCC_AHB1_GPIOB;
        return 0;
    case 3U:
        *gpio_base = STM32F411_GPIOC_BASE;
        *rcc_bit   = STM32F411_RCC_AHB1_GPIOC;
        return 0;
    case 4U:
        *gpio_base = STM32F411_GPIOD_BASE;
        *rcc_bit   = STM32F411_RCC_AHB1_GPIOD;
        return 0;
    default:
        return -1;
    }
}

int
tiku_stm32f411_pinmux_config(uint8_t port, uint8_t pin,
                             uint32_t mode, uint32_t pupd,
                             uint32_t speed, uint8_t af)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint32_t moder;
    uint32_t otyper;
    uint32_t ospeedr;
    uint32_t pull;

    if (tiku_stm32f411_pinmux_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return -1;
    }

    stm32f411_rcc_enable_ahb1(rcc_bit);

    moder = _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base));
    moder &= ~STM32F411_GPIO_MODE(pin, 3U);
    moder |= STM32F411_GPIO_MODE(pin, mode);
    _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base)) = moder;

    otyper = _STM32F411_REG(STM32F411_GPIO_OTYPER(gpio_base));
    otyper &= ~STM32F411_GPIO_OTYPE_OD(pin);
    _STM32F411_REG(STM32F411_GPIO_OTYPER(gpio_base)) = otyper;

    ospeedr = _STM32F411_REG(STM32F411_GPIO_OSPEEDR(gpio_base));
    ospeedr &= ~STM32F411_GPIO_SPEED(pin, 3U);
    ospeedr |= STM32F411_GPIO_SPEED(pin, speed);
    _STM32F411_REG(STM32F411_GPIO_OSPEEDR(gpio_base)) = ospeedr;

    pull = _STM32F411_REG(STM32F411_GPIO_PUPDR(gpio_base));
    pull &= ~STM32F411_GPIO_PUPD(pin, 3U);
    pull |= STM32F411_GPIO_PUPD(pin, pupd);
    _STM32F411_REG(STM32F411_GPIO_PUPDR(gpio_base)) = pull;

    if (mode == STM32F411_GPIO_MODE_AF) {
        stm32f411_gpio_set_af(gpio_base, pin, af);
    }

    return 0;
}

int
tiku_stm32f411_pinmux_init_output(uint8_t port, uint8_t pin)
{
    return tiku_stm32f411_pinmux_config(port, pin,
                                        STM32F411_GPIO_MODE_OUTPUT,
                                        STM32F411_GPIO_PUPD_NONE,
                                        STM32F411_GPIO_SPEED_HIGH,
                                        0U);
}

int
tiku_stm32f411_pinmux_init_input(uint8_t port, uint8_t pin, uint32_t pupd)
{
    return tiku_stm32f411_pinmux_config(port, pin,
                                        STM32F411_GPIO_MODE_INPUT,
                                        pupd,
                                        STM32F411_GPIO_SPEED_HIGH,
                                        0U);
}
