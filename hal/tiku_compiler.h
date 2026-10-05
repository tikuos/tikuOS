/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compiler.h - Compiler abstraction for CCS and GCC
 *
 * Gives TIKU_ISR and TIKU_WEAK for TI CCS (cl430) and for GCC on MSP430,
 * Cortex-M and RISC-V.
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
 * On msp430-elf-gcc the `lower` attribute keeps the handler in lower FRAM:
 * MSP430 vectors are 16-bit, so an ISR placed in HIFRAM under the large
 * model would have its address truncated and the vector would be wrong.
 */
#if defined(__TI_COMPILER_VERSION__)
#define TIKU_ISR(vec, name) \
    _Pragma(TIKU_ISR_STRINGIFY_(vector=vec)) \
    __interrupt void name(void)
#define TIKU_ISR_STRINGIFY_(x) #x

#elif defined(__GNUC__) && defined(PLATFORM_MSP430)
#define TIKU_ISR(vec, name) \
    __attribute__((interrupt(vec), lower)) \
    void name(void)

#elif defined(__GNUC__)
/* Other GCC targets: an ISR is a plain C function.  A Cortex-M port's
 * startup code (arch/<platform>/tiku_crt_early.c) places its address in the
 * vector table; ESP32-C61 reaches its handlers through the interrupt
 * dispatcher in tiku_irq_arch.c.  The vector argument is unused; it keeps the
 * call sites the same as on MSP430. */
#define TIKU_ISR(vec, name) \
    void name(void)

#else
#error "Unsupported compiler — define TIKU_ISR for your toolchain"
#endif

/*---------------------------------------------------------------------------*/
/* WEAK SYMBOL                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Mark a symbol as weak so a strong definition elsewhere overrides it.
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
