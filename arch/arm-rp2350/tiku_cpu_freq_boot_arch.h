/*
 * Tiku Operating System v0.05
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
 * grouping. Does NOT touch clocks — that is handled by
 * tiku_cpu_freq_rp2350_init().
 */
void tiku_cpu_boot_rp2350_init(void);

/**
 * @brief Configure the system clock tree.
 *
 * Starts the XOSC, locks PLL_SYS to the requested frequency, and
 * switches CLK_SYS to the PLL output. The first port only supports
 * the silicon-default 150 MHz; any other value is silently snapped
 * to 150.
 *
 * @param target_mhz  Requested CLK_SYS frequency in MHz (150 on RP2350).
 */
void tiku_cpu_freq_rp2350_init(unsigned int target_mhz);

/**
 * @brief Enter the processor idle state (WFI).
 *
 * Both LIGHT and DEEP idle modes map here on RP2350 — dormant mode
 * is intentionally not supported in the first port. Wakes on any
 * pending interrupt.
 */
void tiku_cpu_boot_rp2350_power_wfi_enter(void);

/*---------------------------------------------------------------------------*/
/* Clock-rate queries                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the current CLK_SYS (CPU) frequency in Hz.
 *
 * @return CLK_SYS frequency in Hz (typically 150 000 000).
 */
unsigned long tiku_cpu_rp2350_clock_get_hz(void);

/**
 * @brief Return the current CLK_PERI frequency in Hz.
 *
 * CLK_PERI gates the UART, SPI, I2C, and other peripheral blocks.
 * On the first port this is derived from CLK_SYS and equals it.
 *
 * @return CLK_PERI frequency in Hz.
 */
unsigned long tiku_cpu_rp2350_smclk_get_hz(void);

/**
 * @brief Return the low-frequency auxiliary clock frequency in Hz.
 *
 * The RP2350 has no MSP430-style low-frequency ACLK; this function
 * always returns 0 for HAL compatibility.
 *
 * @return Always 0 (no low-frequency clock on RP2350).
 */
unsigned long tiku_cpu_rp2350_aclk_get_hz(void);

/**
 * @brief Return non-zero if the PLL has lost lock or the clock is
 *        in a fault state.
 *
 * Reads the PLL_SYS lock status. A fault typically indicates that
 * the XOSC failed to start or the VCO fell out of range.
 *
 * @return 0 if the clock tree is healthy; non-zero on a fault.
 */
int           tiku_cpu_rp2350_clock_has_fault(void);

#endif /* TIKU_RP2350_CPU_FREQ_BOOT_ARCH_H_ */
