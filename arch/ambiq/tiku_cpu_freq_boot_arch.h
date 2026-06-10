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
 * NOTE (de-SDK): the implementation currently leans on AmbiqSuite
 * (am_bsp_low_power_init / am_hal_clkmgr / am_hal_pwrctrl / cache).
 * Every such call is tagged @ambiq-sdk in the .c and will be replaced
 * with direct register sequences transcribed from the Ambiq HAL source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_

/* One-time CPU/SoC bring-up: power, clock tree, caches. */
void tiku_cpu_boot_ambiq_init(void);

/* Apply a target core frequency (MHz). Apollo510 runs HFRC-derived
 * clocks; cpu_freq is accepted for API compatibility with the boot
 * sequencer (see tiku.h MAIN_CPU_FREQ). */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq);

/* Idle entry: plain WFI (SysTick / timers / UART RX still wake). */
void tiku_cpu_boot_ambiq_power_wfi_enter(void);

/* Clock-rate queries (Hz) used by /sys and the timer subsystems. */
unsigned long tiku_cpu_ambiq_clock_get_hz(void);   /* core / MCLK   */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void);   /* peripheral    */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void);    /* low-freq (LFRC/XT) */
int           tiku_cpu_ambiq_clock_has_fault(void);

#endif /* TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_ */
