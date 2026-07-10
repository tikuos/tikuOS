/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - nRF54L memory-architecture constants + size type
 *
 * The memory HAL (hal/tiku_mem_hal.h) declares the tiku_mem_arch_* API; this
 * arch header supplies the platform's word-size type and alignment.  Mirrors
 * arch/arm-rp2350/tiku_mem_arch.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_MEM_ARCH_H_
#define TIKU_NORDIC_MEM_ARCH_H_

#include <stdint.h>
#include <stddef.h>   /* NULL -- kept in the mem-HAL chain like the other ports */

/** @brief Natural allocation alignment (32-bit Cortex-M33 word). */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** @brief Memory size / length type (32-bit address space). */
#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#endif

/**
 * @brief Drain any pending RRAM writes (no-op: RRAM writes are unbuffered).
 *
 * Declared here (like the rp2350 arch header) so kernel/memory/tiku_mpu.c's
 * lock-NVM path sees a prototype rather than an implicit declaration.
 */
void tiku_mem_arch_nvm_flush(void);

#endif /* TIKU_NORDIC_MEM_ARCH_H_ */

