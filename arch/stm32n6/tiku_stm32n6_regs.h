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
#define STM32N6_USART_ICR(b)        ((b) + 0x20U)
#define STM32N6_USART_PRESC(b)      ((b) + 0x2CU)

#define STM32N6_USART_CR1_UE        (1UL << 0)
#define STM32N6_USART_CR1_FIFOEN    (1UL << 29)
#define STM32N6_USART_CR1_RE        (1UL << 2)
#define STM32N6_USART_CR1_TE        (1UL << 3)
#define STM32N6_USART_ISR_RXNE      (1UL << 5)
#define STM32N6_USART_ISR_TC        (1UL << 6)
#define STM32N6_USART_ISR_TXE       (1UL << 7)
#define STM32N6_USART_ISR_ORE       (1UL << 3)
#define STM32N6_USART_ICR_ORECF     (1UL << 3)

#define STM32N6_USART1_TX_PIN       5U
#define STM32N6_USART1_RX_PIN       6U
#define STM32N6_USART1_AF           7U

/* Cortex-M NVIC. One ISER/ICER word per 32 IRQs. */
#define STM32N6_NVIC_ISER(n)        (0xE000E100UL + ((n) * 4U))
#define STM32N6_NVIC_ICER(n)        (0xE000E180UL + ((n) * 4U))
#define STM32N6_NVIC_ICPR(n)        (0xE000E280UL + ((n) * 4U))
#define STM32N6_NVIC_IPR(irq)       (0xE000E400UL + (irq))

/* Reset cause flags, latched across a reset. */
#define STM32N6_RCC_RSR             (STM32N6_RCC_BASE + 0x034U)
#define STM32N6_RCC_RSR_PINRSTF     (1UL << 22)
#define STM32N6_RCC_RSR_PORRSTF     (1UL << 23)
#define STM32N6_RCC_RSR_SFTRSTF     (1UL << 24)
#define STM32N6_RCC_RSR_IWDGRSTF    (1UL << 26)
#define STM32N6_RCC_RSR_WWDGRSTF    (1UL << 28)
#define STM32N6_RCC_RSR_LPWRRSTF    (1UL << 30)

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
#define STM32N6_LPTIM_CCR1(b)       ((b) + 0x14U)
#define STM32N6_LPTIM_ARR(b)        ((b) + 0x18U)
#define STM32N6_LPTIM_CNT(b)        ((b) + 0x1CU)

#define STM32N6_LPTIM_ISR_CC1IF     (1UL << 0)
#define STM32N6_LPTIM_ICR_CC1CF     (1UL << 0)
#define STM32N6_LPTIM_DIER_CC1IE    (1UL << 0)
#define STM32N6_LPTIM_ISR_ARRM      (1UL << 1)
#define STM32N6_LPTIM_ISR_ARROK     (1UL << 4)
#define STM32N6_LPTIM_ISR_DIEROK    (1UL << 24)
#define STM32N6_LPTIM_ICR_ARROKCF   (1UL << 4)
#define STM32N6_LPTIM_ICR_DIEROKCF  (1UL << 24)
#define STM32N6_LPTIM_ICR_ARRMCF    (1UL << 1)
#define STM32N6_LPTIM_DIER_ARRMIE   (1UL << 1)
#define STM32N6_LPTIM_CFGR_PRESC_POS 9U
#define STM32N6_LPTIM_CR_ENABLE     (1UL << 0)
#define STM32N6_LPTIM_CR_CNTSTRT    (1UL << 2)

#define STM32N6_IRQ_LPTIM1          136U

/* HPDMA: sixteen channels, 0x80 apart. Driven through the SECURE alias (bit
 * 28 set): the image lives at 0x341xxxxx, the secure alias of AXISRAM, so the
 * channel must issue secure transactions (SECCFGR + TR1 SSEC/DSEC) or RISAF
 * filters them to read-as-zero / write-ignored -- and the SECCFGR write itself
 * is only accepted when it arrives as a secure access. */
#define STM32N6_RCC_AHB5ENR         (STM32N6_RCC_BASE + 0x260U)
#define STM32N6_RCC_AHB5ENR_HPDMA1  (1UL << 0)

