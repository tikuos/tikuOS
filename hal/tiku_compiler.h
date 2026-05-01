/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compiler.h - Compiler abstraction for CCS and GCC
 *
 * Provides portable macros for ISR declarations, weak symbols,
 * and other compiler-specific constructs so that TikuOS builds
 * with both TI CCS (cl430) and msp430-elf-gcc.
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

#ifndef TIKU_COMPILER_H_
#define TIKU_COMPILER_H_

/*---------------------------------------------------------------------------*/
/* INTERRUPT SERVICE ROUTINE DECLARATION                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Declare an ISR portably across CCS and GCC.
 *
 * Usage:
 *   TIKU_ISR(TIMER0_A0_VECTOR, timer0_a0_isr)
 *   {
 *       // handler body
 *   }
 *
 * CCS expands to:
 *   #pragma vector=TIMER0_A0_VECTOR
 *   __interrupt void timer0_a0_isr(void) { ... }
 *
 * GCC expands to:
 *   __attribute__((interrupt(TIMER0_A0_VECTOR), lower))
 *   void timer0_a0_isr(void) { ... }
 *
 * The `lower` attribute pins the handler in lower FRAM (0x4400-0xFF7F).
 * MSP430 interrupt vectors at 0xFF80 are 16-bit, so any ISR that drifts
 * into HIFRAM under MEMORY_MODEL=large gets its address truncated and
 * the vector points to garbage — boot may complete but the handler
 * silently never fires. Under the default small model the attribute is
 * a no-op (all code already lives in lower FRAM). Keeping it on every
 * ISR site is what makes the large-model build safe.
 */
#if defined(__TI_COMPILER_VERSION__)
#define TIKU_ISR(vec, name) \
    _Pragma(TIKU_ISR_STRINGIFY_(vector=vec)) \
    __interrupt void name(void)
#define TIKU_ISR_STRINGIFY_(x) #x

#elif defined(__GNUC__)
#define TIKU_ISR(vec, name) \
    __attribute__((interrupt(vec), lower)) \
    void name(void)

#else
#error "Unsupported compiler — define TIKU_ISR for your toolchain"
#endif

/*---------------------------------------------------------------------------*/
/* WEAK SYMBOL                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Mark a symbol as weak so user code can override it.
 *
 * Usage:
 *   TIKU_WEAK struct tiku_process * const tiku_autostart_processes[] = {NULL};
 */
#if defined(__TI_COMPILER_VERSION__)
#define TIKU_WEAK __attribute__((weak))
#elif defined(__GNUC__)
#define TIKU_WEAK __attribute__((weak))
#else
#define TIKU_WEAK
#endif

#endif /* TIKU_COMPILER_H_ */
