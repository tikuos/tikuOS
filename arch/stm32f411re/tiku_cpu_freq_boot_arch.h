/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_cpu_freq_boot_arch.h - STM32F411RE CPU clock / boot interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_STM32F411_CPU_FREQ_BOOT_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Required HAL entry points                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Prepare the Cortex-M4 and STM32F411 reset-default hardware state.
 *
 * Called once from tiku_cpu_boot_init(). This function leaves the core on
 * HSI and enables only the baseline clocks needed by the next boot stages.
 * PLL setup belongs to tiku_cpu_freq_stm32f411_init().
 */
void tiku_cpu_boot_stm32f411_init(void);

/**
 * @brief Configure the STM32F411 clock tree.
 *
 * @param target_mhz Requested SYSCLK/HCLK frequency in MHz. Supported first
 *                   targets are 16, 48, 84, and 100 MHz. Unsupported targets
 *                   select the safe default, 100 MHz, and set the fault flag.
 */
void tiku_cpu_freq_stm32f411_init(unsigned int target_mhz);

/**
 * @brief Idle entry point for the scheduler.
 *
 * All generic idle depths map to WFI for the first STM32F411 port.
 */
void tiku_cpu_boot_stm32f411_power_wfi_enter(void);

/**
 * @brief Request a Cortex-M system reset.
 */
void tiku_cpu_boot_stm32f411_reset(void);

/*---------------------------------------------------------------------------*/
/* Clock-rate queries                                                        */
/*---------------------------------------------------------------------------*/

unsigned long tiku_cpu_stm32f411_clock_get_hz(void);  /* HCLK / core clock */
unsigned long tiku_cpu_stm32f411_smclk_get_hz(void);  /* APB1 peripheral clock */
unsigned long tiku_cpu_stm32f411_aclk_get_hz(void);   /* LSI/LSE, if enabled */
unsigned long tiku_cpu_stm32f411_pclk1_get_hz(void);
unsigned long tiku_cpu_stm32f411_pclk2_get_hz(void);
int           tiku_cpu_stm32f411_clock_has_fault(void);

#endif /* TIKU_STM32F411_CPU_FREQ_BOOT_ARCH_H_ */