#define STM32N6_GPDMA_BASE          0x58020000UL
#define STM32N6_GPDMA_SECCFGR       (STM32N6_GPDMA_BASE + 0x00U)
#define STM32N6_GPDMA_PRIVCFGR      (STM32N6_GPDMA_BASE + 0x04U)
#define STM32N6_GPDMA_CH_STRIDE     0x80UL
#define STM32N6_GPDMA_CH(c)         (STM32N6_GPDMA_BASE + ((c) * STM32N6_GPDMA_CH_STRIDE))
#define STM32N6_GPDMA_FCR(c)        (STM32N6_GPDMA_CH(c) + 0x5CU)
#define STM32N6_GPDMA_SR(c)         (STM32N6_GPDMA_CH(c) + 0x60U)
#define STM32N6_GPDMA_CR(c)         (STM32N6_GPDMA_CH(c) + 0x64U)
#define STM32N6_GPDMA_TR1(c)        (STM32N6_GPDMA_CH(c) + 0x90U)
#define STM32N6_GPDMA_TR2(c)        (STM32N6_GPDMA_CH(c) + 0x94U)
#define STM32N6_GPDMA_BR1(c)        (STM32N6_GPDMA_CH(c) + 0x98U)
#define STM32N6_GPDMA_SAR(c)        (STM32N6_GPDMA_CH(c) + 0x9CU)
#define STM32N6_GPDMA_DAR(c)        (STM32N6_GPDMA_CH(c) + 0xA0U)
#define STM32N6_GPDMA_LLR(c)        (STM32N6_GPDMA_CH(c) + 0xCCU)
#define STM32N6_GPDMA_CIDCFGR(c)    (STM32N6_GPDMA_CH(c) + 0x54U)

/* With CFEN clear a channel emits CID 0 whatever SCID says; the CPU and the
 * trusted domain are CID 1, which is the only CID RISAF's default rule passes
 * when no region is configured -- and the boot ROM configures none. */
#define STM32N6_GPDMA_CID_CFEN      (1UL << 0)
#define STM32N6_GPDMA_CID_SCID_POS  4U
#define STM32N6_GPDMA_CID_TRUSTED   1UL

#define STM32N6_GPDMA_CR_EN         (1UL << 0)
#define STM32N6_GPDMA_CR_RESET      (1UL << 1)
#define STM32N6_GPDMA_CR_TCIE       (1UL << 8)
#define STM32N6_GPDMA_SR_IDLEF      (1UL << 0)
#define STM32N6_GPDMA_SR_TCF        (1UL << 8)
#define STM32N6_GPDMA_SR_DTEF       (1UL << 10)
#define STM32N6_GPDMA_SR_ULEF       (1UL << 11)
#define STM32N6_GPDMA_SR_USEF       (1UL << 12)
#define STM32N6_GPDMA_FCR_ALL       (0x7F00UL)
#define STM32N6_GPDMA_TR1_SINC      (1UL << 3)
#define STM32N6_GPDMA_TR1_SSEC      (1UL << 15)
#define STM32N6_GPDMA_TR1_DSEC      (1UL << 31)
#define STM32N6_GPDMA_TR1_DINC      (1UL << 19)
#define STM32N6_GPDMA_TR1_SDW_POS   0U
#define STM32N6_GPDMA_TR1_DDW_POS   16U
#define STM32N6_GPDMA_TR2_SWREQ     (1UL << 9)
#define STM32N6_GPDMA_MEMCPY_CH     0U
#define STM32N6_IRQ_GPDMA1_CH0      68U   /* HPDMA1_CH0 */

/* TIM1 drives four PWM channels; ST maps them to PE9/PE11/PE13/PE14 on AF1,
 * none of which collide with the console on PE5/PE6. */
#define STM32N6_RCC_APB2ENR_TIM1    (1UL << 0)
#define STM32N6_TIM1_BASE           0x42000000UL
#define STM32N6_TIM_CR1(b)          ((b) + 0x00U)
#define STM32N6_TIM_EGR(b)          ((b) + 0x14U)
#define STM32N6_TIM_CCMR1(b)        ((b) + 0x18U)
#define STM32N6_TIM_CCMR2(b)        ((b) + 0x1CU)
#define STM32N6_TIM_CCER(b)         ((b) + 0x20U)
#define STM32N6_TIM_PSC(b)          ((b) + 0x28U)
#define STM32N6_TIM_ARR(b)          ((b) + 0x2CU)
#define STM32N6_TIM_CCR(b, ch)      ((b) + 0x34U + (((ch) - 1U) * 4U))
#define STM32N6_TIM_BDTR(b)         ((b) + 0x44U)
#define STM32N6_TIM_CR1_CEN         (1UL << 0)
#define STM32N6_TIM_CR1_ARPE        (1UL << 7)
#define STM32N6_TIM_EGR_UG          (1UL << 0)
#define STM32N6_TIM_BDTR_MOE        (1UL << 15)
#define STM32N6_TIM1_AF             1U
#define STM32N6_TIM1_PWM_PORT       STM32N6_GPIO_PORT_E

