/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_arch.h - nRF54L power and performance configuration.
 *
 * Two enable bits, the instruction/data cache and the DC/DC converter, which
 * between them account for most of the measured idle-current gap.  Kept out of the
 * clock file deliberately: clock setup has an ordering constraint and these do not.
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
 * @brief Apply the port's power and performance configuration.
 *
 * Enables the instruction/data cache and, if the board can support it, the
 * DC/DC converter.  Safe to call once at boot; both settings are idempotent.
 */
void tiku_nordic_power_boot_init(void);

/*---------------------------------------------------------------------------*/
/* CACHE                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enable or disable the instruction/data cache.
 *
 * Unlike the core frequency, the datasheet explicitly sanctions changing this
 * at run time, which is what lets the power suite measure the same workload
 * both ways on one boot.  Disabling invalidates first.
 *
 * @param on  Non-zero to enable.
 */
void tiku_nordic_cache_set(int on);

/** @brief Non-zero if the cache is currently enabled. */
int tiku_nordic_cache_enabled(void);

/**
 * @brief Start counting cache hits and misses from zero.
 *
 * The counters are what make "the cache is on" a measurement rather than an
 * assertion: a build that enables it and then never hits proves nothing.
 */
void tiku_nordic_cache_profile_start(void);

/**
 * @brief Read the profiling counters.
 *
 * Any pointer may be NULL.  Values are cumulative since the last
 * tiku_nordic_cache_profile_start().
 */
void tiku_nordic_cache_profile_read(uint32_t *hits, uint32_t *misses,
                                    uint32_t *reads, uint32_t *writes);

/*---------------------------------------------------------------------------*/
/* CLOCK ORACLE                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Measure the core clock in Hz by timing SysTick against the GRTC.
 *
 * Independent of PLL.CURRENTFREQ: this reports what the core is actually
 * doing, not what the clock register claims.  Blocks for ~50 ms.
 *
 * @return Measured core frequency in Hz, or 0 if the window did not elapse.
 */
unsigned long tiku_nordic_cpu_hz_measure(void);

/**
 * @brief Run a fixed, cache-sensitive workload and report how long it took.
 *
 * The cache cannot be measured on an idle board: the scheduler's idle path is a
 * handful of instructions that sit in any prefetch buffer, so an 8 KB cache
 * moves its power by nothing measurable (3.031 mA vs 3.044 mA, i.e. noise).
 *
 * @note The workload walks a large const array in NVM with a stride that
 *       defeats sequential prefetch, so it is dominated by fetch latency rather
 *       than ALU work -- exactly the axis the cache moves.
 * @param out_us  Elapsed microseconds (GRTC-timed, so clock-independent).
 * @return A checksum of the traversal, returned so the compiler cannot
 *         optimise the work away and so a caller can confirm the SAME work was
 *         done in every configuration being compared.
 */
uint32_t tiku_nordic_cache_workload(uint32_t *out_us);

/*---------------------------------------------------------------------------*/
/* MEMORY-ACCESS WORKLOADS                                                   */
/*---------------------------------------------------------------------------*/

/* Kinds for tiku_nordic_mem_probe().  Each is a tight, alignment-pinned,
 * access-counted loop; NOP is the register-only reference so "an access" can be
 * priced against "a register operation" on one basis. */
#define TIKU_MEM_KIND_NOP          0u
#define TIKU_MEM_KIND_SRAM_R       1u
#define TIKU_MEM_KIND_SRAM_W       2u
#define TIKU_MEM_KIND_SRAM_STRIDE  3u
#define TIKU_MEM_KIND_RRAM_HOT     4u   /* 4 KB set: inside the 8 KB cache   */
#define TIKU_MEM_KIND_RRAM_COLD    5u   /* 64 KB + stride: defeats the cache */
#define TIKU_MEM_KIND_COUNT        6u

/**
 * @brief Run a memory workload for @p ms; returns elapsed microseconds (GRTC).
 *
 * Every core figure before this had no memory traffic in it: the reference loop
 * that priced the core is two register instructions, while real code loads and
 * stores and a durability decision writes NVM.  These loops price each.
 */
uint32_t tiku_nordic_mem_probe(unsigned kind, uint32_t ms);

/** @brief Accesses retired by the last memory probe (the work denominator). */
uint32_t tiku_nordic_mem_access_count(void);

/**
 * @brief Checksum of the last probe's traversal.
 *
 * Live for the SRAM kinds, whose buffer is seeded with a pattern.
 * STRUCTURALLY ZERO for the RRAM kinds, whose arrays are `const` zero-filled,
 * so for those it is not evidence of anything and must not be read as such.
 *
 * @note The access COUNT is the denominator that matters, and nop landing on
 *       its architectural 3 cycles/iteration is what validates the accounting.
 */
uint32_t tiku_nordic_mem_checksum(void);

/*---------------------------------------------------------------------------*/
/* SLEEP FLOOR PROBE                                                         */
/*---------------------------------------------------------------------------*/

/** Things this port leaves running that a truly idle part would not. */
#define TIKU_SLEEP_STOP_PLL   0x1u   /**< release the pinned core PLL       */
#define TIKU_SLEEP_STOP_UART  0x2u   /**< disable the console UARTE         */
#define TIKU_SLEEP_STOP_HFXO  0x4u   /**< stop the 32 MHz crystal           */
#define TIKU_SLEEP_DEEP       0x8u   /**< WFI with SCR.SLEEPDEEP set        */
#define TIKU_SLEEP_STOP_TIM   0x10u  /**< stop the free-running htimer TIMER20 */
#define TIKU_SLEEP_STOP_TICK  0x20u  /**< stretch the 128 Hz kernel tick across
                                          the whole window (tickless path), so the
                                          CPU takes ~1 wake instead of 128/s.  NOT
                                          part of `quiet`, whose meaning is frozen */
