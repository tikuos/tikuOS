/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_stm32n6_regs.h - STM32N657 register bases and the bits the port uses.
 *
 * Addresses come from the STM32N657 SVD shipped with STM32CubeProgrammer.
 * Only registers this port touches appear here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_REGS_H_
#define TIKU_STM32N6_REGS_H_

#include <stdint.h>

#define TIKU_REG32(a)               (*(volatile uint32_t *)(uintptr_t)(a))

/* Reset and clock control. */
#define STM32N6_RCC_BASE            0x46028000UL
#define STM32N6_RCC_CR              (STM32N6_RCC_BASE + 0x000U)
#define STM32N6_RCC_SR              (STM32N6_RCC_BASE + 0x004U)
#define STM32N6_RCC_HSICFGR         (STM32N6_RCC_BASE + 0x048U)
#define STM32N6_RCC_CCIPR13         (STM32N6_RCC_BASE + 0x174U)
#define STM32N6_RCC_AHB4ENR         (STM32N6_RCC_BASE + 0x25CU)
#define STM32N6_RCC_APB2ENR         (STM32N6_RCC_BASE + 0x26CU)

#define STM32N6_RCC_CR_HSION        (1UL << 3)
#define STM32N6_RCC_SR_HSIRDY       (1UL << 3)
#define STM32N6_RCC_HSICFGR_DIV_POS 7U      /* 2 bits: HSI / (1 << HSIDIV) */
#define STM32N6_RCC_HSICFGR_DIV_MSK (3UL << STM32N6_RCC_HSICFGR_DIV_POS)
#define STM32N6_RCC_AHB4ENR_GPIOE   (1UL << 4)
#define STM32N6_RCC_AHB4ENR_GPIOG   (1UL << 6)
#define STM32N6_RCC_APB2ENR_USART1  (1UL << 4)

/* USART1 kernel clock select, CCIPR13[2:0].  6 selects HSI; the SVD carries no
 * names for these, so the value is taken from LL_RCC_USART1_CLKSOURCE_HSI. */
#define STM32N6_CCIPR13_USART1SEL_MSK (7UL << 0)
#define STM32N6_CCIPR13_USART1SEL_HSI (6UL << 0)

/* HSI runs at 64 MHz before HSIDIV. */
#define STM32N6_HSI_HZ              64000000UL

/* GPIO ports are 0x400 apart from port A; E is 4 and G is 6. */
#define STM32N6_GPIO_BASE(port)     (0x46020000UL + ((uint32_t)(port) * 0x400UL))
#define STM32N6_GPIO_PORT_E         4U
#define STM32N6_GPIO_PORT_G         6U

#define STM32N6_GPIO_MODER(p)       (STM32N6_GPIO_BASE(p) + 0x00U)
#define STM32N6_GPIO_OTYPER(p)      (STM32N6_GPIO_BASE(p) + 0x04U)
#define STM32N6_GPIO_OSPEEDR(p)     (STM32N6_GPIO_BASE(p) + 0x08U)
#define STM32N6_GPIO_PUPDR(p)       (STM32N6_GPIO_BASE(p) + 0x0CU)
#define STM32N6_GPIO_IDR(p)         (STM32N6_GPIO_BASE(p) + 0x10U)
#define STM32N6_GPIO_ODR(p)         (STM32N6_GPIO_BASE(p) + 0x14U)
#define STM32N6_GPIO_BSRR(p)        (STM32N6_GPIO_BASE(p) + 0x18U)
#define STM32N6_GPIO_AFRL(p)        (STM32N6_GPIO_BASE(p) + 0x20U)
#define STM32N6_GPIO_AFRH(p)        (STM32N6_GPIO_BASE(p) + 0x24U)

#define STM32N6_GPIO_MODE_INPUT     0UL
#define STM32N6_GPIO_MODE_OUTPUT    1UL
#define STM32N6_GPIO_MODE_ALT       2UL

