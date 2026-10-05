/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_apollo4l.c - Apollo4 CPU/SoC bring-up and clocks.
 *
 * Boot enables the CACHECTRL cache over MRAM.  The core runs at 96 MHz in
 * low-power mode or 192 MHz in high-performance mode, which first brings up
 * the SIMO buck.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "apollo4l.h"       /* CMSIS register map */
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_cpu_common.h"  /* tiku_cpu_ambiq_delay_us */

/** @brief Core clock in Hz, read from the perf-mode status on each change. */
static unsigned long s_core_hz = 96000000UL;

/**
 * @brief Read the core clock from the perf-mode status (PWRCTRL.MCUPERFREQ).
 *
 * @return 96000000 in low-power mode, 192000000 in high-performance mode.
 */
static unsigned long tiku_ambiq_core_hz(void) {
    if (PWRCTRL->MCUPERFREQ_b.MCUPERFSTATUS ==
            PWRCTRL_MCUPERFREQ_MCUPERFSTATUS_HP) {
        return 192000000UL;
    }
    return 96000000UL;
}

/**
 * @brief Enable the CACHECTRL cache over MRAM, instruction and data sides.
 *
 * The Cortex-M4 has no SCB caches.  The boot ROM leaves the HFRC core clock
 * and the power rails usable, so nothing else is set up here.
 */
static void tiku_ambiq_soc_init(void) {
    /* The SBL configures CACHECFG but leaves it disabled; without the cache
     * every fetch from MRAM stalls the core, more so at 192 MHz.  The D-cache
     * holds read-only MRAM data: the CPU never writes MRAM through it (the
     * boot ROM programs MRAM), and tiku_mem_arch_nvm_flush() invalidates it
     * after each program.  The configuration is AmbiqSuite's: 1-way, 128-bit
     * lines, 4096 entries, then enable and invalidate. */
    CPU->CACHECFG = (1u << CPU_CACHECFG_CLKGATE_Pos)
                  | (1u << CPU_CACHECFG_DATACLKGATE_Pos)
                  | (1u << CPU_CACHECFG_LRU_Pos)
                  | ((uint32_t)CPU_CACHECFG_CONFIG_W1_128B_4096E
                        << CPU_CACHECFG_CONFIG_Pos)
                  | (1u << CPU_CACHECFG_IENABLE_Pos)
                  | (1u << CPU_CACHECFG_DENABLE_Pos);   /* I-cache + D-cache */
    CPU->CACHECFG_b.ENABLE      = 1u;
    CPU->CACHECTRL_b.INVALIDATE = 1u;
}

/**
 * @brief Apollo4 data-cache maintenance, for tiku_cpu_dcache_*().
 *
 * Clean does nothing: the CACHECTRL D-cache holds only read-only MRAM data.
 * Invalidate drops the whole cache, as CACHECTRL has no by-range operation.
 *
 * @note The barrier pair lets the invalidate take effect before the next
 *       fetch or load.
 */
void tiku_cpu_ambiq_dcache_clean(const void *addr, unsigned long len) {
    (void)addr; (void)len;   /* nothing dirty to write back */
}

void tiku_cpu_ambiq_dcache_invalidate(const void *addr, unsigned long len) {
    (void)addr; (void)len;
    CPU->CACHECTRL_b.INVALIDATE = 1u;
    __DSB();
    __ISB();
}

/**
 * @brief Apollo4 I-cache invalidate, for tiku_cpu_icache_invalidate().
 *
 * The CACHECTRL cache serves instruction fetches too, so this is the same
 * whole-cache invalidate as the data side.
 */
void tiku_cpu_ambiq_icache_invalidate(void) {
    CPU->CACHECTRL_b.INVALIDATE = 1u;
    __DSB();
    __ISB();
}

/**
 * @brief Boot-time CPU setup: enable the cache, then read the core clock.
 */
void tiku_cpu_boot_ambiq_init(void) {
    tiku_ambiq_soc_init();
    s_core_hz = tiku_ambiq_core_hz();
}

