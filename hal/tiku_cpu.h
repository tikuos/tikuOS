/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.h - platform-agnostic CPU abstraction interface.
 *
 * Atomic section entry/exit, IRQ control, clock-rate queries and idle-mode entry.
 * Every function delegates to the active platform's arch implementation, which
 * each port supplies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CPU_H_
#define TIKU_CPU_H_

/*---------------------------------------------------------------------------*/
/* ATOMIC / IRQ CONTROL                                                      */
/*---------------------------------------------------------------------------*/

void tiku_atomic_enter(void); /* Enter nested atomic section */
void tiku_atomic_exit(void);  /* Exit nested atomic section */

/**
 * @brief Unconditionally enable global interrupts.
 *
 * Used by boot-time code that wants the scheduler's first run to
 * have IRQs on. Most kernel code should prefer the atomic_enter/
 * atomic_exit pair, which preserves caller GIE state.
 */
void tiku_cpu_irq_enable(void);

/**
 * @brief Unconditionally disable global interrupts.
 */
void tiku_cpu_irq_disable(void);

/*---------------------------------------------------------------------------*/
/* BOOT / FREQUENCY                                                          */
/*---------------------------------------------------------------------------*/

void tiku_cpu_boot_init(void);

/**
 * @brief Apply a core-clock frequency to the platform clock tree.
 *
 * Call after tiku_cpu_boot_init() and before anything timed off the core or
 * peripheral clock.  A request the part cannot honour is clamped or ignored
 * rather than failing, so a caller that cares must confirm with _mclk_hz().
 *
 * @param cpu_freq  Requested core frequency in MHz
 */
void tiku_cpu_freq_init(unsigned int cpu_freq);

/*---------------------------------------------------------------------------*/
/* CLOCK RATE QUERIES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Current main CPU clock frequency in Hz.
 *
 * MSP430 maps this to MCLK; other platforms map to their primary
 * core clock.
 */
unsigned long tiku_cpu_mclk_hz(void);

/**
 * @brief Current peripheral / sub-system clock frequency in Hz.
 *
 * MSP430 maps this to SMCLK. Platforms without a separate
 * peripheral clock should return the same value as tiku_cpu_mclk_hz().
 */
unsigned long tiku_cpu_smclk_hz(void);

/**
 * @brief Current low-power / always-on clock frequency in Hz.
 *
 * MSP430 maps this to ACLK (typ. 32.768 kHz crystal or REFOCLK).
 * Platforms without an LPM-friendly clock should return 0.
 */
unsigned long tiku_cpu_aclk_hz(void);

/**
 * @brief Non-zero if the platform reports a clock-source fault.
 *
 * Used by /sys/boot/clock/fault and the shell info command. On MSP430
 * this aggregates LFXT/HFXT/DCO oscillator-fault flags. Platforms
 * with no equivalent should return 0.
 */
int tiku_cpu_clock_has_fault(void);

/*---------------------------------------------------------------------------*/
/* CPU DATA-CACHE MAINTENANCE                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clean (write back) the data cache over an address range.
 *
 * Pushes dirty lines to memory so an out-of-band reader such as a DMA or ROM
 * agent sees them; the memory module calls it before handing a staging buffer
 * to the NVM programmer.  A no-op where the range is not cached.
 */
void tiku_cpu_dcache_clean(const void *addr, unsigned long len);

/**
 * @brief Invalidate the data cache over an address range.
 *
 * Drops cached copies so the next read sees what an out-of-band writer left --
 * the key to D-cache coherence with the persist layer.  Where the controller
 * has no by-range op the whole cache is invalidated: coarser but correct.
 */
void tiku_cpu_dcache_invalidate(const void *addr, unsigned long len);

/**
 * @brief Invalidate the entire instruction cache.
 *
 * Required after out-of-band writes to executable memory and before the first
 * fetch from the modified range.  Full invalidate, because modules are small
 * and installs are rare, so by-address precision buys nothing.
 */
void tiku_cpu_icache_invalidate(void);

/*---------------------------------------------------------------------------*/
/* IDLE / LOW-POWER MODES                                                    */
/*---------------------------------------------------------------------------*/

/*
 * Generic idle-mode classifications, mapped by each platform to its native
 * low-power state.  On MSP430: OFF is a busy-wait, LIGHT is LPM0 (CPU off),
 * DEEP is LPM3 (SMCLK off too) and DEEPEST is LPM4 (all clocks off, GPIO wake).
 */
typedef enum {
    TIKU_CPU_IDLE_OFF      = 0,
    TIKU_CPU_IDLE_LIGHT    = 1,
    TIKU_CPU_IDLE_DEEP     = 2,
    TIKU_CPU_IDLE_DEEPEST  = 3,
} tiku_cpu_idle_mode_t;

/** Function pointer signature for idle-entry hooks. */
typedef void (*tiku_cpu_idle_enter_t)(void);

/**
 * @brief Return the platform's entry function for the given mode.
 * @return Hook callable as the scheduler's idle hook, or NULL when
 *         the mode is OFF or unsupported.
 */
tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode);

/**
 * @brief Does the system tick interrupt wake this idle mode?
 *
 * Deadline-aware idle sleeps with timers armed only if the tick can wake the
 * CPU to dispatch them.  True for every MSP430 mode but LPM4, and always true
 * on the Cortex-M parts, where each mode is a WFI variant.
 *
 * @return Non-zero if the tick wakes the CPU out of @p mode
 */
int tiku_cpu_idle_mode_wakes_on_tick(tiku_cpu_idle_mode_t mode);

/**
 * @brief Short, platform-specific name for the mode.
 *        e.g. on MSP430: "off", "LPM0", "LPM3", "LPM4".
 */
const char *tiku_cpu_idle_mode_name(tiku_cpu_idle_mode_t mode);

/**
 * @brief Long, descriptive name for the mode.
 *        e.g. on MSP430: "LPM3 (CPU+SMCLK off, ACLK on)".
 */
const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t mode);

#endif /* TIKU_CPU_H_ */
