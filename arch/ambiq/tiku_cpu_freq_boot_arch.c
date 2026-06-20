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
 *   - CPU core: 96 MHz in Low-Power mode (the SBL default), 192 MHz in
 *     High-Performance "turbo" mode (HFRC2 is 250 MHz, but the M55 runs 192 in
 *     HP). The DWT cycle counter (which counts core cycles) reads 96 MHz,
 *     confirming the LP core clock. HP/turbo is intentionally NOT enabled yet
 *     -- it is kept as a future addition; see tiku_cpu_freq_ambiq_init().
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
 * Returns 96 MHz for Low-Power mode or 192 MHz for High-Performance
 * ("turbo") mode -- per the apollo510.h MCUPERFSTATUS_HP definition (HFRC2 is
 * 250 MHz but the M55 runs at 192 MHz in HP; an earlier 250 here was wrong).
 *
 * @return Core clock frequency in Hz (96000000 or 192000000)
 */
static unsigned long tiku_ambiq_core_hz(void) {
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        return 192000000UL;
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
 * @brief Select the CPU operating frequency (perf mode).
 *
 * Apollo510 has Low-Power (96 MHz) and High-Performance "turbo" (192 MHz)
 * modes via the PWRCTRL performance-mode request. Only LP is honoured here.
 *
 * HP/turbo is INTENTIONALLY DEFERRED -- kept as a future addition. Unlike
 * Apollo4 Lite, where HP just needs the SIMO buck enabled with the voltages
 * unchanged (see tiku_ambiq_simobuck_enable() in tiku_cpu_freq_boot_apollo4l.c),
 * Apollo5 HP requires actively RAISING the core voltage (VDDC/VDDF) before the
 * switch. The targets are per-chip factory trims in INFO1/OTP, selected by a
 * temperature + CPU/GPU-state matrix (the AmbiqSuite "SPOT manager"), applied
 * via a timed double-boost with HFRC2 forced on and an ICACHE-gated PWRSW
 * supply override. A wrong trim/index risks an over-voltage, so this must be
 * brought up HW-in-the-loop on a connected apollo510 EVB, never blind. Until
 * then an HP request is declined and the core stays in LP rather than run
 * 192 MHz on the LP voltage rail (which would brown out). The OS tick is on the
 * STIMER and the busy-delay re-reads the live core clock, so a mode change
 * doesn't disturb timekeeping.
 *
 * @param cpu_freq  Requested core frequency in MHz.
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    uint32_t spin;

    if (cpu_freq > 96u) {
        /* HP/turbo is a future addition: the per-chip SPOT voltage raise
         * described above is not yet ported, so decline -- the M55 never runs
         * at 192 MHz on the LP voltage rail. `freq 192` therefore reports
         * "not applied" on apollo510 by design. */
        return;
    }

    /* LP request: ensure Low-Power mode (the SBL default, so usually a no-op),
     * then refresh the reported core clock. */
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS != PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_LP) {
        PWRCTRL->MCUPERFREQ_b.MCUPERFREQ = PWRCTRL_MCUPERFREQ_MCUPERFREQ_LP;
        spin = 100000u;
        while (PWRCTRL->MCUPERFREQ_b.MCUPERFACK == 0u) {
            if (spin-- == 0u) break;
        }
    }
    s_core_hz = tiku_ambiq_core_hz();
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
 *         register (96 MHz LP or 192 MHz HP)
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

/**
 * @brief Apollo510 (M55) data-cache maintenance (routed from tiku_cpu_dcache_*).
 *
 * The M55 has architectural L1 I/D caches (enabled in soc_init), so coherency
 * with out-of-band MRAM writes needs by-address SCB ops: clean the staging
 * buffer before the bootrom reads it, invalidate the programmed page after.
 */
void tiku_cpu_ambiq_dcache_clean(const void *addr, unsigned long len) {
    SCB_CleanDCache_by_Addr((void *)(uintptr_t)addr, (int32_t)len);
}

void tiku_cpu_ambiq_dcache_invalidate(const void *addr, unsigned long len) {
    SCB_InvalidateDCache_by_Addr((void *)(uintptr_t)addr, (int32_t)len);
}
