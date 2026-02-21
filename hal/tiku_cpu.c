/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.c - Platform-agnostic CPU abstraction implementation
 *
 * Provides atomic section entry/exit for interrupt-safe operations.
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
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <msp430.h>    /* MSP430 intrinsics for interrupt state management */
#include "tiku_cpu.h"

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_cpu_freq_boot_arch.h"
#endif

/*
 * Atomic section nesting depth.
 *
 * A simple disable/enable pair has a subtle issue: if tiku_atomic_exit()
 * is called from ISR context, __enable_interrupt() would briefly re-enable
 * GIE before the ISR returns. On MSP430 this is safe for TikuOS because
 * the Timer A0 CCR0 flag is auto-cleared on ISR entry, and no other ISR
 * source would cause re-entrancy issues.
 *
 * A save/restore approach with a static variable is NOT safe because
 * the ISR can overwrite the saved SR between __get_interrupt_state()
 * and __disable_interrupt() in the main context, permanently disabling
 * interrupts. The nesting counter avoids shared state between contexts.
 */
static volatile unsigned int tiku_atomic_nesting = 0;

/*
 * Enter atomic section: disable interrupts and track nesting
 */
void tiku_atomic_enter() {

  __disable_interrupt();
  tiku_atomic_nesting++;

}

/*
 * Exit atomic section: re-enable interrupts only when fully unnested
 */
void tiku_atomic_exit() {

  if (--tiku_atomic_nesting == 0) {
    __enable_interrupt();
  }

}

/*
 * Initialize CPU boot sequence
 */
void tiku_cpu_boot_init(void) {

#ifdef PLATFORM_MSP430
    tiku_cpu_boot_msp430_init();
#endif
}

/*
 * Initialize CPU frequency
 */
void tiku_cpu_freq_init(unsigned int cpu_freq) {

#ifdef PLATFORM_MSP430
    tiku_cpu_freq_msp430_init(cpu_freq);
#endif
}