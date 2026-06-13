/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - Apollo 510 CPU/SoC bring-up + clocks
 *
 * Clock facts (Apollo510, hardware-confirmed):
 *   - CPU core: 96 MHz in Low-Power mode (the SBL default),
 *     250 MHz in High-Performance "turbo" mode. The DWT cycle counter (which
 *     counts core cycles) reads 96 MHz, confirming the LP core clock.
 *   - SysTick timer clock: 48 MHz = core/2 on this Cortex-M55. That is the OS
 *     tick + busy-delay timebase (see TIKU_MAIN_CPU_HZ in tiku.h, and the
 *     SysTick delay in tiku_cpu_common.c) — NOT the core clock.
 *   - The HFRC "free-run ~48 MHz" the SBL leaves running is a peripheral
 *     reference oscillator, unrelated to the core clock.
 * s_core_hz below reports the TRUE core clock (read from the perf-mode reg).
 *
 * SoC bring-up is fully bare-metal now (direct CMSIS register access, no
 * AmbiqSuite SDK): it enables the I/D caches + M55 prefetch unit and otherwise
 * inherits the power rails and clock tree exactly as the secure bootloader
 * (SBL) left them. Each dropped am_hal_* call is documented inline below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "apollo510.h"       /* CMSIS register defs (PWRCTRL/CLKGEN/MEMSYSCTL) -- register header only */
#include "tiku_cpu_freq_boot_arch.h"

/** True CPU core frequency in Hz; updated from the perf-mode register at boot */
static unsigned long s_core_hz = 96000000UL;  /* true CPU core; set from perf mode */

/**
 * @brief Bare-metal Apollo510 SoC bring-up (caches + prefetch)
 *
 * Enables the Cortex-M55 I/D caches and prefetch unit via CMSIS, then
 * inherits the power rails and clock tree exactly as the Secure Boot
 * Loader (SBL) left them. Each dropped am_hal_* call is documented
 * inline — a wrong drop would brown out the chip at boot.
 *
 * De-SDK steps performed here:
 *   - Step 3 (TEST): skip am_hal_pwrctrl_low_power_init (SBL provides
 *     a stable power state; brown-out = proof the call was load-bearing).
 *   - Step 1:  dropped SIMOBUCK_INIT and temp_update — efficiency
 *     upgrades only, not correctness.
 *   - Step 2a: dropped clkmgr XTAL bookkeeping and HFRC2 config.
 *   - Step 2b (TEST): drop am_hal_clkmgr_clock_config(HFRC) — the
 *     SBL HFRC already free-runs near 48 MHz; UART cleanness is the
 *     canary.
 */
static void tiku_ambiq_soc_init(void) {
    /* De-SDK step 3 (TEST): skip am_hal_pwrctrl_low_power_init. The SBL leaves
     * the chip in a usable power state -- our reset handler and all early boot
     * already ran on it before this call ever executed. If the system boots and
     * runs stably with a steady VDD_MCU, the SBL power suffices and we reach
     * ZERO am_hal calls; if it browns out (no boot / hang / instability), the
     * LDO/voltage config is load-bearing and we transcribe the essentials. */

    /* Enable the Cortex-M55 I/D caches bare-metal (CMSIS), replacing
     * am_hal_cachectrl_icache/dcache_enable(). The HAL versions are just the
     * CMSIS SCB_Enable*Cache() calls plus the M55 prefetch-unit tuning below
     * (Apollo RevB defaults: MAX_OS=6, MAX_LA=6, MIN_LA=4). */
    SCB_EnableICache();
    MEMSYSCTL->PFCR = (6u << MEMSYSCTL_PFCR_MAX_OS_Pos) |
                      (6u << MEMSYSCTL_PFCR_MAX_LA_Pos) |
                      (4u << MEMSYSCTL_PFCR_MIN_LA_Pos) |
                      (1u << MEMSYSCTL_PFCR_ENABLE_Pos);
    SCB_EnableDCache();
    SCB_CleanDCache();

    /* De-SDK step 1: dropped the optional power-OPTIMISATION calls
     * am_hal_pwrctrl_control(SIMOBUCK_INIT) and am_hal_pwrctrl_temp_update(25C).
     * The core already runs on the LDO that am_hal_pwrctrl_low_power_init set up
     * (boot reached here on it); SIMOBUCK is only a buck-vs-LDO efficiency
     * upgrade, and the spotmgr temperature defaults to a safe value. A wrong
     * drop here would brown the chip out and fail loudly at boot. */

    /* De-SDK step 2a: dropped am_hal_clkmgr_board_info_set (clkmgr XTAL
     * bookkeeping -- tikuOS enables the 32 kHz crystal directly in the htimer,
     * not via the clkmgr) and the HFRC2 (250 MHz) config (nothing uses HFRC2).
     *
     * De-SDK step 2b (TEST): also drop am_hal_clkmgr_clock_config(HFRC) -- the
     * reset/SBL HFRC already free-runs near 48 MHz, so re-configuring it may be
     * redundant. The UART (HFRC/2 = 24 MHz tap) is the canary: clean UART means
     * the config was redundant; a garbled UART means the HFRC needs explicit
     * setup and this returns (bare-metal via CLKGEN). */
}

