/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_hal.h - Platform-routing header for memory management
 *
 * Routes to the correct architecture-specific memory header based on
 * the selected platform. Provides portable fallback defaults when no
 * platform is selected (e.g. host-mode testing).
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

#ifndef TIKU_MEM_HAL_H_
#define TIKU_MEM_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PLATFORM ROUTING                                                          */
/*---------------------------------------------------------------------------*/

#ifdef PLATFORM_MSP430
#include "arch/msp430/tiku_mem_arch.h"
#endif

/*---------------------------------------------------------------------------*/
/* FALLBACK DEFAULTS (host / unknown platform)                               */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_MEM_ARCH_ALIGNMENT
/** Default alignment for 32-bit platforms (ARM Cortex-M, RISC-V, host) */
#define TIKU_MEM_ARCH_ALIGNMENT  4U
#endif

#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
/** Default 32-bit size type for platforms with > 64 KB address space */
typedef uint32_t tiku_mem_arch_size_t;
#endif

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize platform-specific memory hardware
 */
void tiku_mem_arch_init(void);

/**
 * @brief Securely wipe a memory region
 *
 * @param buf   Start of the region to wipe
 * @param len   Number of bytes to zero
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Read from non-volatile memory into SRAM
 *
 * @param dst   SRAM destination buffer
 * @param src   NVM source address
 * @param len   Number of bytes to read
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Write from SRAM into non-volatile memory
 *
 * @param dst   NVM destination address
 * @param src   SRAM source buffer
 * @param len   Number of bytes to write
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

#endif /* TIKU_MEM_HAL_H_ */