/* XSPI2 drives the board's 64 MB Macronix NOR through the XSPI I/O manager on
 * port 2. All eleven signals are GPION at AF9; the flash also answers at
 * 0x70000000 once memory-mapped mode is armed, which this driver does not
 * need because every access here is an explicit indirect transfer. */
#define STM32N6_RCC_AHB4ENR_GPION   (1UL << 13)
#define STM32N6_RCC_AHB5ENR_XSPI2   (1UL << 12)
#define STM32N6_RCC_AHB5ENR_XSPIM   (1UL << 13)
#define STM32N6_RCC_CCIPR6          (STM32N6_RCC_BASE + 0x158U)
#define STM32N6_CCIPR6_XSPI2SEL_MSK (3UL << 4)
#define STM32N6_CCIPR6_XSPI2SEL_IC3 (2UL << 4)      /* IC3, per ST's config */

#define STM32N6_GPIO_PORT_N         13U
#define STM32N6_XSPI2_AF            9U

/* The XSPI2 pads live in the VDDIO3 supply domain, which comes up unpowered:
 * the rail has to be declared valid and its range selected before the pins
 * drive anything. This board supplies 1.8 V, which is what ST's own board
 * code selects -- the range bit is only dangerous on a 3.3 V rail. */
#define STM32N6_PWR_SVMCR3          (STM32N6_PWR_BASE + 0x03CU)
#define STM32N6_PWR_SVMCR3_VDDIO3SV (1UL << 9)
#define STM32N6_PWR_SVMCR3_VDDIO3RDY (1UL << 17)
#define STM32N6_PWR_SVMCR3_VDDIO3VRSEL (1UL << 26)

#define STM32N6_XSPIM_BASE          0x4802B400UL
#define STM32N6_XSPIM_CR            (STM32N6_XSPIM_BASE + 0x00U)
#define STM32N6_XSPIM_CR_MUXEN      (1UL << 0)
#define STM32N6_XSPIM_CR_MODE       (1UL << 1)
#define STM32N6_XSPIM_CR_CSSEL_OVR_EN (1UL << 4)
#define STM32N6_XSPIM_CR_REQ2ACK_POS 16U

#define STM32N6_XSPI2_BASE          0x4802A000UL
#define STM32N6_XSPI_CR             (STM32N6_XSPI2_BASE + 0x000U)
#define STM32N6_XSPI_DCR1           (STM32N6_XSPI2_BASE + 0x008U)
#define STM32N6_XSPI_DCR2           (STM32N6_XSPI2_BASE + 0x00CU)
#define STM32N6_XSPI_SR             (STM32N6_XSPI2_BASE + 0x020U)
#define STM32N6_XSPI_FCR            (STM32N6_XSPI2_BASE + 0x024U)
#define STM32N6_XSPI_DLR            (STM32N6_XSPI2_BASE + 0x040U)
#define STM32N6_XSPI_AR             (STM32N6_XSPI2_BASE + 0x048U)
#define STM32N6_XSPI_DR             (STM32N6_XSPI2_BASE + 0x050U)
#define STM32N6_XSPI_CCR            (STM32N6_XSPI2_BASE + 0x100U)
#define STM32N6_XSPI_TCR            (STM32N6_XSPI2_BASE + 0x108U)
#define STM32N6_XSPI_IR             (STM32N6_XSPI2_BASE + 0x110U)

#define STM32N6_XSPI_CR_EN          (1UL << 0)
#define STM32N6_XSPI_CR_ABORT       (1UL << 1)
#define STM32N6_XSPI_CR_FTHRES_POS  8U
#define STM32N6_XSPI_CR_CSSEL       (1UL << 24)
#define STM32N6_XSPI_CR_FMODE_POS   28U
#define STM32N6_XSPI_CR_FMODE_MSK   (3UL << 28)
#define STM32N6_XSPI_FMODE_WRITE    0UL
#define STM32N6_XSPI_FMODE_READ     1UL

