/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.h - C6 integer frames and software-interrupt switching.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_THREAD_ARCH_H
#define TIKU_C6_THREAD_ARCH_H
#include <stdint.h>
#define C6_SWITCH_IRQ 18u
#define C6_SWITCH_SOURCE 22u
#define C6_SWITCH_REQUEST 0x600c5090u
#define C6_THREAD_FRAME_WORDS 36u
/** @brief Claim CPU software interrupt zero and native CPU line 18. */
void tiku_thread_arch_boot(void);
/** @brief Request a switch through the level-triggered software interrupt. */
void tiku_thread_arch_pend(void);
/** @brief Return the wrapping cycle counter used by the native runtime. */
uint32_t tiku_thread_arch_cycles(void);
/** @brief Build a 16-byte-aligned RV32IMAC machine-return frame. */
uint32_t *tiku_thread_arch_frame_init(uint32_t *top,void (*entry)(void *),
                                    void *arg,void (*exit_fn)(void));
/** @brief Clear a software request and select the next saved integer frame. */
uint32_t *tiku_c6_thread_dispatch(uint32_t *frame);
#endif
