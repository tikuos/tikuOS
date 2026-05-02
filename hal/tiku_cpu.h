/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.h - Platform-agnostic CPU abstraction interface
 *
 * Provides atomic section entry/exit, IRQ control, clock-rate
 * queries, and idle-mode entry hooks. Each function delegates to
 * the active platform's arch implementation; non-MSP430 ports
 * supply their own backend.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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
/* IDLE / LOW-POWER MODES                                                    */
/*---------------------------------------------------------------------------*/

/**
 * Generic idle-mode classifications. Each platform maps these to its
 * native low-power state. The mapping for MSP430 is:
 *
 *   OFF      -> busy-wait (no LPM entry)
 *   LIGHT    -> LPM0  (CPU off, SMCLK+ACLK on)
 *   DEEP     -> LPM3  (CPU+SMCLK off, ACLK on)
 *   DEEPEST  -> LPM4  (all clocks off, GPIO wake only)
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
