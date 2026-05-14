/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_cpu_freq_boot_arch.c - STM32F411RE CPU bring-up
 *
 * Default clock target:
 *
 *   HSI (16 MHz)
 *     -> PLLM = 16     -> PLL input = 1 MHz
 *     -> PLLN = 200    -> VCO = 200 MHz
 *     -> PLLP = 2      -> SYSCLK = 100 MHz
 *     -> AHB  /1       -> HCLK  = 100 MHz
 *     -> APB1 /2       -> PCLK1 = 50 MHz
 *     -> APB2 /1       -> PCLK2 = 100 MHz
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_stm32f411_regs.h"
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Local constants                                                           */
/*---------------------------------------------------------------------------*/

#define STM32F411_HSI_HZ              16000000UL
#define STM32F411_CLOCK_SPIN_TIMEOUT  1000000U

#define STM32F411_RCC_CFGR_HPRE_MASK  (0x0FU << 4)
#define STM32F411_RCC_CFGR_PPRE1_MASK (0x07U << 10)
#define STM32F411_RCC_CFGR_PPRE2_MASK (0x07U << 13)

#define STM32F411_RCC_CIR_CSSF        STM32F411_BIT(7)
#define STM32F411_PWR_CR_VOS_SCALE1   (0x03U << STM32F411_PWR_CR_VOS_SHIFT)

/*---------------------------------------------------------------------------*/
/* Linker symbols                                                            */
/*---------------------------------------------------------------------------*/

extern uint32_t __vectors_start;

/*---------------------------------------------------------------------------*/
/* Cached clock rates                                                        */
/*---------------------------------------------------------------------------*/

static volatile unsigned long g_sysclk_hz  = STM32F411_HSI_HZ;
static volatile unsigned long g_hclk_hz    = STM32F411_HSI_HZ;
static volatile unsigned long g_pclk1_hz   = STM32F411_HSI_HZ;
static volatile unsigned long g_pclk2_hz   = STM32F411_HSI_HZ;
static volatile uint8_t       g_clock_fault = 0U;

/*---------------------------------------------------------------------------*/
/* Internal helpers                                                          */
/*---------------------------------------------------------------------------*/

static inline void stm32f411_disable_irq(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
}

static inline void stm32f411_dsb(void) {
    __asm__ volatile ("dsb" ::: "memory");
}

static inline void stm32f411_isb(void) {
    __asm__ volatile ("isb" ::: "memory");
}

static int stm32f411_spin_until_set(uint32_t reg, uint32_t mask) {
    uint32_t i = STM32F411_CLOCK_SPIN_TIMEOUT;
    while (i--) {
        if (_STM32F411_REG(reg) & mask) {
            return 1;
        }
    }
    return 0;
}

static int stm32f411_spin_until_clear(uint32_t reg, uint32_t mask) {
    uint32_t i = STM32F411_CLOCK_SPIN_TIMEOUT;
    while (i--) {
        if ((_STM32F411_REG(reg) & mask) == 0U) {
            return 1;
        }
    }
    return 0;
}

static int stm32f411_spin_until_value(uint32_t reg,
                                      uint32_t mask,
                                      uint32_t value) {
    uint32_t i = STM32F411_CLOCK_SPIN_TIMEOUT;
    while (i--) {
        if ((_STM32F411_REG(reg) & mask) == value) {
            return 1;
        }
    }
    return 0;
}

static void stm32f411_clock_cache_hsi(void) {
    g_sysclk_hz = STM32F411_HSI_HZ;
    g_hclk_hz   = STM32F411_HSI_HZ;
    g_pclk1_hz  = STM32F411_HSI_HZ;
    g_pclk2_hz  = STM32F411_HSI_HZ;
}

static void stm32f411_set_vector_table(void) {
    _STM32F411_REG(STM32F411_SCB_VTOR) = (uint32_t)(uintptr_t)&__vectors_start;
    stm32f411_dsb();
    stm32f411_isb();
}

