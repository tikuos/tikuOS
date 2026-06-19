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
#include "tiku_cpu_common.h"  /* tiku_cpu_ambiq_delay_us */

/** True CPU core frequency in Hz (Apollo4 Lite HFRC, low-power default). */
static unsigned long s_core_hz = 96000000UL;

/**
 * @brief Read the true CPU core clock from the MCU performance-mode register.
 *
 * Apollo4 Lite runs the Cortex-M4F at 96 MHz in Low-Power mode or 192 MHz in
 * High-Performance "turbo" mode.
 *
 * @return Core clock frequency in Hz (96000000 or 192000000).
 */
static unsigned long tiku_ambiq_core_hz(void) {
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        return 192000000UL;
    }
    return 96000000UL;
}

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
    s_core_hz = tiku_ambiq_core_hz();
}

/**
 * @brief Bring up the SIMO buck regulator (LDO -> buck) so the core can enter
 *        High-Performance mode.
 *
 * The SBL boots Apollo4 Lite on the LDO; HP (192 MHz) requires the buck active.
 * This mirrors the AmbiqSuite SIMOBUCK_INIT post-PCM / post-A0 path: set the
 * buck LP-TON trims, short VDDF to VDDS (mandatory on this part -- doubles the
 * VDDF load cap so the buck can regulate), enable RX compensation, turn the
 * buck on, then force it active with the CORE + MEM LDOs in parallel, and wait
 * for ACT. The SBL already loaded the per-chip voltage (VREF) trims, so the
 * regulated voltages are unchanged -- this only flips the buck on (no over-volt
 * risk).
 *
 * @return 0 once VRSTATUS.SIMOBUCKST == ACT, -1 on timeout.
 */
static int tiku_ambiq_simobuck_enable(void) {
    uint32_t spin;

    if (PWRCTRL->VRSTATUS_b.SIMOBUCKST == PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        return 0;
    }

    /* This part is post-PCM (INFO1 trim rev >= 2) and post-A0, so the pre-PCM
     * active-TON-trim block is skipped -- this mirrors the AmbiqSuite
     * SIMOBUCK_INIT GT_A0 path. */

    /* Low-power-mode buck switching (TON) trims. */
    MCUCTRL->SIMOBUCK3_b.VDDCLPLOWTONTRIM  = 0xAu;
    MCUCTRL->SIMOBUCK3_b.VDDCLPHIGHTONTRIM = 0xAu;
    MCUCTRL->SIMOBUCK8_b.VDDFLPLOWTONTRIM  = 0xFu;
    MCUCTRL->SIMOBUCK8_b.VDDFLPHIGHTONTRIM = 0xFu;

    /* MANDATORY on Apollo4 Lite: short VDDF to VDDS to double the VDDF load
     * capacitance (2.2uF + 2.2uF). Without this the buck cannot regulate and
     * VRSTATUS.SIMOBUCKST never reaches ACT. */
    MCUCTRL->PWRSW1_b.SHORTVDDFVDDSORVAL = 1u;
    MCUCTRL->PWRSW1_b.SHORTVDDFVDDSOREN  = 1u;
    MCUCTRL->SIMOBUCK13_b.ACTTRIMVDDS    = 0u;  /* VDDS trim -> 0 (now shorted) */

    /* RX compensation on the VDDC / VDDS / VDDF rails. */
    MCUCTRL->SIMOBUCK0 = MCUCTRL_SIMOBUCK0_VDDCRXCOMPEN_Msk |
                         MCUCTRL_SIMOBUCK0_VDDSRXCOMPEN_Msk |
                         MCUCTRL_SIMOBUCK0_VDDFRXCOMPEN_Msk;

    /* Enable the buck (post-A0: just the enable here; the active/override bits
     * and the parallel LDOs follow), then allow dynamic trim latching. */
    PWRCTRL->VRCTRL_b.SIMOBUCKEN        = 1u;
    MCUCTRL->SIMOBUCK15_b.TRIMLATCHOVER = 1u;

    /* Force the buck active and run the CORE + MEM LDOs in parallel with SIMO
     * (Apollo4 Lite requires this); set each *OVER override bit last. */
    MCUCTRL->VRCTRL_b.SIMOBUCKPDNB   = 1u;
    MCUCTRL->VRCTRL_b.SIMOBUCKRSTB   = 1u;
    MCUCTRL->VRCTRL_b.SIMOBUCKACTIVE = 1u;
    MCUCTRL->VRCTRL_b.SIMOBUCKOVER   = 1u;

    MCUCTRL->VRCTRL_b.CORELDOCOLDSTARTEN = 0u;
    MCUCTRL->VRCTRL |= MCUCTRL_VRCTRL_CORELDOACTIVE_Msk |
                       MCUCTRL_VRCTRL_CORELDOACTIVEEARLY_Msk |
                       MCUCTRL_VRCTRL_CORELDOPDNB_Msk;
    MCUCTRL->VRCTRL_b.CORELDOOVER = 1u;

    MCUCTRL->VRCTRL_b.MEMLDOCOLDSTARTEN = 0u;
    MCUCTRL->VRCTRL |= MCUCTRL_VRCTRL_MEMLDOACTIVE_Msk |
                       MCUCTRL_VRCTRL_MEMLDOACTIVEEARLY_Msk |
                       MCUCTRL_VRCTRL_MEMLDOPDNB_Msk;
    MCUCTRL->VRCTRL_b.MEMLDOOVER = 1u;

    /* Wait for the buck to report active. */
    spin = 1000u;
    while (PWRCTRL->VRSTATUS_b.SIMOBUCKST != PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        tiku_cpu_ambiq_delay_us(1u);
        if (spin-- == 0u) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Select the CPU operating frequency (perf mode).
 *
 * Apollo4 Lite supports Low-Power (96 MHz) and High-Performance "turbo"
 * (192 MHz), selected via the PWRCTRL performance-mode request. Unlike
 * Apollo510 this part needs no manual core-voltage step, so the switch is just
 * the request + an ACK poll -- HP only requires the SIMOBUCK regulator active.
 * The OS tick is on the STIMER and the busy-delay re-reads the live core clock,
 * so a mode change doesn't disturb timekeeping.
 *
 * @param cpu_freq  Requested core frequency in MHz.
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    unsigned int want = (cpu_freq > 96u)
        ? PWRCTRL_MCUPERFREQ_MCUPERFREQ_HP
        : PWRCTRL_MCUPERFREQ_MCUPERFREQ_LP;
    uint32_t spin;

    /* HP turbo needs the SIMOBUCK regulator active. The SBL boots on the LDO,
     * so bring the buck up on demand; if it won't reach active, decline HP and
     * stay in the current mode rather than run 192 MHz undervolted. */
    if (want == PWRCTRL_MCUPERFREQ_MCUPERFREQ_HP &&
        PWRCTRL->VRSTATUS_b.SIMOBUCKST != PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        if (tiku_ambiq_simobuck_enable() != 0) {
            return;
        }
    }

    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS != want) {
        PWRCTRL->MCUPERFREQ_b.MCUPERFREQ = want;
        spin = 100000u;
        while (PWRCTRL->MCUPERFREQ_b.MCUPERFACK == 0u) {
            if (spin-- == 0u) break;
        }
    }
    s_core_hz = tiku_ambiq_core_hz();
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
