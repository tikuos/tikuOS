/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - RP2350 CPU clock / boot interface
 *
 * Boot-time clock bring-up, run-time PLL_SYS retuning, WFI idle and the
 * cached clock rates.  Clocks run XOSC -> PLL_SYS -> CLK_SYS, and CLK_PERI
 * runs from CLK_SYS.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_RP2350_CPU_FREQ_BOOT_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* REQUIRED HAL ENTRY POINTS                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring up the clocks and release the kernel's peripherals from reset.
 *
 * Starts the XOSC, locks PLL_SYS and runs CLK_SYS and CLK_PERI at 150 MHz,
 * releases IO_BANK0, PADS_BANK0, UART0, TIMER0 and TIMER1 from reset and
 * starts the 1 us tick.  A step that times out leaves CLK_PERI on XOSC.
 *
 * @note Call it once at boot, before any peripheral driver starts.
 */
void tiku_cpu_boot_rp2350_init(void);

/**
 * @brief Retune CLK_SYS to one of the supported frequencies.
 *
 * Supports 12, 48, 100, 125, 133 and 150 MHz; 12 MHz runs CLK_SYS from XOSC
 * with PLL_SYS off.  An unsupported target sets the clock-fault flag and
 * leaves the clock as it was.
 *
 * @param target_mhz  Requested CLK_SYS frequency in MHz.
 */
void tiku_cpu_freq_rp2350_init(unsigned int target_mhz);

/**
 * @brief Enter the processor idle state (WFI).
 *
 * The HAL maps the light, deep and deepest idle modes to it; this port does
 * not use dormant mode.  Wakes on any pending interrupt.
 */
void tiku_cpu_boot_rp2350_power_wfi_enter(void);

/*---------------------------------------------------------------------------*/
/* CLOCK-RATE QUERIES                                                        */
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
 * CLK_PERI clocks the UART, SPI, I2C and other peripheral blocks.  The
 * cached rate always equals CLK_SYS.
 *
 * @return CLK_PERI frequency in Hz.
 */
unsigned long tiku_cpu_rp2350_smclk_get_hz(void);

/**
 * @brief Return the low-frequency auxiliary clock frequency in Hz.
 *
 * This port runs no low-frequency auxiliary clock, so it returns 0 for the
 * HAL's ACLK query.
 *
 * @return Always 0 (no low-frequency clock on RP2350).
 */
unsigned long tiku_cpu_rp2350_aclk_get_hz(void);

/**
 * @brief Report whether the last clock init or retune failed.
 *
 * Returns a flag set when an unsupported frequency was requested or a PLL or
 * mux step timed out, and cleared by a successful init or retune.
 *
 * @return 1 after a fault, 0 otherwise.
 */
int           tiku_cpu_rp2350_clock_has_fault(void);

#endif /* TIKU_RP2350_CPU_FREQ_BOOT_ARCH_H_ */
