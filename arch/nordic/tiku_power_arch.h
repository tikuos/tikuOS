/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_arch.h - nRF54L power and performance configuration.
 *
 * The cache and DC/DC enables, plus the probes the `power` shell command
 * drives: cache and memory workloads, sleep and spin probes, a core-clock
 * measurement and System OFF.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_POWER_ARCH_H_
#define TIKU_NORDIC_POWER_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* BOOT                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enable the cache and the DC/DC converter.
 *
 * TIKU_NORDIC_CACHE_DISABLE or TIKU_NORDIC_DCDC_DISABLE set to 1 skips one.
 * Without an inductor the part stays on its LDO.  Repeating the call is
 * harmless.
 */
void tiku_nordic_power_boot_init(void);

/*---------------------------------------------------------------------------*/
/* CACHE                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enable or disable the instruction/data cache at run time.
 *
 * Enabling invalidates the cache first; disabling invalidates it afterwards.
 *
 * @param on  Non-zero to enable.
 */
void tiku_nordic_cache_set(int on);

/** @brief Non-zero if the cache is currently enabled. */
int tiku_nordic_cache_enabled(void);

/**
 * @brief Enable the cache profiling counters and clear them.
 */
void tiku_nordic_cache_profile_start(void);

/**
 * @brief Read the cache profiling counters.
 *
 * Any pointer may be NULL.  Values are cumulative since the last
 * tiku_nordic_cache_profile_start().
 */
void tiku_nordic_cache_profile_read(uint32_t *hits, uint32_t *misses,
                                    uint32_t *reads, uint32_t *writes);

/*---------------------------------------------------------------------------*/
/* CORE CLOCK MEASUREMENT                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Measure the core clock in Hz by timing SysTick against the GRTC.
 *
 * Does not read PLL.CURRENTFREQ.  Blocks for 50 ms; SysTick's CTRL and LOAD
 * are restored afterwards.
 *
 * @return Measured core frequency in Hz, or 0 if no time elapsed.
 */
unsigned long tiku_nordic_cpu_hz_measure(void);

/**
 * @brief Run a fixed, cache-sensitive workload and report how long it took.
 *
 * Strided reads over a 16 KB const array in RRAM, twice the cache size, so
 * the time is dominated by NVM fetch latency.
 *
 * @param out_us  Out, optional: elapsed microseconds, GRTC-timed
 * @return A checksum of the traversal, the same in every configuration
 */
uint32_t tiku_nordic_cache_workload(uint32_t *out_us);

/*---------------------------------------------------------------------------*/
/* MEMORY-ACCESS WORKLOADS                                                   */
/*---------------------------------------------------------------------------*/

/* Workload kinds for tiku_nordic_mem_probe(): each is an aligned loop that
 * counts its accesses; NOP is a register-only loop counted the same way. */
#define TIKU_MEM_KIND_NOP          0u   /**< register-only loop             */
#define TIKU_MEM_KIND_SRAM_R       1u   /**< sequential SRAM reads          */
#define TIKU_MEM_KIND_SRAM_W       2u   /**< sequential SRAM writes         */
#define TIKU_MEM_KIND_SRAM_STRIDE  3u   /**< SRAM reads, 17-word stride     */
#define TIKU_MEM_KIND_RRAM_HOT     4u   /**< 4 KB RRAM set: inside the cache */
#define TIKU_MEM_KIND_RRAM_COLD    5u   /**< 64 KB RRAM, strided: misses it */
#define TIKU_MEM_KIND_COUNT        6u   /**< number of kinds                */

/**
 * @brief Run memory workload @p kind for @p ms; return elapsed microseconds.
 *
 * An unknown @p kind runs the NOP loop.  Timed on the GRTC; the access count
 * and checksum are read back separately.
 */
uint32_t tiku_nordic_mem_probe(unsigned kind, uint32_t ms);

/** @brief Accesses retired by the last memory probe. */
uint32_t tiku_nordic_mem_access_count(void);

/**
 * @brief Checksum of the last memory probe's traversal.
 *
 * Non-zero only for the SRAM kinds.  The RRAM arrays are const and
 * zero-filled, so the RRAM kinds and NOP always give 0.
 */
uint32_t tiku_nordic_mem_checksum(void);

/*---------------------------------------------------------------------------*/
/* SLEEP FLOOR PROBE                                                         */
/*---------------------------------------------------------------------------*/

