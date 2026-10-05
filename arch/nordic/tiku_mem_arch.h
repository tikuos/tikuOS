/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - nRF54L memory-architecture constants + size type
 *
 * The memory HAL (hal/tiku_mem_hal.h) declares the tiku_mem_arch_* API; this
 * header supplies the size type and alignment it uses on the nRF54L.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_MEM_ARCH_H_
#define TIKU_NORDIC_MEM_ARCH_H_

#include <stdint.h>
#include <stddef.h>   /* NULL, size_t */

/** @brief Natural allocation alignment (32-bit Cortex-M33 word). */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
/** @brief Memory size / length type (32-bit address space). */
typedef uint32_t tiku_mem_arch_size_t;
#endif

/**
 * @brief Wait until RRAMC is ready.  RRAM writes on this port are
 *        unbuffered, so no other write is pending.
 */
void tiku_mem_arch_nvm_flush(void);

/**
 * @brief Wait until RRAMC is ready.
 * @return 0
 */
int tiku_mem_arch_nvm_flush_status(void);

#endif /* TIKU_NORDIC_MEM_ARCH_H_ */
