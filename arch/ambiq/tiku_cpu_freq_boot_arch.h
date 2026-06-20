/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - Apollo 510 (Cortex-M55) CPU boot + clocks
 *
 * Mirrors arch/arm-rp2350/tiku_cpu_freq_boot_arch.h. These are the
 * arch backends dispatched by hal/tiku_cpu.c under PLATFORM_AMBIQ.
 *
 * NOTE (de-SDK): fully bare-metal. The implementation uses only direct
 * CMSIS register access (apollo510.h); no AmbiqSuite HAL/BSP calls remain.
 * Power and clocks are inherited from the secure bootloader (SBL).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_

#include <stdint.h>

/**
 * @brief Perform one-time CPU and SoC bring-up.
 *
 * Configures power domains, the clock tree, and caches after SBL
 * hand-off. Called once very early in main() before any other
 * subsystem initializes. Uses direct CMSIS register access only —
 * no AmbiqSuite HAL/BSP calls.
 */
void tiku_cpu_boot_ambiq_init(void);

/**
 * @brief Apply a target core frequency to the Apollo510 clock tree.
 *
 * Apollo510 derives the core clock from HFRC or HFRC2. The @p cpu_freq
 * parameter is accepted for API compatibility with the portable boot
 * sequencer (see TIKU_MAIN_CPU_FREQ in tiku.h); the implementation
 * selects the nearest supported divider.
 *
 * @param cpu_freq  Desired core frequency in MHz.
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq);

/**
 * @brief Enter CPU idle via WFI (Wait For Interrupt).
 *
 * Issues a plain ARM WFI instruction. SysTick, software timers,
 * UART RX, and other enabled interrupts will wake the core. This
 * is the lowest-power idle state that preserves all register context.
 */
void tiku_cpu_boot_ambiq_power_wfi_enter(void);

/**
 * @brief Query the current core (MCLK) frequency.
 *
 * Returns the frequency of the Cortex-M55 core clock as last
 * configured by tiku_cpu_freq_ambiq_init(). Used by /sys and the
 * timer subsystems to compute tick intervals.
 *
 * @return Core clock frequency in Hz.
 */
unsigned long tiku_cpu_ambiq_clock_get_hz(void);

/**
 * @brief Query the peripheral (PCLK) bus frequency.
 *
 * Returns the frequency used by IOM, UART, ADC, and other
 * peripheral modules. May differ from the core clock.
 *
 * @return Peripheral clock frequency in Hz.
 */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void);

/**
 * @brief Query the low-frequency auxiliary clock (LFRC / XTAL) frequency.
 *
 * Returns the frequency of the low-frequency clock source feeding
 * STIMER and the RTC. Nominally 32768 Hz when the XTAL is running.
 *
 * @return Low-frequency clock frequency in Hz.
 */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void);

/**
 * @brief Check whether a clock fault is currently active.
 *
 * Reads the CLKGEN fault status register. A non-zero return indicates
 * the clock tree may be running on a fallback source.
 *
 * @return Non-zero if a clock fault is detected, 0 if clocks are clean.
 */
int           tiku_cpu_ambiq_clock_has_fault(void);

/**
 * @brief Clean / invalidate the data cache over [addr, addr+len).
 *
 * Per-part: Apollo510 (M55) uses the SCB by-address ops; Apollo4 Lite uses
 * the CACHECTRL (clean is a no-op -- its MRAM data cache is read-only from the
 * CPU; invalidate flushes the whole cache, lacking a by-range op). Routed from
 * the portable tiku_cpu_dcache_* HAL.
 */
void          tiku_cpu_ambiq_dcache_clean(const void *addr, unsigned long len);
void          tiku_cpu_ambiq_dcache_invalidate(const void *addr, unsigned long len);

#endif /* TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_ */
