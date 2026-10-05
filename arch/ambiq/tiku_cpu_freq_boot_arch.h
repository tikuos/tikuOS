/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - Ambiq CPU boot, clock and cache backends.
 *
 * Dispatched by hal/tiku_cpu.c; implemented in tiku_cpu_freq_boot_arch.c
 * (Apollo510) and tiku_cpu_freq_boot_apollo4l.c (Apollo4).  The HP probe,
 * buck and clock-measurement calls exist on Apollo510 only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_

#include <stdint.h>

/**
 * @brief One-time CPU bring-up: enable the caches and read the core clock.
 *
 * On Apollo510 it also enables the prefetch unit and powers down the
 * CryptoCell, OTP reader and trace unit.  The clock tree is left as booted.
 *
 * @note Called once, early in boot, before other subsystems start.
 */
void tiku_cpu_boot_ambiq_init(void);

/**
 * @brief Select the core's perf mode from a requested frequency.
 *
 * Above 96 MHz asks for high-performance mode (250 MHz on Apollo510, 192 MHz
 * on Apollo4), anything else for low-power mode (96 MHz).  A refused request
 * leaves the core in LP; tiku_cpu_ambiq_clock_get_hz() reports the result.
 *
 * @param cpu_freq  Requested core frequency in MHz (MAIN_CPU_FREQ at boot).
 */
void tiku_cpu_freq_ambiq_init(unsigned int cpu_freq);

/**
 * @brief Idle the core with WFI until the next enabled interrupt.
 *
 * The STIMER kernel tick, console UART RX and any other enabled IRQ wake it.
 */
void tiku_cpu_boot_ambiq_power_wfi_enter(void);

/**
 * @brief Query the current core (MCLK) frequency.
 *
 * Read from the perf-mode status after the last mode change.  Busy delays
 * scale by it and /sys/cpu/freq reports it.
 *
 * @return Core clock frequency in Hz.
 */
unsigned long tiku_cpu_ambiq_clock_get_hz(void);

/**
 * @brief SMCLK-equivalent clock query; this port has no separate SMCLK.
 *
 * @return The core clock frequency in Hz, as tiku_cpu_ambiq_clock_get_hz().
 */
unsigned long tiku_cpu_ambiq_smclk_get_hz(void);

/**
 * @brief ACLK-equivalent clock query: the 32.768 kHz crystal that clocks
 *        STIMER.
 *
 * @return 32768.
 */
unsigned long tiku_cpu_ambiq_aclk_get_hz(void);

/**
 * @brief Report a clock fault; this port does no oscillator-fault detection.
 *
 * @return 0.
 */
int           tiku_cpu_ambiq_clock_has_fault(void);

/**
 * @brief Write cached data in [addr, addr+len) back to memory.
 *
 * Apollo510 cleans the range by address.  On Apollo4 it does nothing: the
 * CACHECTRL cache holds only MRAM data the CPU reads.
 */
void          tiku_cpu_ambiq_dcache_clean(const void *addr, unsigned long len);

/**
 * @brief Drop cached copies of [addr, addr+len) so the next read comes from
 *        memory.
 *
 * Apollo510 invalidates the range by address.  Apollo4 ignores @p addr and
 * @p len and invalidates its whole CACHECTRL cache, then issues DSB/ISB.
 *
 * @note Call it after something other than the CPU (the boot ROM's MRAM
 *       programmer, the GPU, DMA) writes the range, before the CPU reads it.
 * @param addr  Start of the range whose cached copies must be dropped.
 * @param len   Length of the range in bytes.
 */
void          tiku_cpu_ambiq_dcache_invalidate(const void *addr, unsigned long len);

/**
 * @brief Invalidate the instruction cache (whole cache).
 *
 * Apollo510 writes ICIALLU with barriers on both sides; Apollo4 invalidates
 * its unified CACHECTRL cache.  Needed after code is written behind the CPU.
 */
void          tiku_cpu_ambiq_icache_invalidate(void);