/* Probe flags: each releases one source of wakes or of HFCLK requests. */
#define TIKU_SLEEP_STOP_PLL   0x1u   /**< release the pinned core PLL       */
#define TIKU_SLEEP_STOP_UART  0x2u   /**< disable the console UARTE         */
#define TIKU_SLEEP_STOP_HFXO  0x4u   /**< stop the 32 MHz crystal           */
#define TIKU_SLEEP_DEEP       0x8u   /**< WFI with SCR.SLEEPDEEP set        */
#define TIKU_SLEEP_STOP_TIM   0x10u  /**< stop the htimer's TIMER20         */
/** Stretch the kernel tick across the window (tickless path). */
#define TIKU_SLEEP_STOP_TICK  0x20u
/**
 * Clear GRTC MODE.SYSCOUNTEREN for the window (AUTOEN stays set), so the
 * 1 MHz SYSCOUNTER sleeps with the CPUs.  The LFCLK must be running to wake.
 */
#define TIKU_SLEEP_STOP_SYSC  0x40u

/**
 * @brief Sit in WFI for @p ms, optionally releasing what keeps HFCLK running.
 *
 * HFCLK stops only when nothing requests it.  The console UARTE and the core
 * PLL, which the erratum-39 workaround starts at boot, hold standing requests.
 *
 * @note A measurement probe that blocks the caller for @p ms, not a
 *       power-management API.  Every release is undone on return; the
 *       low-power sub-mode that TIKU_SLEEP_DEEP selects is kept.
 * @param ms     Duration to stay in WFI.
 * @param flags  TIKU_SLEEP_STOP_* bits.
 * @return Actual elapsed microseconds (GRTC-timed).
 */
uint32_t tiku_nordic_sleep_probe(uint32_t ms, unsigned flags);

/** @brief How many times WFI returned during the last sleep probe. */
uint32_t tiku_nordic_sleep_wake_count(void);

/**
 * @brief Outer passes retired by the last spin probe.
 *
 * Each pass is tiku_nordic_spin_inner() loop iterations.
 */
uint32_t tiku_nordic_spin_pass_count(void);

#if (TIKU_FLPR_ENABLE + 0)
/**
 * @brief FLPR spin passes retired inside the last probe window.
 *
 * Sampled on the M33 at the window edges.
 */
uint32_t tiku_nordic_flpr_pass_delta(void);
#endif
/** @brief Inner loop iterations per spin-probe pass. */
uint32_t tiku_nordic_spin_inner(void);

/**
 * @brief Non-zero if a debugger has halting debug enabled (DHCSR.C_DEBUGEN).
 *
 * The datasheet's low-power figures apply to normal mode only (9.3).  A flash
 * that ends in a system reset leaves the part in debug interface mode.
 */
int tiku_nordic_debug_attached(void);

/**
 * @brief Spin the core in a two-instruction loop for @p ms.
 *
 * The loop is a 16-byte-aligned inline-asm `subs`/`bne`; the GRTC is read
 * once per 4096 iterations.  @p flags release what tiku_nordic_sleep_probe()
 * releases, and the releases are undone on return.
 *
 * @param ms     Duration to spin.
 * @param flags  TIKU_SLEEP_STOP_* bits.
 * @return Actual elapsed microseconds (GRTC-timed).
 */
uint32_t tiku_nordic_spin_probe(uint32_t ms, unsigned flags);

/**
 * @brief Disarm the GRTC compares and enter System OFF; does not return.
 *
 * A System OFF wake (reset or GPIO DETECT) is a reset.
 */
void tiku_nordic_system_off(void) __attribute__((noreturn));

/*---------------------------------------------------------------------------*/
/* SUPPLY                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Inductor detection status, as the hardware currently reports it.
 *
 * Detection only runs while the converter is off (datasheet 5.7.2.4.2).
 *
 * @return 1 detected, 0 not detected, -1 cannot tell (converter enabled)
 * @see tiku_nordic_dcdc_probe_inductor()
 */
int tiku_nordic_dcdc_inductor_present(void);

/**
 * @brief Report whether an inductor is fitted.
 *
 * If the converter is on, it is turned off for the read (the part runs on its
 * LDO meanwhile) and turned back on.
 *
 * @return 1 if an inductor is detected, 0 otherwise.
 */
int tiku_nordic_dcdc_probe_inductor(void);

/** @brief Non-zero if the DC/DC converter is currently enabled. */
int tiku_nordic_dcdc_enabled(void);

/**
 * @brief Enable or disable the DC/DC converter.
 *
 * When enabling, the silicon checks for the inductor and stays in LDO mode if
 * there is none.
 *
 * @param on  Non-zero to enable.
 * @return Non-zero if DCDCEN reads set on return.
 */
int tiku_nordic_dcdc_set(int on);

#endif /* TIKU_NORDIC_POWER_ARCH_H_ */
