/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - RP2350 memory architecture constants
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_MEM_ARCH_H_
#define TIKU_RP2350_MEM_ARCH_H_

#include <stdint.h>

/** Cortex-M33 32-bit native word — most allocations should be 4-byte
 *  aligned to avoid the unaligned-access penalty. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** 32-bit address space → 32-bit size type. */
#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#endif

void tiku_mem_arch_init(void);
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

#endif /* TIKU_RP2350_MEM_ARCH_H_ */
