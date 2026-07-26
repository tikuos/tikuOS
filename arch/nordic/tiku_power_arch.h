/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_power_arch.h - nRF54L power and performance configuration.
 *
 * Two registers this port had never written, both found by measuring the board
 * against its own datasheet: the instruction/data cache, and the DC/DC
 * converter.  Neither is subtle -- each is one enable bit -- and between them
 * they account for most of the gap between the datasheet's 2.6 mA running
 * CoreMark and the 6.76 mA this port measured doing nothing.
 *
 * Kept out of the clock file on purpose.  Clock setup has a hard ordering
 * constraint (the core frequency may only be chosen before any peripheral
 * requests the HF clock); these two do not, and mixing them would invite
 * someone to "simplify" the ordering later.
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
 * at run time ("Ability to enable/disable cache at run-time"), which is what
 * lets the power suite measure the same workload both ways on one boot.
 *
 * Disabling invalidates first, so a later re-enable cannot serve stale lines.
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
 * Exists because the cache cannot be measured on an idle board: the scheduler's
 * idle path is a handful of instructions that sit in any prefetch buffer, so
 * enabling an 8 KB cache changes its power by nothing measurable (observed:
 * 3.031 mA vs 3.044 mA, i.e. noise).  A meaningful cache measurement needs a
 * working set bigger than a loop body, streamed out of NVM.
 *
 * The workload walks a large const array in NVM with a stride that defeats
 * sequential prefetch, so it is dominated by fetch latency rather than by ALU
 * work -- which is exactly the axis the cache moves.
 *
 * @param out_us  Elapsed microseconds (GRTC-timed, so clock-independent).
 * @return A checksum of the traversal, returned so the compiler cannot
 *         optimise the work away and so a caller can confirm the SAME work was
 *         done in every configuration being compared.
 */
uint32_t tiku_nordic_cache_workload(uint32_t *out_us);

/*---------------------------------------------------------------------------*/
/* SLEEP FLOOR PROBE                                                         */
/*---------------------------------------------------------------------------*/

/** Things this port leaves running that a truly idle part would not. */
#define TIKU_SLEEP_STOP_PLL   0x1u   /**< release the pinned core PLL       */
#define TIKU_SLEEP_STOP_UART  0x2u   /**< disable the console UARTE         */
#define TIKU_SLEEP_STOP_HFXO  0x4u   /**< stop the 32 MHz crystal           */
#define TIKU_SLEEP_DEEP       0x8u   /**< WFI with SCR.SLEEPDEEP set        */
#define TIKU_SLEEP_STOP_TIM   0x10u  /**< stop the free-running htimer TIMER20 */

/**
 * @brief Sit in WFI for @p ms, optionally shutting down what holds HFCLK up.
 *
 * EXISTS BECAUSE "IDLE" WAS OFF BY A FACTOR OF 300.  This port's WFI idle
 * measured 953 uA against a datasheet System ON IDLE figure of 3.0 uA (GRTC on
 * XOSC, 256 KB RAM).  Halting the core is not the same as letting the part
 * sleep: the HFCLK controller only stops the clock when NOTHING requests it,
 * and this port holds at least two standing requests -- an enabled console
 * UARTE, and a core PLL pinned on by the erratum-39 workaround, which triggers
 * TASKS_PLLSTART at boot and never issues the paired PLLSTOP.
 *
 * Each flag releases one of those so the 953 uA can be attributed rather than
 * guessed at.  Everything is restored before returning, including the console,
 * so the caller can still report.
 *
 * NOT A POWER-MANAGEMENT API.  This is a measurement instrument: it tells you
 * what a real low-power path would have to do, and what each piece is worth.
 *
 * @param ms     Duration to stay in WFI.
 * @param flags  TIKU_SLEEP_STOP_* bits.
 * @return Actual elapsed microseconds (GRTC-timed).
 */
uint32_t tiku_nordic_sleep_probe(uint32_t ms, unsigned flags);

/** @brief How many times WFI returned during the last sleep probe. */
uint32_t tiku_nordic_sleep_wake_count(void);

/**
 * @brief Non-zero if a debugger has halting debug enabled (DHCSR.C_DEBUGEN).
 *
 * The datasheet's low-power figures apply to NORMAL mode only: "when a debug
 * session is over, the device must be set to Normal mode by the external
 * debugger, followed by a pin reset" (9.3).  A flashing flow that ends with a
 * SYSTEM reset never leaves debug interface mode, and every current
 * measurement taken in that state is an upper bound, not a figure.
 */
int tiku_nordic_debug_attached(void);

/**
 * @brief Spin the core in a tight two-instruction loop for @p ms.
 *
 * The `while(1){}` reference point.  Measured against the WFI floor with
 * everything else identical, the difference is the core's own dynamic cost --
 * i.e. what the CPU clock is worth -- with no workload, no memory traffic and
 * no peripheral activity mixed in.
 *
 * The loop is `subs`/`bne` in inline asm rather than C, so no optimiser
 * decision stands between the source and what the core executes: an empty C
 * `while(1)` compiles to a single backward branch, and a C loop with a counter
 * may or may not survive -O2 intact.  The GRTC is read once per 4096
 * iterations, keeping the peripheral bus below ~0.1% of the window so what is
 * measured is the core, not the bus.
 *
 * @param ms  Duration to spin.
 * @return Actual elapsed microseconds (GRTC-timed).
 */
uint32_t tiku_nordic_spin_probe(uint32_t ms);

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
