/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.c - Platform-agnostic CPU abstraction implementation
 *
 * Provides atomic section entry/exit, IRQ control, clock-rate
 * queries, and idle-mode hooks. On MSP430 these forward to the
 * tiku_cpu_msp430_* / tiku_cpu_boot_msp430_* arch functions.
 *
 * The atomic functions use MSP430 intrinsics directly (<msp430.h>) rather
 * than including tiku.h, because tiku.h defines PLATFORM_MSP430 which
 * would also activate tiku_cpu_boot_init() and tiku_cpu_freq_init() —
 * those require proper clock/pin configuration that is not yet ready
 * at the HAL level.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <msp430.h>    /* MSP430 intrinsics for interrupt state management */
#include <stddef.h>
#include "tiku_cpu.h"

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_cpu_freq_boot_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* ATOMIC NESTING                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Atomic section nesting depth and saved GIE state.
 *
 * On the outermost tiku_atomic_enter() (nesting == 0) we snapshot
 * the current GIE bit BEFORE disabling interrupts.  On the matching
 * outermost tiku_atomic_exit() we only re-enable interrupts if GIE
 * was originally set.  This prevents atomic sections from
 * unconditionally enabling interrupts as a side-effect.
 *
 * ISR safety: if an ISR fires between __get_interrupt_state() and
 * __disable_interrupt() during the outermost enter, the ISR runs its
 * own balanced enter/exit pair (which sees nesting == 0, saves
 * GIE == 0 since the hardware clears GIE on ISR entry, and does not
 * re-enable on exit).  When the ISR returns via RETI, GIE is
 * restored from the stacked SR, and the main context continues with
 * its local `sr` still valid on the stack.
 */
static volatile unsigned int tiku_atomic_nesting = 0;
static volatile unsigned int tiku_atomic_gie_saved = 0;

void tiku_atomic_enter(void) {

  unsigned int sr = __get_interrupt_state();
  __disable_interrupt();
  if (tiku_atomic_nesting == 0) {
    tiku_atomic_gie_saved = (sr & GIE) != 0;
  }
  tiku_atomic_nesting++;

}

void tiku_atomic_exit(void) {

  if (--tiku_atomic_nesting == 0 && tiku_atomic_gie_saved) {
    __enable_interrupt();
  }

}

/*---------------------------------------------------------------------------*/
/* IRQ CONTROL                                                               */
/*---------------------------------------------------------------------------*/

void tiku_cpu_irq_enable(void) {
    __enable_interrupt();
}

void tiku_cpu_irq_disable(void) {
    __disable_interrupt();
}

/*---------------------------------------------------------------------------*/
/* BOOT / FREQUENCY                                                          */
/*---------------------------------------------------------------------------*/

void tiku_cpu_boot_init(void) {

#ifdef PLATFORM_MSP430
    tiku_cpu_boot_msp430_init();
#endif
}

void tiku_cpu_freq_init(unsigned int cpu_freq) {

#ifdef PLATFORM_MSP430
    tiku_cpu_freq_msp430_init(cpu_freq);
#endif
}

/*---------------------------------------------------------------------------*/
/* CLOCK RATE QUERIES                                                        */
/*---------------------------------------------------------------------------*/

unsigned long tiku_cpu_mclk_hz(void) {
#ifdef PLATFORM_MSP430
    return tiku_cpu_msp430_clock_get_hz();
#else
    return 0;
#endif
}

unsigned long tiku_cpu_smclk_hz(void) {
#ifdef PLATFORM_MSP430
    return tiku_cpu_msp430_smclk_get_hz();
#else
    return 0;
#endif
}

unsigned long tiku_cpu_aclk_hz(void) {
#ifdef PLATFORM_MSP430
    return tiku_cpu_msp430_aclk_get_hz();
#else
    return 0;
#endif
}

int tiku_cpu_clock_has_fault(void) {
#ifdef PLATFORM_MSP430
    return tiku_cpu_msp430_clock_has_fault() ? 1 : 0;
#else
    return 0;
#endif
}

/*---------------------------------------------------------------------------*/
/* IDLE / LOW-POWER MODES                                                    */
/*---------------------------------------------------------------------------*/

tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode) {
#ifdef PLATFORM_MSP430
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
            return tiku_cpu_boot_msp430_power_lpm0_enter;
        case TIKU_CPU_IDLE_DEEP:
            return tiku_cpu_boot_msp430_power_lpm3_enter;
        case TIKU_CPU_IDLE_DEEPEST:
            return tiku_cpu_boot_msp430_power_lpm4_enter;
        case TIKU_CPU_IDLE_OFF:
        default:
            return NULL;
    }
#else
    (void)mode;
    return NULL;
#endif
}

const char *tiku_cpu_idle_mode_name(tiku_cpu_idle_mode_t mode) {
#ifdef PLATFORM_MSP430
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:   return "LPM0";
        case TIKU_CPU_IDLE_DEEP:    return "LPM3";
        case TIKU_CPU_IDLE_DEEPEST: return "LPM4";
        case TIKU_CPU_IDLE_OFF:
        default:                    return "off";
    }
#else
    (void)mode;
    return "off";
#endif
}

const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t mode) {
#ifdef PLATFORM_MSP430
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
            return "LPM0 (CPU off, SMCLK+ACLK on)";
        case TIKU_CPU_IDLE_DEEP:
            return "LPM3 (CPU+SMCLK off, ACLK on)";
        case TIKU_CPU_IDLE_DEEPEST:
            return "LPM4 (all clocks off)";
        case TIKU_CPU_IDLE_OFF:
        default:
            return "off (busy-wait)";
    }
#else
    (void)mode;
    return "off (busy-wait)";
#endif
}
