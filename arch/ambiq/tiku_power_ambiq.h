/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_ambiq.h - Apollo510 power-measurement instruments.
 *
 * The timebase is the always-on STIMER, which survives WFI where SysTick does not.
 * The cache is the M55's architectural L1 and its geometry is READ from
 * CLIDR/CCSIDR, never assumed.  Instruments, not an API: each restores what it changed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_POWER_AMBIQ_H_
#define TIKU_POWER_AMBIQ_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TIMEBASE                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Voted read of the always-on STIMER counter (32.768 kHz). */
uint32_t tiku_ambiq_stimer_now(void);

/**
 * @brief Convert STIMER counts to microseconds, exactly and without floats.
 *
 * 1e6/32768 = 15625/512 is exact, so the conversion introduces no rounding
 * beyond the counter's own 30.5 us tick.
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
 * @brief Report L1 cache geometry as the silicon describes it.
 *
 * Reads CLIDR/CCSIDR rather than trusting a datasheet transcription, because
 * every working-set size in the memory experiment is chosen relative to this.
 *
 * @param i_bytes  Out: I-cache size in bytes (0 if absent).
 * @param d_bytes  Out: D-cache size in bytes (0 if absent).
 * @param line     Out: line length in bytes.
 */
void tiku_ambiq_cache_geometry(uint32_t *i_bytes, uint32_t *d_bytes,
                               uint32_t *line);

/*---------------------------------------------------------------------------*/
/* CLOCK ORACLE                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Measure the core clock in Hz by timing SysTick against the STIMER.
 *
 * Blocks ~50 ms.  Returns 0 if the window did not elapse.
 */
unsigned long tiku_ambiq_cpu_hz_measure(void);

/*---------------------------------------------------------------------------*/
/* PROBES                                                                    */
/*---------------------------------------------------------------------------*/

/** Release bits for tiku_ambiq_sleep_probe().  Deliberately a SHORTER list
 *  than the Nordic port's: each is added only once its effect is measured on
 *  this part.  `quiet` does not exist here on purpose -- that word's meaning is
 *  frozen by published nRF54L experiments. */
#define TIKU_AMBIQ_SLEEP_DEEP  0x1u   /**< WFI with SCR.SLEEPDEEP set */
#define TIKU_AMBIQ_SLEEP_STOP_UART 0x2u /**< power the console UART1 domain off
                                             for the window; its clock request
                                             is what keeps HFRC from gating in
                                             deep sleep.  Restored after */
#define TIKU_AMBIQ_SLEEP_STOP_TICK 0x4u /**< stretch the 128 Hz kernel tick
                                             across the window via the tickless
                                             path -- ~1 wake instead of 128/s */
#define TIKU_AMBIQ_SLEEP_DBGLOCK   0x8u /**< write MCUCTRL.DEBUGGER lockout for
                                             the window.  An attached probe's
                                             latched power request may ignore it,
                                             which is what this measures */
#define TIKU_AMBIQ_SLEEP_LFRC     0x10u /**< reclock the STIMER timebase to the
                                             ~900 Hz LFRC for the window: the
                                             crystal dies under real deep sleep.
                                             Verified switch, degrading to XTAL */

/**
 * @brief Sit in WFI for @p ms; returns elapsed microseconds (STIMER-timed).
 *
 * The wake count is published separately: a WFI that returns immediately is not
 * sleeping, and from the outside that is indistinguishable from one that is.
 */
uint32_t tiku_ambiq_sleep_probe(uint32_t ms, unsigned flags);

/** @brief WFI returns during the last sleep probe. */
uint32_t tiku_ambiq_sleep_wake_count(void);

/**
 * @brief Run a register-only busy loop for @p ms; returns elapsed microseconds.
 *
 * The reference workload, matched to the Nordic port's so the two parts can be
 * compared on identical work.  Alignment-pinned: on the other platform an
 * unrelated build option moved this loop and shifted its current by 956 uA.
 */
uint32_t tiku_ambiq_spin_probe(uint32_t ms);

/** @brief Outer passes retired by the last busy probe, and iterations per pass. */
uint32_t tiku_ambiq_spin_pass_count(void);
uint32_t tiku_ambiq_spin_inner(void);

/*---------------------------------------------------------------------------*/
/* MEMORY-ACCESS WORKLOADS                                                   */
/*---------------------------------------------------------------------------*/

#define TIKU_AMBIQ_MEM_NOP          0u
#define TIKU_AMBIQ_MEM_SRAM_R       1u
#define TIKU_AMBIQ_MEM_SRAM_W       2u
#define TIKU_AMBIQ_MEM_SRAM_STRIDE  3u
#define TIKU_AMBIQ_MEM_MRAM_HOT     4u   /**< small set: cache-resident   */
#define TIKU_AMBIQ_MEM_MRAM_COLD    5u   /**< large set + stride: misses  */
#define TIKU_AMBIQ_MEM_KIND_COUNT   6u

/**
 * @brief Run a memory workload for @p ms; returns microseconds (STIMER-timed).
 *
 * The MRAM counterpart of the nRF54L's RRAM sweep, so "what does one access
 * cost" can be answered on two different non-volatile technologies with one
 * method.  Access count is the denominator for energy per access.
 */
uint32_t tiku_ambiq_mem_probe(unsigned kind, uint32_t ms);

/** @brief Accesses retired by the last memory probe. */
uint32_t tiku_ambiq_mem_access_count(void);

/** @brief Checksum of the traversal (live for SRAM kinds; see the .c note). */
uint32_t tiku_ambiq_mem_checksum(void);

/** @brief Working-set sizes actually compiled in, so a report can state them. */
uint32_t tiku_ambiq_mem_hot_bytes(void);
uint32_t tiku_ambiq_mem_cold_bytes(void);

/*---------------------------------------------------------------------------*/
/* FLOOR DUMP                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console-free deep-sleep staircase; never returns.
 *
 * spin 3 s / idle 10 s / deep-sleep 45 s, forever, after a one-time tidy
 * (buck + crypto/OTP/NVM1/ROM/TRCENA).  Built for the J16-unplugged
 * measurement where the trace itself is the report.
 */
void tiku_ambiq_power_autorun(void);

/**
 * @brief Non-zero if a debugger is attached (MCUCTRL.DEBUGGER).
 *
 * Worth its own function because on the other platform a forgotten debug
 * session cost ~130 uA and silently made every low-power figure an upper bound.
 */
int tiku_ambiq_debugger_attached(void);

#endif /* TIKU_POWER_AMBIQ_H_ */