#define STM32N6_XSPI_DCR1_CKMODE    (1UL << 0)
#define STM32N6_XSPI_DCR1_CSHT_POS  8U
#define STM32N6_XSPI_DCR1_DEVSIZE_POS 16U
#define STM32N6_XSPI_DCR1_MTYP_MACRONIX (1UL << 24)

#define STM32N6_XSPI_SR_TEF         (1UL << 0)
#define STM32N6_XSPI_SR_TCF         (1UL << 1)
#define STM32N6_XSPI_SR_FTF         (1UL << 2)
#define STM32N6_XSPI_SR_BUSY        (1UL << 5)
#define STM32N6_XSPI_FCR_ALL        (0x1BUL)

/* One line for instruction, address and data: the part powers up in plain SPI
 * and answers there without any mode switch. */
#define STM32N6_XSPI_CCR_IMODE_1L   (1UL << 0)
#define STM32N6_XSPI_CCR_ADMODE_1L  (1UL << 8)
#define STM32N6_XSPI_CCR_ADSIZE_32  (3UL << 12)
#define STM32N6_XSPI_CCR_DMODE_1L   (1UL << 24)
#define STM32N6_XSPI_TCR_DHQC       (1UL << 28)

/* True random number generator, on its own AHB3 clock gate. */
#define STM32N6_RCC_AHB3ENR         (STM32N6_RCC_BASE + 0x258U)
#define STM32N6_RCC_AHB3ENR_RNG     (1UL << 0)

#define STM32N6_RNG_BASE            0x44020000UL
#define STM32N6_RNG_CR              (STM32N6_RNG_BASE + 0x000U)
#define STM32N6_RNG_SR              (STM32N6_RNG_BASE + 0x004U)
#define STM32N6_RNG_DR              (STM32N6_RNG_BASE + 0x008U)

#define STM32N6_RNG_CR_RNGEN        (1UL << 2)
#define STM32N6_RNG_CR_IE           (1UL << 3)
#define STM32N6_RNG_CR_CED          (1UL << 5)
#define STM32N6_RNG_CR_CONDRST      (1UL << 30)
#define STM32N6_RNG_SR_DRDY         (1UL << 0)
#define STM32N6_RNG_SR_CECS         (1UL << 1)
#define STM32N6_RNG_SR_SECS         (1UL << 2)
#define STM32N6_RNG_SR_CEIS         (1UL << 5)
#define STM32N6_RNG_SR_SEIS         (1UL << 6)

/* Cache maintenance. DMA moves data behind the caches, so a buffer must be
 * cleaned before the controller reads it and invalidated after it writes. */
#define STM32N6_SCB_CCR             0xE000ED14UL
#define STM32N6_SCB_CCR_DC          (1UL << 16)
#define STM32N6_SCB_CCR_IC          (1UL << 17)
#define STM32N6_SCB_DCCMVAC         0xE000EF68UL   /* clean by address      */
#define STM32N6_SCB_DCIMVAC         0xE000EF5CUL   /* invalidate by address */
#define STM32N6_CACHE_LINE          32UL

/* DWT cycle counter: a true count of core cycles, so the CPU rate can be
 * measured without assuming how many cycles an instruction takes. */
#define STM32N6_SCB_DEMCR           0xE000EDFCUL
#define STM32N6_SCB_DEMCR_TRCENA    (1UL << 24)
#define STM32N6_DWT_CTRL            0xE0001000UL
#define STM32N6_DWT_CTRL_CYCCNTENA  (1UL << 0)
#define STM32N6_DWT_CYCCNT          0xE0001004UL
#define STM32N6_DWT_LAR             0xE0001FB0UL
#define STM32N6_DWT_LAR_KEY         0xC5ACCE55UL

/* Clock tree. PLL1..4 feed twenty IC dividers; IC1 drives the CPU alone and
 * IC2/IC6/IC11 drive the system buses, so the core rate moves independently.
 * PLL M/N/P fields hold raw values, IC dividers hold divider - 1. */
