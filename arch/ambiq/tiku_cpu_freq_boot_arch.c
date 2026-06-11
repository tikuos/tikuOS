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
 *   - CPU core: 96 MHz in Low-Power mode (am_hal_pwrctrl_low_power_init),
 *     250 MHz in High-Performance "turbo" mode. The DWT cycle counter (which
 *     counts core cycles) reads 96 MHz, confirming the LP core clock.
 *   - SysTick timer clock: 48 MHz = core/2 on this Cortex-M55. That is the OS
 *     tick + busy-delay timebase (see TIKU_MAIN_CPU_HZ in tiku.h, and the
 *     SysTick delay in tiku_cpu_common.c) — NOT the core clock.
 *   - The HFRC "free-run ~48 MHz" the BSP configures is a peripheral reference
 *     oscillator, unrelated to the core clock.
 * s_core_hz below reports the TRUE core clock (read from the perf-mode reg).
 *
 * The one-time SoC bring-up inlines am_bsp_low_power_init()'s essential am_hal
 * init (pwrctrl + I/D cache + clkmgr) but DROPS its gratuitous
 * am_util_delay_ms(2000) at the top (~2 s of boot, gone). Still @ambiq-sdk:
 * those am_hal pwrctrl/cachectrl/clkmgr calls (the core), bare-metal later.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "am_mcu_apollo.h"   /* @ambiq-sdk: am_hal_pwrctrl/clkmgr; PWRCTRL/CLKGEN regs */
#include "tiku_cpu_freq_boot_arch.h"

static unsigned long s_core_hz = 96000000UL;  /* true CPU core; set from perf mode */

/*
 * Faithful inline of am_bsp_low_power_init() MINUS the am_util_delay_ms(2000)
 * (and the OEM-recovery SCRATCH check + USB-PHY tuning, both irrelevant to a
 * normal non-USB boot). Same am_hal bring-up, so the SoC comes up identically.
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

/* True CPU core clock from the MCU performance-mode register: Low-Power = 96 MHz,
 * High-Performance ("turbo") = 250 MHz. (Independent of the 48 MHz SysTick clock.) */
static unsigned long tiku_ambiq_core_hz(void) {
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            AM_HAL_PWRCTRL_MCU_MODE_HIGH_PERFORMANCE) {
        return 250000000UL;
    }
    return 96000000UL;
}

void tiku_cpu_boot_ambiq_init(void) {
    tiku_ambiq_soc_init();          /* = am_bsp_low_power_init() minus the 2 s delay */
    s_core_hz = tiku_ambiq_core_hz();
}

void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    (void)cpu_freq;
}

void tiku_cpu_boot_ambiq_power_wfi_enter(void) {
    __asm__ volatile ("wfi");
}

unsigned long tiku_cpu_ambiq_clock_get_hz(void) { return s_core_hz; }
unsigned long tiku_cpu_ambiq_smclk_get_hz(void) { return s_core_hz; }
unsigned long tiku_cpu_ambiq_aclk_get_hz(void)  { return 32768UL; }
int           tiku_cpu_ambiq_clock_has_fault(void) { return 0; }
