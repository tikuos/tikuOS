/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_thread_arch.h - C5 worker context and C-library state.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_THREAD_ARCH_H_
#define TIKU_ESP32C5_THREAD_ARCH_H_
#include <stdint.h>
/** @brief Claim C5 software interrupt 0 for worker context switches. */
void tiku_thread_arch_boot(void);
/** @brief Pend a worker switch through software interrupt 0. */
void tiku_thread_arch_pend(void);
/** @brief Return the wrapping CPU cycle counter. */
uint32_t tiku_thread_arch_cycles(void);
/** @brief Create a 16-byte-aligned RISC-V register frame for a new worker. */
uint32_t *tiku_thread_arch_frame_init(uint32_t *top, void (*entry)(void *),
                                      void *argument, void (*exit_fn)(void));
/** @brief Reset C-library state for a worker slot before it starts. */
void tiku_c5_reent_init(uint8_t slot);
/** @brief Select the interrupt C-library state before running a handler. */
void tiku_c5_reent_enter(void);
/** @brief Select C-library state for the context about to resume. */
void tiku_c5_reent_leave(void);
#endif
