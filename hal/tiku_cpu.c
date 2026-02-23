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

/*
 * Enter atomic section: save GIE on outermost call, then disable
 */
void tiku_atomic_enter() {

  unsigned int sr = __get_interrupt_state();
  __disable_interrupt();
  if (tiku_atomic_nesting == 0) {
    tiku_atomic_gie_saved = (sr & GIE) != 0;
  }
  tiku_atomic_nesting++;

}

/*
 * Exit atomic section: restore GIE only when fully unnested and
 * only if it was set before the outermost enter.
 */
void tiku_atomic_exit() {

  if (--tiku_atomic_nesting == 0 && tiku_atomic_gie_saved) {
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