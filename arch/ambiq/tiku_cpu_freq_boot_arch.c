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

#include "am_mcu_apollo.h"   /* @ambiq-sdk: am_hal_pwrctrl/cachectrl/clkmgr; PWRCTRL */
#include "am_bsp.h"          /* AM_BSP_* board constants/config (header only) */
#include "tiku_cpu_freq_boot_arch.h"

static unsigned long s_core_hz = 96000000UL;  /* true CPU core; set from perf mode */

/*
 * Faithful inline of am_bsp_low_power_init() MINUS the am_util_delay_ms(2000)
 * (and the OEM-recovery SCRATCH check + USB-PHY tuning, both irrelevant to a
 * normal non-USB boot). Same am_hal bring-up, so the SoC comes up identically.
 */
static void tiku_ambiq_soc_init(void) {
    am_hal_pwrctrl_low_power_init();             /* @ambiq-sdk: power (LP/96 MHz) */
    am_hal_cachectrl_icache_enable();            /* @ambiq-sdk: I-cache */
    am_hal_cachectrl_dcache_enable(true);        /* @ambiq-sdk: D-cache */

#if AM_BSP_ENABLE_SIMOBUCK
    am_hal_pwrctrl_control(AM_HAL_PWRCTRL_CONTROL_SIMOBUCK_INIT, NULL); /* @ambiq-sdk */
#endif
#if AM_BSP_SET_ROOM_TEMPS
    {
        am_hal_pwrctrl_temp_thresh_t dummy;
        am_hal_pwrctrl_temp_update(25.0f, &dummy);   /* @ambiq-sdk: spotmgr temp */
    }
#endif

    {
        am_hal_clkmgr_board_info_t info = {
            .sXtalHs.eXtalHsMode    = AM_BSP_XTAL_HS_MODE,
            .sXtalHs.ui32XtalHsFreq = AM_BSP_XTAL_HS_FREQ_HZ,
            .sXtalLs.eXtalLsMode    = AM_BSP_XTAL_LS_MODE,
            .sXtalLs.ui32XtalLsFreq = AM_BSP_XTAL_LS_FREQ_HZ,
            .ui32ExtRefClkFreq      = AM_BSP_EXTREF_CLK_FREQ_HZ
        };
        am_hal_clkmgr_board_info_set(&info);     /* @ambiq-sdk: clock board info */
    }
    am_hal_clkmgr_clock_config(AM_HAL_CLKMGR_CLK_ID_HFRC,
                               AM_HAL_CLKMGR_HFRC_FREQ_FREE_RUN_APPROX_48MHZ,
                               NULL);            /* @ambiq-sdk: HFRC ref ~48 MHz */
    am_hal_clkmgr_clock_config(AM_HAL_CLKMGR_CLK_ID_HFRC2,
                               AM_HAL_CLKMGR_HFRC2_FREQ_FREE_RUN_APPROX_250MHZ,
                               NULL);            /* @ambiq-sdk: HFRC2 ref ~250 MHz */
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