/* USART1 is the ST-LINK virtual COM port: TX PE5, RX PE6, both AF7. */
#define STM32N6_USART1_BASE         0x42001000UL
#define STM32N6_USART_CR1(b)        ((b) + 0x00U)
#define STM32N6_USART_CR2(b)        ((b) + 0x04U)
#define STM32N6_USART_CR3(b)        ((b) + 0x08U)
#define STM32N6_USART_BRR(b)        ((b) + 0x0CU)
#define STM32N6_USART_ISR(b)        ((b) + 0x1CU)
#define STM32N6_USART_RDR(b)        ((b) + 0x24U)
#define STM32N6_USART_TDR(b)        ((b) + 0x28U)
#define STM32N6_USART_PRESC(b)      ((b) + 0x2CU)

#define STM32N6_USART_CR1_UE        (1UL << 0)
#define STM32N6_USART_CR1_RE        (1UL << 2)
#define STM32N6_USART_CR1_TE        (1UL << 3)
#define STM32N6_USART_ISR_RXNE      (1UL << 5)
#define STM32N6_USART_ISR_TC        (1UL << 6)
#define STM32N6_USART_ISR_TXE       (1UL << 7)
#define STM32N6_USART_ISR_ORE       (1UL << 3)

#define STM32N6_USART1_TX_PIN       5U
#define STM32N6_USART1_RX_PIN       6U
#define STM32N6_USART1_AF           7U

/* Cortex-M NVIC. One ISER/ICER word per 32 IRQs. */
#define STM32N6_NVIC_ISER(n)        (0xE000E100UL + ((n) * 4U))
#define STM32N6_NVIC_ICER(n)        (0xE000E180UL + ((n) * 4U))
#define STM32N6_NVIC_ICPR(n)        (0xE000E280UL + ((n) * 4U))
#define STM32N6_NVIC_IPR(irq)       (0xE000E400UL + (irq))

/* LPTIM1 is the kernel time base. Its clock comes from CLKP, and CLKP is
 * pointed at HSI, so the tick does not move when the CPU clock does -- which
 * on this part is inherited from the boot ROM and not reproducible. */
#define STM32N6_RCC_CCIPR7          (STM32N6_RCC_BASE + 0x15CU)
#define STM32N6_RCC_CCIPR12         (STM32N6_RCC_BASE + 0x170U)
#define STM32N6_RCC_APB1LENR        (STM32N6_RCC_BASE + 0x264U)

#define STM32N6_CCIPR7_PERSEL_MSK   (7UL << 0)
#define STM32N6_CCIPR7_PERSEL_HSI   (0UL << 0)
#define STM32N6_CCIPR12_LPTIM1_MSK  (7UL << 8)
#define STM32N6_CCIPR12_LPTIM1_CLKP (1UL << 8)
#define STM32N6_RCC_APB1LENR_LPTIM1 (1UL << 9)

#define STM32N6_LPTIM1_BASE         0x40002400UL
#define STM32N6_LPTIM_ISR(b)        ((b) + 0x00U)
#define STM32N6_LPTIM_ICR(b)        ((b) + 0x04U)
#define STM32N6_LPTIM_DIER(b)       ((b) + 0x08U)
#define STM32N6_LPTIM_CFGR(b)       ((b) + 0x0CU)
#define STM32N6_LPTIM_CR(b)         ((b) + 0x10U)
#define STM32N6_LPTIM_ARR(b)        ((b) + 0x18U)
#define STM32N6_LPTIM_CNT(b)        ((b) + 0x1CU)

#define STM32N6_LPTIM_ISR_ARRM      (1UL << 1)
#define STM32N6_LPTIM_ICR_ARRMCF    (1UL << 1)
#define STM32N6_LPTIM_DIER_ARRMIE   (1UL << 1)
#define STM32N6_LPTIM_CFGR_PRESC_POS 9U
#define STM32N6_LPTIM_CR_ENABLE     (1UL << 0)
#define STM32N6_LPTIM_CR_CNTSTRT    (1UL << 2)

#define STM32N6_IRQ_LPTIM1          136U

#endif /* TIKU_STM32N6_REGS_H_ */
