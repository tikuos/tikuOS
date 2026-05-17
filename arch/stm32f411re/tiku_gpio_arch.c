/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_gpio_arch.c - STM32F411RE GPIO backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "tiku_stm32f411_regs.h"
#include <stdint.h>

static int stm32f411_gpio_resolve(uint8_t port, uint8_t pin,
                                  uint32_t *gpio_base,
                                  uint32_t *rcc_bit)
{
    if (pin > 15U) {
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

/*---------------------------------------------------------------------------*/
/* Per-pin direct helpers                                                    */
/*---------------------------------------------------------------------------*/

static void stm32f411_gpio_mode_write(uint32_t gpio_base, uint8_t pin,
                                      uint32_t mode, uint32_t pupd)
{
    uint32_t moder = _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base));
    uint32_t otyper = _STM32F411_REG(STM32F411_GPIO_OTYPER(gpio_base));
    uint32_t speed = _STM32F411_REG(STM32F411_GPIO_OSPEEDR(gpio_base));
    uint32_t pull = _STM32F411_REG(STM32F411_GPIO_PUPDR(gpio_base));

    moder &= ~STM32F411_GPIO_MODE(pin, 3U);
    moder |= STM32F411_GPIO_MODE(pin, mode);
    _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base)) = moder;

    otyper &= ~STM32F411_GPIO_OTYPE_OD(pin);
    _STM32F411_REG(STM32F411_GPIO_OTYPER(gpio_base)) = otyper;

    speed &= ~STM32F411_GPIO_SPEED(pin, 3U);
    speed |= STM32F411_GPIO_SPEED(pin, STM32F411_GPIO_SPEED_HIGH);
    _STM32F411_REG(STM32F411_GPIO_OSPEEDR(gpio_base)) = speed;

    pull &= ~STM32F411_GPIO_PUPD(pin, 3U);
    pull |= STM32F411_GPIO_PUPD(pin, pupd);
    _STM32F411_REG(STM32F411_GPIO_PUPDR(gpio_base)) = pull;
}

/*---------------------------------------------------------------------------*/
/* HAL entry points                                                          */
/*---------------------------------------------------------------------------*/

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;

    if (stm32f411_gpio_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    stm32f411_gpio_mode_write(gpio_base, pin, STM32F411_GPIO_MODE_OUTPUT,
                              STM32F411_GPIO_PUPD_NONE);
    return 0;
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;

    if (stm32f411_gpio_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    stm32f411_gpio_mode_write(gpio_base, pin, STM32F411_GPIO_MODE_INPUT,
                              STM32F411_GPIO_PUPD_UP);
    return 0;
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;

    if (stm32f411_gpio_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    stm32f411_gpio_mode_write(gpio_base, pin, STM32F411_GPIO_MODE_OUTPUT,
                              STM32F411_GPIO_PUPD_NONE);
    _STM32F411_REG(STM32F411_GPIO_BSRR(gpio_base)) =
        val ? STM32F411_GPIO_BSRR_SET(pin) : STM32F411_GPIO_BSRR_CLR(pin);
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint32_t odr;

    if (stm32f411_gpio_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    stm32f411_gpio_mode_write(gpio_base, pin, STM32F411_GPIO_MODE_OUTPUT,
                              STM32F411_GPIO_PUPD_NONE);
    odr = _STM32F411_REG(STM32F411_GPIO_ODR(gpio_base));
    _STM32F411_REG(STM32F411_GPIO_BSRR(gpio_base)) =
        (odr & STM32F411_BIT(pin))
            ? STM32F411_GPIO_BSRR_CLR(pin)
            : STM32F411_GPIO_BSRR_SET(pin);
    return 0;
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;

    if (stm32f411_gpio_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    return (_STM32F411_REG(STM32F411_GPIO_IDR(gpio_base)) & STM32F411_BIT(pin))
        ? 1 : 0;
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    uint32_t gpio_base;
    uint32_t rcc_bit;
    uint32_t moder;

    if (stm32f411_gpio_resolve(port, pin, &gpio_base, &rcc_bit) != 0) {
        return -1;
    }
    stm32f411_rcc_enable_ahb1(rcc_bit);
    moder = _STM32F411_REG(STM32F411_GPIO_MODER(gpio_base));
    return (((moder >> (pin * 2U)) & 0x3U) == STM32F411_GPIO_MODE_OUTPUT)
        ? 1 : 0;
}

void tiku_stm32f411_gpio_init_output(uint8_t port, uint8_t pin)
{
    (void)tiku_gpio_arch_set_output(port, pin);
}

void tiku_stm32f411_gpio_set(uint8_t port, uint8_t pin, uint8_t value)
{
    (void)tiku_gpio_arch_write(port, pin, value);
}

void tiku_stm32f411_gpio_toggle(uint8_t port, uint8_t pin)
{
    (void)tiku_gpio_arch_toggle(port, pin);
}
