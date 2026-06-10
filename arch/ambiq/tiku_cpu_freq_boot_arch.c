/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - Apollo 510 CPU/SoC bring-up + clocks
 *
 * Hybrid bring-up: the one-time SoC bring-up (clock manager, power
 * domains, I/D cache) reuses AmbiqSuite's am_bsp_low_power_init() — the
 * same call the hello_world example makes — so we inherit Ambiq's
 * known-good HFRC/HFRC2/XTAL/SIMOBUCK sequencing. All SDK calls are
 * tagged @ambiq-sdk; the de-SDK pass replaces them with the register
 * sequences transcribed from am_hal_clkmgr.c / am_hal_pwrctrl.c /
 * am_hal_cachectrl.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "am_mcu_apollo.h"   /* @ambiq-sdk: SystemCoreClock, am_hal_* */
#include "am_bsp.h"          /* @ambiq-sdk: am_bsp_low_power_init */
#include "tiku_cpu_freq_boot_arch.h"

/* Core clock in Hz, refreshed from CMSIS SystemCoreClock after bring-up.
 * Apollo510 free-runs HFRC ~96 MHz by default. */
static unsigned long s_core_hz = 96000000UL;

void tiku_cpu_boot_ambiq_init(void) {
    /* Full low-power SoC bring-up: power control, the clock manager
     * (HFRC/HFRC2/XTAL), SIMOBUCK, and I/D cache enable. NOTE: this call
     * includes Ambiq's ~2 s silicon-settle delay, so first output appears
     * a couple of seconds after reset. @ambiq-sdk */
    am_bsp_low_power_init();

    /* CMSIS keeps the live core frequency here (updated by SystemInit /
     * the clock manager). Mirror it for the HAL clock-rate queries. */
    s_core_hz = (unsigned long)SystemCoreClock;   /* @ambiq-sdk */
}

void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    /* Apollo510's operating frequency is owned by the clock manager set
     * up in tiku_cpu_boot_ambiq_init(); the MHz hint is accepted only for
     * API compatibility with the boot sequencer. */
    (void)cpu_freq;
}

void tiku_cpu_boot_ambiq_power_wfi_enter(void) {
    __asm__ volatile ("wfi");
}

unsigned long tiku_cpu_ambiq_clock_get_hz(void) { return s_core_hz; }

/* No separate peripheral-clock concept is modelled yet; report the core
 * rate. Refined when the clkmgr is brought up bare-metal. */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void) { return s_core_hz; }

/* Low-frequency domain (LFRC / 32.768 kHz XTAL). */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void)  { return 32768UL; }

int tiku_cpu_ambiq_clock_has_fault(void) { return 0; }
