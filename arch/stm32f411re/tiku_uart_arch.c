/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_uart_arch.c - STM32F411RE polling USART2 console backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_stm32f411_regs.h"
#include "tiku.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static volatile uint16_t g_uart_overruns;

static void stm32f411_uart_gpio_init(void)
{
    uint32_t moder;
    uint32_t speed;
    uint32_t pupd;

    stm32f411_rcc_enable_ahb1(STM32F411_RCC_AHB1_GPIOA);

    moder = _STM32F411_REG(STM32F411_GPIO_MODER(STM32F411_GPIOA_BASE));
    moder &= ~(STM32F411_GPIO_MODE(TIKU_BOARD_UART_TX_PIN, 3U)
            |  STM32F411_GPIO_MODE(TIKU_BOARD_UART_RX_PIN, 3U));
    moder |=  STM32F411_GPIO_MODE(TIKU_BOARD_UART_TX_PIN, STM32F411_GPIO_MODE_AF)
           |  STM32F411_GPIO_MODE(TIKU_BOARD_UART_RX_PIN, STM32F411_GPIO_MODE_AF);
    _STM32F411_REG(STM32F411_GPIO_MODER(STM32F411_GPIOA_BASE)) = moder;

    speed = _STM32F411_REG(STM32F411_GPIO_OSPEEDR(STM32F411_GPIOA_BASE));
    speed |= STM32F411_GPIO_SPEED(TIKU_BOARD_UART_TX_PIN, STM32F411_GPIO_SPEED_HIGH)
          |  STM32F411_GPIO_SPEED(TIKU_BOARD_UART_RX_PIN, STM32F411_GPIO_SPEED_HIGH);
    _STM32F411_REG(STM32F411_GPIO_OSPEEDR(STM32F411_GPIOA_BASE)) = speed;

    pupd = _STM32F411_REG(STM32F411_GPIO_PUPDR(STM32F411_GPIOA_BASE));
    pupd &= ~(STM32F411_GPIO_PUPD(TIKU_BOARD_UART_TX_PIN, 3U)
           |  STM32F411_GPIO_PUPD(TIKU_BOARD_UART_RX_PIN, 3U));
    pupd |= STM32F411_GPIO_PUPD(TIKU_BOARD_UART_RX_PIN, STM32F411_GPIO_PUPD_UP);
    _STM32F411_REG(STM32F411_GPIO_PUPDR(STM32F411_GPIOA_BASE)) = pupd;

    stm32f411_gpio_set_af(STM32F411_GPIOA_BASE, TIKU_BOARD_UART_TX_PIN,
                          STM32F411_GPIO_AF_USART1_2);
    stm32f411_gpio_set_af(STM32F411_GPIOA_BASE, TIKU_BOARD_UART_RX_PIN,
                          STM32F411_GPIO_AF_USART1_2);
}

static uint32_t stm32f411_uart_brr(unsigned long pclk, unsigned long baud)
{
    if (baud == 0UL) {
        baud = 115200UL;
    }
    return (uint32_t)((pclk + (baud / 2UL)) / baud);
}

void tiku_uart_init(void)
{
    unsigned long pclk1;

    stm32f411_uart_gpio_init();
    stm32f411_rcc_enable_apb1(STM32F411_RCC_APB1_USART2);
    stm32f411_rcc_reset_apb1(STM32F411_RCC_APB1_USART2);

    pclk1 = tiku_cpu_stm32f411_pclk1_get_hz();
    if (pclk1 == 0UL) {
        pclk1 = 16000000UL;
    }

    _STM32F411_REG(STM32F411_USART_CR1(STM32F411_USART2_BASE)) = 0U;
    _STM32F411_REG(STM32F411_USART_CR2(STM32F411_USART2_BASE)) = STM32F411_USART_CR2_STOP_1;
    _STM32F411_REG(STM32F411_USART_CR3(STM32F411_USART2_BASE)) = 0U;
    _STM32F411_REG(STM32F411_USART_BRR(STM32F411_USART2_BASE)) =
        stm32f411_uart_brr(pclk1, (unsigned long)TIKU_BOARD_UART_BAUD);
    _STM32F411_REG(STM32F411_USART_CR1(STM32F411_USART2_BASE)) =
        STM32F411_USART_CR1_UE
        | STM32F411_USART_CR1_TE
        | STM32F411_USART_CR1_RE;

    g_uart_overruns = 0U;
}

void tiku_uart_putc(char c)
{
    while ((_STM32F411_REG(STM32F411_USART_SR(STM32F411_USART2_BASE))
           & STM32F411_USART_SR_TXE) == 0U) {
        /* spin */
    }
    _STM32F411_REG(STM32F411_USART_DR(STM32F411_USART2_BASE)) =
        (uint32_t)(uint8_t)c;
}

void tiku_uart_puts(const char *s)
{
    while (s != 0 && *s != '\0') {
        tiku_uart_putc(*s++);
    }
}

void tiku_uart_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) {
        return;
    }
    tiku_uart_puts(buf);
}

uint8_t tiku_uart_rx_ready(void)
{
    return (_STM32F411_REG(STM32F411_USART_SR(STM32F411_USART2_BASE))
            & STM32F411_USART_SR_RXNE) ? 1U : 0U;
}

int tiku_uart_getc(void)
{
    uint32_t sr = _STM32F411_REG(STM32F411_USART_SR(STM32F411_USART2_BASE));

    if (sr & STM32F411_USART_SR_ORE) {
        g_uart_overruns++;
        (void)_STM32F411_REG(STM32F411_USART_DR(STM32F411_USART2_BASE));
        return -1;
    }
    if ((sr & STM32F411_USART_SR_RXNE) == 0U) {
        return -1;
    }
    return (int)(_STM32F411_REG(STM32F411_USART_DR(STM32F411_USART2_BASE))
                 & STM32F411_USART_DR_MASK);
}

uint16_t tiku_uart_overrun_count(void)
{
    return g_uart_overruns;
}

void tiku_uart_overrun_reset(void)
{
    g_uart_overruns = 0U;
}
