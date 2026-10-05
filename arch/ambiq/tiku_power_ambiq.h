/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_ambiq.h - Apollo510 power-measurement instruments.
 *
 * Probes time their windows on the always-on STIMER, which keeps counting
 * through WFI.  The cache controls drive the M55's architectural L1, whose
 * geometry is read from CCSIDR.  A sleep probe reverses each flag it applies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_POWER_AMBIQ_H_
#define TIKU_POWER_AMBIQ_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TIMEBASE                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Voted read of the STIMER counter (32.768 kHz on the crystal). */
uint32_t tiku_ambiq_stimer_now(void);

/**
 * @brief Convert STIMER counts to whole microseconds in integer arithmetic.
 *
 * On the crystal the factor 1e6/32768 = 15625/512 is exact and the result is
 * truncated.  Once tiku_ambiq_power_autorun() has moved the STIMER to the
 * LFRC, the conversion uses the 900 Hz nominal rate.
 */
uint32_t tiku_ambiq_stimer_us(uint32_t counts);

/*---------------------------------------------------------------------------*/
/* CACHE (Cortex-M55 architectural L1)                                       */
/*---------------------------------------------------------------------------*/

/** @brief Enable or disable both L1 caches (I and D), with maintenance. */
void tiku_ambiq_cache_set(int on);

/** @brief Non-zero if the L1 instruction cache is enabled (SCB.CCR.IC). */
int tiku_ambiq_cache_enabled(void);

/**
 * @brief Report the L1 cache geometry read from CCSIDR.
 *
 * Any output pointer may be NULL.  CSSELR is restored on return.
 *
 * @param i_bytes  Out: I-cache size in bytes, sets x ways x line.
 * @param d_bytes  Out: D-cache size in bytes, sets x ways x line.
 * @param line     Out: line length in bytes, of the I-cache.
 */
void tiku_ambiq_cache_geometry(uint32_t *i_bytes, uint32_t *d_bytes,
                               uint32_t *line);

/*---------------------------------------------------------------------------*/
/* CORE CLOCK MEASUREMENT                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Measure the core clock in Hz by counting DWT CYCCNT cycles over a
 *        ~16 ms STIMER window.
 *
 * Leaves DEMCR.TRCENA and the cycle counter on.
 *
 * @note Spins until the window elapses, with no bound: a stopped STIMER
 *       hangs the call.
 * @return Core clock in Hz, or 0 if CYCCNT does not advance
 */
unsigned long tiku_ambiq_cpu_hz_measure(void);

/*---------------------------------------------------------------------------*/
/* PROBES                                                                    */
/*---------------------------------------------------------------------------*/

/* Flags for tiku_ambiq_sleep_probe(), OR'd together.  The probe applies
 * each one for the window and reverses it before returning. */
#define TIKU_AMBIQ_SLEEP_DEEP  0x1u   /**< WFI with SCR.SLEEPDEEP set */
/** @brief Power the console UART1 domain off for the window: its clock
 *         request keeps HFRC running in deep sleep. */
#define TIKU_AMBIQ_SLEEP_STOP_UART 0x2u
/** @brief Stretch the kernel tick past the end of the window through the
 *         tickless path. */
#define TIKU_AMBIQ_SLEEP_STOP_TICK 0x4u
/** @brief Set the MCUCTRL.DEBUGGER lockout for the window and clear it after;
 *         an attached probe's latched power request may override it. */
#define TIKU_AMBIQ_SLEEP_DBGLOCK   0x8u
/** @brief Time the window on the ~900 Hz LFRC, which keeps running when deep
 *         sleep stops the 32 kHz crystal; if the LFRC is not seen counting,
 *         the window is timed on the crystal. */
#define TIKU_AMBIQ_SLEEP_LFRC     0x10u

