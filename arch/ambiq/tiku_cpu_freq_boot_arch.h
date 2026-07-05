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

/**
 * @brief Raw identity + power snapshot for the HP-turbo bring-up (`freq probe`).
 *
 * Everything the Apollo5 High-Performance mode decision needs, read live:
 * silicon revision, INFO1 residency, the factory trim revision words, the
 * SIMOBUCK/perf-mode state, and the SPOT-manager POWERSTATE trim table. The
 * shell formats it; keeping the raw words here lets the HP port (and a bug
 * report) see exactly what the chip carries.
 */
typedef struct {
    uint32_t chiprev;          /**< MCUCTRL->CHIPREV (REVMAJ/REVMIN)          */
    uint32_t shadowvalid;      /**< MCUCTRL->SHADOWVALID (bit3 = INFO1SELOTP) */
    uint32_t vrstatus;         /**< PWRCTRL->VRSTATUS (SIMOBUCKST bits [5:4]) */
    uint32_t mcuperfreq;       /**< PWRCTRL->MCUPERFREQ (perf mode + status)  */
    uint32_t devpwrstatus;     /**< PWRCTRL->DEVPWRSTATUS (bit27 = OTP power) */
    uint32_t trim_rev;         /**< INFO1 TRIM_REV -- the PCM trim version    */
    uint32_t pgm_info;         /**< INFO1 PGM_INFO (bits [7:0] = TrimSubRev)  */
    uint32_t patch_tracker0;   /**< INFO1 PATCH_TRACKER0 (bit0 = UCRG patch)  */
    uint32_t powerstate[20];   /**< INFO1 SPOT-manager POWERSTATE trim table  */
    uint8_t  info1_in_otp;     /**< 1 = INFO1 read from OTP, 0 = MRAM shadow  */
    uint8_t  info1_ok;         /**< 1 = the INFO1 words above are valid reads */
} tiku_ambiq_hp_probe_t;

/**
 * @brief Fill @p out with the live HP-mode identity/power snapshot.
 *
 * Reads the MRAM INFO1 shadow directly when it is the current INFO1; if INFO1
 * is OTP-resident, powers the OTP block on for the read and restores its
 * previous power state after. Never changes the perf mode or any voltage.
 */
void tiku_cpu_freq_ambiq_hp_probe(tiku_ambiq_hp_probe_t *out);

/**
 * @brief Measure the true core clock against the 32.768 kHz STIMER crystal.
 *
 * Counts SysTick (CLKSOURCE = processor) decrements over a 125 ms STIMER
 * window; independent of what the perf-mode register claims, so it is the
 * ground truth for verifying an LP<->HP switch. Blocks for 125 ms.
 *
 * @return Measured core clock in Hz (0 if SysTick is not configured).
 */
unsigned long tiku_cpu_freq_ambiq_measured_hz(void);

#endif /* TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_ */