static void stm32f411_enable_fpu(void) {
    _STM32F411_REG(STM32F411_SCB_CPACR) |=
        STM32F411_SCB_CPACR_CP10_FULL | STM32F411_SCB_CPACR_CP11_FULL;
    stm32f411_dsb();
    stm32f411_isb();
}

static int stm32f411_hsi_enable(void) {
    _STM32F411_REG(STM32F411_RCC_CR) |= STM32F411_RCC_CR_HSION;
    return stm32f411_spin_until_set(STM32F411_RCC_CR,
                                    STM32F411_RCC_CR_HSIRDY);
}

static int stm32f411_switch_sysclk_to_hsi(void) {
    uint32_t cfgr;

    if (!stm32f411_hsi_enable()) {
        return 0;
    }

    cfgr = _STM32F411_REG(STM32F411_RCC_CFGR);
    cfgr &= ~STM32F411_RCC_CFGR_SW_MASK;
    cfgr |= STM32F411_RCC_CFGR_SW_HSI;
    _STM32F411_REG(STM32F411_RCC_CFGR) = cfgr;

    return stm32f411_spin_until_value(STM32F411_RCC_CFGR,
                                      STM32F411_RCC_CFGR_SWS_MASK,
                                      STM32F411_RCC_CFGR_SWS_HSI);
}

static void stm32f411_pll_disable(void) {
    _STM32F411_REG(STM32F411_RCC_CR) &= ~STM32F411_RCC_CR_PLLON;
    (void)stm32f411_spin_until_clear(STM32F411_RCC_CR,
                                     STM32F411_RCC_CR_PLLRDY);
}

static int stm32f411_flash_configure(uint8_t latency) {
    uint32_t acr = _STM32F411_REG(STM32F411_FLASH_ACR);

    acr &= ~STM32F411_FLASH_ACR_LATENCY_MASK;
    acr |= STM32F411_FLASH_ACR_LATENCY(latency)
        | STM32F411_FLASH_ACR_PRFTEN
        | STM32F411_FLASH_ACR_ICEN
        | STM32F411_FLASH_ACR_DCEN;
    _STM32F411_REG(STM32F411_FLASH_ACR) = acr;

    return stm32f411_spin_until_value(STM32F411_FLASH_ACR,
                                      STM32F411_FLASH_ACR_LATENCY_MASK,
                                      STM32F411_FLASH_ACR_LATENCY(latency));
}

static void stm32f411_voltage_scale1(void) {
    stm32f411_rcc_enable_apb1(STM32F411_RCC_APB1_PWR);
    _STM32F411_REG(STM32F411_PWR_CR) =
        (_STM32F411_REG(STM32F411_PWR_CR) & ~STM32F411_PWR_CR_VOS_MASK)
        | STM32F411_PWR_CR_VOS_SCALE1;
    (void)stm32f411_spin_until_set(STM32F411_PWR_CSR,
                                   STM32F411_PWR_CSR_VOSRDY);
}

static void stm32f411_enable_boot_peripherals(void) {
    stm32f411_rcc_enable_ahb1(STM32F411_RCC_AHB1_GPIOA
                            | STM32F411_RCC_AHB1_GPIOB
                            | STM32F411_RCC_AHB1_GPIOC);

    stm32f411_rcc_enable_apb1(STM32F411_RCC_APB1_PWR
                            | STM32F411_RCC_APB1_TIM2
                            | STM32F411_RCC_APB1_TIM5
                            | STM32F411_RCC_APB1_USART2);

    stm32f411_rcc_enable_apb2(STM32F411_RCC_APB2_SYSCFG);
}

struct stm32f411_clock_plan {
    unsigned int  target_mhz;
    uint8_t       use_pll;
    uint8_t       pllm;
    uint16_t      plln;
    uint32_t      pllp_bits;
    uint8_t       pllq;
    uint8_t       flash_latency;
    uint32_t      hpre_bits;
    uint32_t      ppre1_bits;
    uint32_t      ppre2_bits;
    unsigned long sysclk_hz;
    unsigned long hclk_hz;
    unsigned long pclk1_hz;
    unsigned long pclk2_hz;
};