/**
 * @brief Sit in WFI for @p ms; returns elapsed microseconds (STIMER-timed).
 *
 * The window ends early if the STIMER stops counting.  The number of WFI
 * returns is kept for tiku_ambiq_sleep_wake_count().
 *
 * @param ms     Window length in milliseconds
 * @param flags  TIKU_AMBIQ_SLEEP_* flags, OR'd together
 */
uint32_t tiku_ambiq_sleep_probe(uint32_t ms, unsigned flags);

/** @brief WFI returns in the last probe window (0 after a spin probe). */
uint32_t tiku_ambiq_sleep_wake_count(void);

/**
 * @brief Run a register-only busy loop for @p ms; returns elapsed microseconds.
 *
 * Each pass is tiku_ambiq_spin_inner() iterations of a two-instruction
 * subs/bne loop that sits in its own 16-byte-aligned section.
 */
uint32_t tiku_ambiq_spin_probe(uint32_t ms);

/** @brief Passes retired in the last probe window (0 after a sleep probe). */
uint32_t tiku_ambiq_spin_pass_count(void);
/** @brief Loop iterations in one spin-probe pass. */
uint32_t tiku_ambiq_spin_inner(void);

/*---------------------------------------------------------------------------*/
/* MEMORY-ACCESS WORKLOADS                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Workload kinds for tiku_ambiq_mem_probe(). */
#define TIKU_AMBIQ_MEM_NOP          0u   /**< spin passes, no memory      */
#define TIKU_AMBIQ_MEM_SRAM_R       1u   /**< DTCM reads, sequential      */
#define TIKU_AMBIQ_MEM_SRAM_W       2u   /**< DTCM writes, sequential     */
#define TIKU_AMBIQ_MEM_SRAM_STRIDE  3u   /**< DTCM reads, 17-word stride  */
#define TIKU_AMBIQ_MEM_MRAM_HOT     4u   /**< small set: cache-resident   */
#define TIKU_AMBIQ_MEM_MRAM_COLD    5u   /**< large set + stride: misses  */
#define TIKU_AMBIQ_MEM_KIND_COUNT   6u   /**< number of kinds             */

/**
 * @brief Run memory workload @p kind for @p ms; returns elapsed microseconds.
 *
 * Every pass counts 256 accesses toward tiku_ambiq_mem_access_count().  An
 * unknown @p kind runs the NOP workload.
 */
uint32_t tiku_ambiq_mem_probe(unsigned kind, uint32_t ms);

/** @brief Accesses retired by the last memory probe. */
uint32_t tiku_ambiq_mem_access_count(void);

/** @brief Checksum of the last memory probe; 0 for the MRAM and NOP kinds. */
uint32_t tiku_ambiq_mem_checksum(void);

/** @brief Size in bytes of the MRAM_HOT working set. */
uint32_t tiku_ambiq_mem_hot_bytes(void);
/** @brief Size in bytes of the MRAM_COLD and SRAM working sets. */
uint32_t tiku_ambiq_mem_cold_bytes(void);

/*---------------------------------------------------------------------------*/
/* DEEP-SLEEP AUTORUN AND DEBUGGER STATE                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Run the console-free deep-sleep sequence; never returns.
 *
 * After one-time power-down steps (SIMOBUCK on; crypto, OTP, NVM1, ROM and
 * trace off) it loops: spin 3 s, WFI 8 s, deep sleep 30 s, spin 2 s,
 * tick-stretched deep sleep 20 s.
 *
 * @note Runs in place of the scheduler.  Real deep sleep needs the J-Link
 *       (J16) unplugged: while it powers the debug domain, SLEEPDEEP acts as
 *       plain sleep.
 */
void tiku_ambiq_power_autorun(void);

/**
 * @brief Non-zero if the SWD lockout, bit 0 of MCUCTRL.DEBUGGER, is clear.
 *
 * A clear lockout lets a debugger attach; it does not show that one is
 * connected.
 */
int tiku_ambiq_debugger_attached(void);

#endif /* TIKU_POWER_AMBIQ_H_ */