/**
 * @brief Read the true CPU core clock from the MCU performance-mode register
 *
 * Returns 96 MHz for Low-Power mode or 250 MHz for High-Performance
 * ("turbo") mode. This is independent of the 48 MHz SysTick clock used
 * by the OS tick and busy-delay routines.
 *
 * @return Core clock frequency in Hz (96000000 or 250000000)
 */
static unsigned long tiku_ambiq_core_hz(void) {
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        return 250000000UL;
    }
    return 96000000UL;
}

/**
 * @brief Initialize the Apollo510 CPU at boot
 *
 * Runs bare-metal SoC bring-up (caches, prefetch) then reads the true
 * core clock from the performance-mode register into s_core_hz. Called
 * once from main() before any kernel subsystem starts.
 */
void tiku_cpu_boot_ambiq_init(void) {
    tiku_ambiq_soc_init();          /* caches + prefetch; power/clocks from SBL */
    s_core_hz = tiku_ambiq_core_hz();
}

/**
 * @brief Set the CPU operating frequency (stub)
 *
 * Frequency switching is not yet implemented for Apollo510. The
 * argument is ignored; the clock stays at whatever the SBL configured.
 *
 * @param cpu_freq  Requested frequency in Hz (ignored)
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    (void)cpu_freq;
}

/**
 * @brief Enter CPU idle using the WFI (Wait For Interrupt) instruction
 *
 * Suspends the core until the next interrupt fires. Used by the kernel
 * scheduler when no process is ready to run.
 */
void tiku_cpu_boot_ambiq_power_wfi_enter(void) {
    __asm__ volatile ("wfi");
}

/**
 * @brief Return the main CPU core clock frequency
 *
 * @return Core frequency in Hz as captured at boot from the perf-mode
 *         register (96 MHz LP or 250 MHz HP)
 */
unsigned long tiku_cpu_ambiq_clock_get_hz(void) { return s_core_hz; }

/**
 * @brief Return the SMCLK-equivalent sub-module clock frequency
 *
 * On Apollo510 there is no dedicated SMCLK; this returns the same value
 * as the core clock for callers that query the peripheral reference.
 *
 * @return Core frequency in Hz
 */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void) { return s_core_hz; }

/**
 * @brief Return the ACLK-equivalent auxiliary/low-frequency clock
 *
 * Maps to the 32.768 kHz crystal used by the real-time clock and the
 * htimer STIMER counter.
 *
 * @return 32768 Hz
 */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void)  { return 32768UL; }

/**
 * @brief Report whether the main clock has a fault
 *
 * Always returns 0 on Apollo510 (no oscillator-fault detection is
 * implemented yet).
 *
 * @return 0 (no fault)
 */
int           tiku_cpu_ambiq_clock_has_fault(void) { return 0; }