/**
 * @brief Bring up the SIMO buck (LDO -> buck) so the core can enter
 *        high-performance mode.
 *
 * The SBL boots on the LDO; HP (192 MHz) needs the buck active.  The sequence
 * is AmbiqSuite's SIMOBUCK_INIT post-PCM path: LP-TON trims, VDDF shorted to
 * VDDS, RX compensation, buck on, forced active with the CORE and MEM LDOs.
 *
 * @note The short doubles the VDDF load capacitance so the buck can regulate.
 *       The SBL has loaded the per-chip VREF trims, so the regulated voltages
 *       do not change.
 * @return 0 once VRSTATUS.SIMOBUCKST == ACT, -1 on timeout.
 */
static int tiku_ambiq_simobuck_enable(void) {
    uint32_t spin;

    if (PWRCTRL->VRSTATUS_b.SIMOBUCKST == PWRCTRL_VRSTATUS_SIMOBUCKST_ACT) {
        return 0;
    }

    /* AmbiqSuite's SIMOBUCK_INIT sequence for parts newer than revision A0
     * with post-PCM trims (INFO1 trim rev >= 2), which leaves out the
     * active-TON trims of pre-PCM parts.  The code does not check the part's
     * revision. */

    /* Low-power-mode buck switching (TON) trims. */
    MCUCTRL->SIMOBUCK3_b.VDDCLPLOWTONTRIM  = 0xAu;
    MCUCTRL->SIMOBUCK3_b.VDDCLPHIGHTONTRIM = 0xAu;
    MCUCTRL->SIMOBUCK8_b.VDDFLPLOWTONTRIM  = 0xFu;
    MCUCTRL->SIMOBUCK8_b.VDDFLPHIGHTONTRIM = 0xFu;

    /* Short VDDF to VDDS to double the VDDF load capacitance (2.2 uF +
     * 2.2 uF); without it the buck cannot regulate and VRSTATUS.SIMOBUCKST
     * never reaches ACT. */
    MCUCTRL->PWRSW1_b.SHORTVDDFVDDSORVAL = 1u;
    MCUCTRL->PWRSW1_b.SHORTVDDFVDDSOREN  = 1u;
    MCUCTRL->SIMOBUCK13_b.ACTTRIMVDDS    = 0u;  /* VDDS trim 0: shorted */

    /* RX compensation on the VDDC / VDDS / VDDF rails. */
    MCUCTRL->SIMOBUCK0 = MCUCTRL_SIMOBUCK0_VDDCRXCOMPEN_Msk |
                         MCUCTRL_SIMOBUCK0_VDDSRXCOMPEN_Msk |
                         MCUCTRL_SIMOBUCK0_VDDFRXCOMPEN_Msk;

    /* Enable the buck (post-A0: the enable alone; the active and override
     * bits and the parallel LDOs follow), then allow dynamic trim latching. */
    PWRCTRL->VRCTRL_b.SIMOBUCKEN        = 1u;
    MCUCTRL->SIMOBUCK15_b.TRIMLATCHOVER = 1u;

    /* Force the buck active and run the CORE and MEM LDOs in parallel with
     * it, as Apollo4 requires; each *OVER override bit is set last. */
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
 * @brief Select the CPU perf mode: LP (96 MHz) or HP (192 MHz).
 *
 * A request above 96 MHz asks for HP, after bringing up the SIMO buck; any
 * other asks for LP.  The switch is the PWRCTRL request and an ACK poll.
 *
 * @note The tick runs from STIMER and busy delays read the core clock at
 *       entry, so a mode change does not disturb timekeeping.
 * @param cpu_freq  Requested core frequency in MHz.
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq) {
    unsigned int want = (cpu_freq > 96u)
        ? PWRCTRL_MCUPERFREQ_MCUPERFREQ_HP
        : PWRCTRL_MCUPERFREQ_MCUPERFREQ_LP;
    uint32_t spin;

    /* HP needs the SIMO buck active and the SBL boots on the LDO, so the
     * buck comes up on demand.  If it does not reach ACT the request is
     * declined and the mode stays as it is: 192 MHz on the LDO is
     * undervolted. */
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

/** @brief Return the SMCLK-equivalent clock: the core clock on this port. */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void) { return s_core_hz; }

/** @brief Return the ACLK-equivalent low-frequency clock (32.768 kHz). */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void)  { return 32768UL; }

/** @brief Report whether the main clock has a fault (always 0). */
int           tiku_cpu_ambiq_clock_has_fault(void) { return 0; }