static const struct stm32f411_clock_plan stm32f411_clock_plans[] = {
    {
        16U, 0U, 0U, 0U, 0U, 0U, 0U,
        STM32F411_RCC_CFGR_HPRE_DIV1,
        STM32F411_RCC_CFGR_PPRE1_DIV1,
        STM32F411_RCC_CFGR_PPRE2_DIV1,
        16000000UL, 16000000UL, 16000000UL, 16000000UL
    },
    {
        48U, 1U, 16U, 192U, STM32F411_RCC_PLLCFGR_PLLP_DIV4, 4U, 1U,
        STM32F411_RCC_CFGR_HPRE_DIV1,
        STM32F411_RCC_CFGR_PPRE1_DIV1,
        STM32F411_RCC_CFGR_PPRE2_DIV1,
        48000000UL, 48000000UL, 48000000UL, 48000000UL
    },
    {
        84U, 1U, 16U, 336U, STM32F411_RCC_PLLCFGR_PLLP_DIV4, 7U, 2U,
        STM32F411_RCC_CFGR_HPRE_DIV1,
        STM32F411_RCC_CFGR_PPRE1_DIV2,
        STM32F411_RCC_CFGR_PPRE2_DIV1,
        84000000UL, 84000000UL, 42000000UL, 84000000UL
    },
    {
        100U, 1U, 16U, 200U, STM32F411_RCC_PLLCFGR_PLLP_DIV2, 4U, 3U,
        STM32F411_RCC_CFGR_HPRE_DIV1,
        STM32F411_RCC_CFGR_PPRE1_DIV2,
        STM32F411_RCC_CFGR_PPRE2_DIV1,
        100000000UL, 100000000UL, 50000000UL, 100000000UL
    },
};

#define STM32F411_CLOCK_PLAN_COUNT \
    (sizeof(stm32f411_clock_plans) / sizeof(stm32f411_clock_plans[0]))

static const struct stm32f411_clock_plan *
stm32f411_lookup_clock_plan(unsigned int target_mhz, uint8_t *unsupported) {
    unsigned int i;

    for (i = 0U; i < STM32F411_CLOCK_PLAN_COUNT; i++) {
        if (stm32f411_clock_plans[i].target_mhz == target_mhz) {
            *unsupported = 0U;
            return &stm32f411_clock_plans[i];
        }
    }

    *unsupported = 1U;
    return &stm32f411_clock_plans[STM32F411_CLOCK_PLAN_COUNT - 1U];
}

static void stm32f411_update_clock_cache(
    const struct stm32f411_clock_plan *plan) {
    g_sysclk_hz = plan->sysclk_hz;
    g_hclk_hz   = plan->hclk_hz;
    g_pclk1_hz  = plan->pclk1_hz;
    g_pclk2_hz  = plan->pclk2_hz;
}

static void stm32f411_fallback_hsi(void) {
    (void)stm32f411_switch_sysclk_to_hsi();
    stm32f411_pll_disable();
    (void)stm32f411_flash_configure(0U);
    stm32f411_clock_cache_hsi();
    g_clock_fault = 1U;
}