#define TIKU_SLEEP_STOP_SYSC  0x40u  /**< drop GRTC MODE.SYSCOUNTEREN for the
                                          window (AUTOEN stays), letting the 1 MHz
                                          SYSCOUNTER sleep with the CPUs.  Needs
                                          LFCLK running ('power lfclk') to wake */

/**
 * @brief Sit in WFI for @p ms, optionally shutting down what holds HFCLK up.
 *
 * This port's WFI idle measured 953 uA against a datasheet System ON IDLE
 * figure of 3.0 uA: halting the core is not the same as letting the part sleep,
 * because HFCLK only stops when NOTHING requests it.
 *
 * @note This port holds at least two standing requests -- an enabled console
 *       UARTE and a core PLL pinned on by the erratum-39 workaround, which
 *       starts the PLL at boot and never issues the paired stop.  Each flag
 *       releases one so the 953 uA can be attributed rather than guessed at;
 *       everything is restored before returning.  A measurement instrument,
 *       NOT a power-management API.
 * @param ms     Duration to stay in WFI.
 * @param flags  TIKU_SLEEP_STOP_* bits.
 * @return Actual elapsed microseconds (GRTC-timed).
 */
uint32_t tiku_nordic_sleep_probe(uint32_t ms, unsigned flags);

/** @brief How many times WFI returned during the last sleep probe. */
uint32_t tiku_nordic_sleep_wake_count(void);

/**
 * @brief Outer passes retired by the last busy probe, and iterations per pass.
 *
 * Current measured over a fixed WINDOW says nothing about efficiency alone: a
 * configuration that draws less may simply have executed less.  Passes x
 * iterations-per-pass is the denominator that turns milliamps into energy.
 */
uint32_t tiku_nordic_spin_pass_count(void);

#if (TIKU_FLPR_ENABLE + 0)
/**
 * @brief Coprocessor passes retired inside the last probe window.
 *
 * Sampled on-chip at the window edges so the figure carries no host round-trip
 * error -- it is the denominator for the coprocessor's energy per unit work.
 */
uint32_t tiku_nordic_flpr_pass_delta(void);
#endif
uint32_t tiku_nordic_spin_inner(void);

/**
 * @brief Non-zero if a debugger has halting debug enabled (DHCSR.C_DEBUGEN).
 *
 * The datasheet's low-power figures apply to NORMAL mode only (9.3), and a
 * flashing flow that ends with a SYSTEM reset never leaves debug interface
 * mode -- so every current measurement in that state is an upper bound.
 */
int tiku_nordic_debug_attached(void);

/**
 * @brief Spin the core in a tight two-instruction loop for @p ms.
 *
 * The `while(1){}` reference point: measured against the WFI floor with
 * everything else identical, the difference is the core's own dynamic cost,
 * with no workload, memory traffic or peripheral activity mixed in.
 *
 * @note The loop is `subs`/`bne` in inline asm, so no optimiser decision stands
 *       between the source and what the core executes.  The GRTC is read once
 *       per 4096 iterations, keeping the peripheral bus below ~0.1% of the
 *       window.  Takes the SAME TIKU_SLEEP_STOP_* flags as the sleep probe and
 *       releases the same things, so busy and idle stay comparable.
 * @param ms     Duration to spin.
 * @param flags  TIKU_SLEEP_STOP_* bits.
 * @return Actual elapsed microseconds (GRTC-timed).
 */
uint32_t tiku_nordic_spin_probe(uint32_t ms, unsigned flags);

/**
 * @brief Enter System OFF.  Does not return; wake is by reset or GPIO DETECT.
 *
 * The control experiment for the idle-floor hunt: the part specs ~uA here, so
 * any current still measured on the rail with the SoC in System OFF belongs to
 * the board or the debug domain, and no firmware change can remove it.
 */
void tiku_nordic_system_off(void) __attribute__((noreturn));

/*---------------------------------------------------------------------------*/
/* SUPPLY                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Inductor detection status, as the hardware currently reports it.
 *
 * Detection only runs while the converter is OFF (datasheet 5.7.2.4.2), so
 * this cannot answer while it is on.
 *
 * @return 1 detected, 0 not detected, -1 cannot tell (converter enabled) --
 *         use tiku_nordic_dcdc_probe_inductor() for a definite answer.
 */
int tiku_nordic_dcdc_inductor_present(void);

/**
 * @brief Definitively determine whether an inductor is fitted.
 *
 * Momentarily drops to LDO if needed so the detector can run, then restores
 * the previous converter state.  Costs a brief LDO window; gives an answer
 * that does not depend on when it was asked.
 *
 * @return 1 if an inductor is detected, 0 otherwise.
 */
int tiku_nordic_dcdc_probe_inductor(void);

/** @brief Non-zero if the DC/DC converter is currently enabled. */
int tiku_nordic_dcdc_enabled(void);

/**
 * @brief Enable or disable the DC/DC converter.
 *
 * No software precondition: the silicon checks for the inductor as part of
 * enabling and stays in LDO mode if there is none, which is both safer and
 * better timed than anything this layer could do.
 *
 * @param on  Non-zero to enable.
 * @return Non-zero if the converter is enabled on return.
 */
int tiku_nordic_dcdc_set(int on);

#endif /* TIKU_NORDIC_POWER_ARCH_H_ */