#define STM32N6_RCC_CFGR1           (STM32N6_RCC_BASE + 0x020U)
#define STM32N6_RCC_CFGR2           (STM32N6_RCC_BASE + 0x024U)
#define STM32N6_RCC_PLL1CFGR1       (STM32N6_RCC_BASE + 0x080U)
#define STM32N6_RCC_PLL1CFGR2       (STM32N6_RCC_BASE + 0x084U)
#define STM32N6_RCC_PLL1CFGR3       (STM32N6_RCC_BASE + 0x088U)
#define STM32N6_RCC_ICCFGR(n)       (STM32N6_RCC_BASE + 0x0C4U + (((n) - 1U) * 4U))
#define STM32N6_RCC_DIVENR          (STM32N6_RCC_BASE + 0x240U)

#define STM32N6_RCC_CR_HSEON        (1UL << 4)
#define STM32N6_RCC_CR_PLL1ON       (1UL << 8)
#define STM32N6_RCC_SR_HSERDY       (1UL << 4)
#define STM32N6_RCC_SR_PLL1RDY      (1UL << 8)

/* CPUSW and SYSSW share one encoding; 3 selects the IC dividers. */
#define STM32N6_CLKSRC_HSI          0UL
#define STM32N6_CLKSRC_MSI          1UL
#define STM32N6_CLKSRC_HSE          2UL
#define STM32N6_CLKSRC_IC           3UL

#define STM32N6_CFGR1_CPUSW_POS     16U
#define STM32N6_CFGR1_CPUSW_MSK     (3UL << 16)
#define STM32N6_CFGR1_CPUSWS_POS    20U
#define STM32N6_CFGR1_SYSSW_POS     24U
#define STM32N6_CFGR1_SYSSW_MSK     (3UL << 24)
#define STM32N6_CFGR1_SYSSWS_POS    28U

/* Bus prescalers encode log2, so 0 divides by 1 and 1 divides by 2. */
#define STM32N6_CFGR2_PPRE1_MSK     (7UL << 0)
#define STM32N6_CFGR2_PPRE2_MSK     (7UL << 4)
#define STM32N6_CFGR2_PPRE4_MSK     (7UL << 12)
#define STM32N6_CFGR2_PPRE5_MSK     (7UL << 16)
#define STM32N6_CFGR2_HPRE_POS      20U
#define STM32N6_CFGR2_HPRE_MSK      (7UL << 20)

#define STM32N6_PLL1_DIVN_POS       8U
#define STM32N6_PLL1_DIVN_MSK       (0xFFFUL << 8)
#define STM32N6_PLL1_DIVM_POS       20U
#define STM32N6_PLL1_DIVM_MSK       (0x3FUL << 20)
#define STM32N6_PLL1_BYP            (1UL << 27)
#define STM32N6_PLL1_SEL_POS        28U
#define STM32N6_PLL1_SEL_MSK        (7UL << 28)
#define STM32N6_PLL1_PDIV2_POS      24U
#define STM32N6_PLL1_PDIV2_MSK      (7UL << 24)
#define STM32N6_PLL1_PDIV1_POS      27U
#define STM32N6_PLL1_PDIV1_MSK      (7UL << 27)
#define STM32N6_PLL1_PDIVEN         (1UL << 30)

#define STM32N6_IC_INT_POS          16U
#define STM32N6_IC_INT_MSK          (0xFFUL << 16)
#define STM32N6_IC_SEL_POS          28U
#define STM32N6_IC_SEL_MSK          (3UL << 28)
#define STM32N6_IC_SEL_PLL1         0UL

/* Voltage scaling: VOS set is range 0, the high-frequency range. */
#define STM32N6_PWR_BASE            0x46024800UL
#define STM32N6_PWR_VOSCR           (STM32N6_PWR_BASE + 0x020U)
#define STM32N6_PWR_VOSCR_VOS       (1UL << 0)
#define STM32N6_PWR_VOSCR_VOSRDY    (1UL << 1)

/* The Nucleo drives its external SMPS from PB12: high selects the 0.89 V
 * overdrive rail that the core needs above the nominal range. Board revisions
 * before C01 leave that net unpopulated. */
#define STM32N6_GPIO_PORT_B         1U
#define STM32N6_SMPS_OVD_PIN        12U

#endif /* TIKU_STM32N6_REGS_H_ */
