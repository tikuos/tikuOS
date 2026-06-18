/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_apollo4l.c - Apollo4 Lite CPU/SoC bring-up + clocks
 *
 * Mirrors arch/ambiq/tiku_cpu_freq_boot_arch.c (Apollo510) but for the
 * Cortex-M4: there is no SCB L1 I/D cache on the M4 core (Apollo4 has a
 * separate CACHECTRL system cache, brought up with the full-kernel milestone),
 * and no HFRC2 high-performance turbo -- the core runs at the ~96 MHz HFRC the
 * boot ROM leaves configured. SoC bring-up therefore inherits the boot power
 * rails and clock tree as-is; no AmbiqSuite SDK is linked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "apollo4l.h"       /* CMSIS register defs -- register header only */
#include "tiku_cpu_freq_boot_arch.h"

/** True CPU core frequency in Hz (Apollo4 Lite HFRC, low-power default). */
static unsigned long s_core_hz = 96000000UL;

/**
 * @brief Bare-metal Apollo4 Lite SoC bring-up.
 *
 * The Cortex-M4 core has no SCB I/D cache to enable (unlike the M55), and the
 * boot ROM leaves the HFRC core clock and power rails in a usable state. Nothing
 * to do here at the minimal/smoke milestone; the CACHECTRL system cache and any
 * power tuning are added with the full-kernel backends.
 */
static void tiku_ambiq_soc_init(void) {
    /* Intentionally empty: inherit the boot ROM clock/power/cache state. */
}

/**
 * @brief Initialize the Apollo4 Lite CPU at boot.
 *
 * Runs bare-metal SoC bring-up then records the core clock. Called once from
 * main() before any kernel subsystem starts.
 */
void tiku_cpu_boot_ambiq_init(void) {
    tiku_ambiq_soc_init();
    s_core_hz = 96000000UL;
}

/**
 * @brief Set the CPU operating frequency (stub -- clock stays as the boot ROM left it).
 *
 * @param cpu_freq  Requested frequency in Hz (ignored)
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    (void)cpu_freq;
}

/** @brief Enter CPU idle using WFI (used by the scheduler when idle). */
void tiku_cpu_boot_ambiq_power_wfi_enter(void) {
    __asm__ volatile ("wfi");
}

/** @brief Return the main CPU core clock frequency in Hz. */
unsigned long tiku_cpu_ambiq_clock_get_hz(void) { return s_core_hz; }

/** @brief Return the SMCLK-equivalent sub-module clock (same as core here). */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void) { return s_core_hz; }

/** @brief Return the ACLK-equivalent low-frequency clock (32.768 kHz). */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void)  { return 32768UL; }

/** @brief Report whether the main clock has a fault (always 0). */
int           tiku_cpu_ambiq_clock_has_fault(void) { return 0; }
