/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_mem_arch.h - STM32F411RE memory architecture constants
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_MEM_ARCH_H_
#define TIKU_STM32F411_MEM_ARCH_H_

#include <stdint.h>

#define TIKU_MEM_ARCH_ALIGNMENT  4U

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
void tiku_mem_arch_nvm_flush(void);

#endif /* TIKU_STM32F411_MEM_ARCH_H_ */
