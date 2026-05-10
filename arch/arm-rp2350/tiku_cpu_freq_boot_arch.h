/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - RP2350 CPU clock / boot interface
 *
 * Mirrors arch/msp430/tiku_cpu_freq_boot_arch.h. The RP2350 has a
 * single mainstream clock topology (XOSC -> PLL_SYS -> CLK_SYS) so
 * the API is much smaller than on MSP430.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_RP2350_CPU_FREQ_BOOT_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Required HAL entry points                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring all peripherals out of reset and prepare basic state.
 *
 * Called once from tiku_cpu_boot_init(). Releases IO_BANK0 +
 * PADS_BANK0 from reset, clears pad ISO, and configures NVIC priority
 * grouping. Does NOT touch clocks — that's tiku_cpu_freq_rp2350_init().
 */
void tiku_cpu_boot_rp2350_init(void);

/**
 * @brief Configure the system clock tree.
 *
 * @param target_mhz Requested CLK_SYS frequency in MHz. The first port
 *                   only supports the silicon default, 150 MHz; any
 *                   other value is silently snapped to 150.
 */
void tiku_cpu_freq_rp2350_init(unsigned int target_mhz);

/**
 * @brief Idle entry point for the scheduler (WFI).
 *
 * Both LIGHT and DEEP idle modes map here on RP2350 — dormant mode
 * is intentionally not supported in the first port.
 */
void tiku_cpu_boot_rp2350_power_wfi_enter(void);

/*---------------------------------------------------------------------------*/
/* Clock-rate queries                                                        */
/*---------------------------------------------------------------------------*/

unsigned long tiku_cpu_rp2350_clock_get_hz(void);   /* CLK_SYS */
unsigned long tiku_cpu_rp2350_smclk_get_hz(void);   /* CLK_PERI */
unsigned long tiku_cpu_rp2350_aclk_get_hz(void);    /* always 0 (no LF clk) */
int           tiku_cpu_rp2350_clock_has_fault(void);

#endif /* TIKU_RP2350_CPU_FREQ_BOOT_ARCH_H_ */
