/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.h - P4 integer worker frames and newlib state.
 *
 * Worker frames use the trap frame layout of tiku_irq_arch.h and hold no
 * floating-point state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_THREAD_ARCH_H_
#define TIKU_ESP32P4_THREAD_ARCH_H_
#include <stdint.h>
/** @brief Claim CPU0 software interrupt 0 for worker context switches. */
void tiku_thread_arch_boot(void);
/** @brief Pend a switch; callable with machine interrupts masked. */
void tiku_thread_arch_pend(void);
/** @brief Return the wrapping CPU cycle counter. */
uint32_t tiku_thread_arch_cycles(void);
/** @brief Initialize a 16-byte-aligned, integer-only machine-mode frame. */
uint32_t *tiku_thread_arch_frame_init(uint32_t *top, void (*entry)(void *),
                                      void *argument, void (*exit_fn)(void));
/** @brief Clear the C-library state of a worker slot before reuse. */
void tiku_p4_reent_init(uint8_t slot);
/** @brief Select the dedicated interrupt C-library state. */
void tiku_p4_reent_enter(void);
/** @brief Select the C-library state of the context about to resume. */
void tiku_p4_reent_leave(void);
#endif