static int stm32f411_apply_pll_plan(
    const struct stm32f411_clock_plan *plan) {
    uint32_t cfgr;

    if (!stm32f411_switch_sysclk_to_hsi()) {
        return 0;
    }

    stm32f411_pll_disable();
    stm32f411_voltage_scale1();

    if (!stm32f411_flash_configure(plan->flash_latency)) {
        return 0;
    }

    cfgr = _STM32F411_REG(STM32F411_RCC_CFGR);
    cfgr &= ~(STM32F411_RCC_CFGR_HPRE_MASK
            | STM32F411_RCC_CFGR_PPRE1_MASK
            | STM32F411_RCC_CFGR_PPRE2_MASK);
    cfgr |= plan->hpre_bits | plan->ppre1_bits | plan->ppre2_bits;
    _STM32F411_REG(STM32F411_RCC_CFGR) = cfgr;

    _STM32F411_REG(STM32F411_RCC_PLLCFGR) =
        STM32F411_RCC_PLLCFGR_PLLM(plan->pllm)
        | STM32F411_RCC_PLLCFGR_PLLN(plan->plln)
        | plan->pllp_bits
        | STM32F411_RCC_PLLCFGR_PLLSRC_HSI
        | STM32F411_RCC_PLLCFGR_PLLQ(plan->pllq);

    _STM32F411_REG(STM32F411_RCC_CR) |= STM32F411_RCC_CR_PLLON;
    if (!stm32f411_spin_until_set(STM32F411_RCC_CR,
                                  STM32F411_RCC_CR_PLLRDY)) {
        return 0;
    }

    cfgr = _STM32F411_REG(STM32F411_RCC_CFGR);
    cfgr &= ~STM32F411_RCC_CFGR_SW_MASK;
    cfgr |= STM32F411_RCC_CFGR_SW_PLL;
    _STM32F411_REG(STM32F411_RCC_CFGR) = cfgr;

    return stm32f411_spin_until_value(STM32F411_RCC_CFGR,
                                      STM32F411_RCC_CFGR_SWS_MASK,
                                      STM32F411_RCC_CFGR_SWS_PLL);
}

/*---------------------------------------------------------------------------*/
/* Public HAL entry points                                                   */
/*---------------------------------------------------------------------------*/

void tiku_cpu_boot_stm32f411_init(void) {
    stm32f411_disable_irq();
    stm32f411_set_vector_table();
    stm32f411_enable_fpu();

    if (!stm32f411_hsi_enable()) {
        g_clock_fault = 1U;
    }

    stm32f411_clock_cache_hsi();
    stm32f411_enable_boot_peripherals();
}

void tiku_cpu_freq_stm32f411_init(unsigned int target_mhz) {
    uint8_t unsupported = 0U;
    const struct stm32f411_clock_plan *plan =
        stm32f411_lookup_clock_plan(target_mhz, &unsupported);

    if (!plan->use_pll) {
        if (!stm32f411_switch_sysclk_to_hsi()) {
            stm32f411_fallback_hsi();
            return;
        }

        stm32f411_pll_disable();
        if (!stm32f411_flash_configure(plan->flash_latency)) {
            g_clock_fault = 1U;
        } else {
            g_clock_fault = unsupported;
        }
        stm32f411_update_clock_cache(plan);
        return;
    }

    if (!stm32f411_apply_pll_plan(plan)) {
        stm32f411_fallback_hsi();
        return;
    }

    stm32f411_update_clock_cache(plan);
    g_clock_fault = unsupported;
}

void tiku_cpu_boot_stm32f411_power_wfi_enter(void) {
    __asm__ volatile ("wfi" ::: "memory");
}

void tiku_cpu_boot_stm32f411_reset(void) {
    _STM32F411_REG(STM32F411_SCB_AIRCR) =
        STM32F411_SCB_AIRCR_VECTKEY | STM32F411_SCB_AIRCR_SYSRESETREQ;
    stm32f411_dsb();
    for (;;) {
        __asm__ volatile ("wfe" ::: "memory");
    }
}

unsigned long tiku_cpu_stm32f411_clock_get_hz(void) {
    return g_hclk_hz;
}

unsigned long tiku_cpu_stm32f411_smclk_get_hz(void) {
    return g_pclk1_hz;
}

unsigned long tiku_cpu_stm32f411_aclk_get_hz(void) {
    return 0UL;
}

unsigned long tiku_cpu_stm32f411_pclk1_get_hz(void) {
    return g_pclk1_hz;
}

unsigned long tiku_cpu_stm32f411_pclk2_get_hz(void) {
    return g_pclk2_hz;
}

int tiku_cpu_stm32f411_clock_has_fault(void) {
    if (g_clock_fault) {
        return 1;
    }

    return (_STM32F411_REG(STM32F411_RCC_CIR) & STM32F411_RCC_CIR_CSSF)
        ? 1 : 0;
}