/**
 * @brief Apollo510 identity and power snapshot for `freq probe`.
 *
 * What the high-performance mode decision reads: silicon revision, INFO1
 * residency, factory trim revisions, SIMOBUCK and perf-mode state, and the
 * SPOT-manager POWERSTATE trim table.
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

    /* VDDF plan: the boost this port computes for a TrimSubRev-0x5F part,
     * the trims it derives, and the trim the hardware runs.  Reading them
     * changes no voltage. */
    uint32_t vddf_ltrim;       /**< INFO1 L_TRIMCODE, raw (0 = not loaded)    */
    uint32_t vddf_etrim;       /**< INFO1 E_TRIMCODE, raw                     */
    uint32_t vddf_mv_x10;      /**< the formula's mV boost, x10 (no floats)   */
    uint8_t  vddf_boost_codes; /**< boost converted to trim codes             */
    uint8_t  vddf_ps5_raw;     /**< TVRGF(state 5), before the boost          */
    uint8_t  vddf_ps13_raw;    /**< TVRGF(state 13), before the boost         */
    uint8_t  vddf_lp;          /**< planned LP trim, boosted + clamped        */
    uint8_t  vddf_hp;          /**< planned HP trim, boosted + clamped        */
    uint8_t  vddf_clamped;     /**< 1 = the [0x8,0x7F] clamp cut a trim       */
    uint8_t  vddf_applied;     /**< MCUCTRL.VREFGEN4.TVRGFVREFTRIM as read    */
    uint8_t  vddf_plan_ok;     /**< 1 = the plan above has been computed      */

    /* Raw regulator and buck registers, undecoded, for comparing LP and HP
     * on the host. */
    uint32_t r_vrefgen2;       /**< MCUCTRL->VREFGEN2 (TVRGC = VDDC ref)      */
    uint32_t r_vrefgen3;       /**< MCUCTRL->VREFGEN3 (TVRGCLV)               */
    uint32_t r_vrefgen4;       /**< MCUCTRL->VREFGEN4 (TVRGF = VDDF ref)      */
    uint32_t r_ldoreg1;        /**< MCUCTRL->LDOREG1 (core LDO trims)         */
    uint32_t r_ldoreg2;        /**< MCUCTRL->LDOREG2 (mem LDO trims)          */
    uint32_t r_vrctrl;         /**< MCUCTRL->VRCTRL (override bits)           */
    uint32_t r_d2aspare;       /**< MCUCTRL->D2ASPARE (MEMLDOREF)             */
    uint32_t r_sb[6];          /**< SIMOBUCK 0,2,4,6,7,15 (comp en + Ton)     */
} tiku_ambiq_hp_probe_t;

/**
 * @brief Fill @p out with the HP identity and power snapshot (Apollo510).
 *
 * Reads INFO1 from its MRAM shadow, or from OTP when OTP holds the current
 * copy, powering OTP on for the read and restoring it after.  Changes no
 * perf mode and no voltage.
 */
void tiku_cpu_freq_ambiq_hp_probe(tiku_ambiq_hp_probe_t *out);

/**
 * @brief Enable the SIMO buck without changing the perf mode (Apollo510).
 *
 * Loads the INFO1 trims first if needed.
 *
 * @return 0 when VRSTATUS reports ACT, -1 if the trims are unusable or the
 *         buck does not reach ACT.
 */
int tiku_cpu_freq_ambiq_simobuck_enable(void);

/**
 * @brief Measure the core clock against the 32.768 kHz STIMER (Apollo510).
 *
 * Counts SysTick (CLKSOURCE = processor) over 125 ms, independent of the
 * perf-mode status, so it shows whether an LP/HP switch took effect.
 *
 * @note Blocks for 125 ms.
 * @return Measured core clock in Hz (0 if SysTick is not configured).
 */
unsigned long tiku_cpu_freq_ambiq_measured_hz(void);

#endif /* TIKU_AMBIQ_CPU_FREQ_BOOT_ARCH_H_ */
