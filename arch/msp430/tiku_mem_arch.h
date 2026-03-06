/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - MSP430 memory architecture constants and declarations
 *
 * Provides platform-specific memory parameters for the MSP430 family:
 * alignment requirement, native size type, and arch-level init/wipe
 * functions. Included indirectly via hal/tiku_mem_hal.h.
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

#ifndef TIKU_MEM_ARCH_H_
#define TIKU_MEM_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* ALIGNMENT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief MSP430 minimum allocation alignment (bytes)
 *
 * MSP430 is a 16-bit architecture. Unaligned word access causes a bus
 * fault, so every allocation must start on an even address.
 */
#define TIKU_MEM_ARCH_ALIGNMENT  2U

/*---------------------------------------------------------------------------*/
/* SIZE TYPE                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Architecture-specific size type for memory operations
 *
 * uint16_t is sufficient for MSP430 — SRAM never exceeds 64 KB.
 * Saves RAM compared to uint32_t on a 16-bit architecture.
 */
#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint16_t tiku_mem_arch_size_t;
#endif

/*---------------------------------------------------------------------------*/
/* FUNCTION DECLARATIONS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize MSP430-specific memory hardware
 *
 * Called once during boot from tiku_mem_init(). Currently a no-op —
 * future use includes FRAM wait-state configuration and MPU setup.
 */
void tiku_mem_arch_init(void);

/**
 * @brief Securely wipe a memory region using a volatile byte loop
 *
 * Overwrites @p len bytes starting at @p buf with zeros. Uses a
 * volatile pointer to prevent the compiler from eliding the loop.
 *
 * @param buf   Start of the region to wipe
 * @param len   Number of bytes to zero
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Read from non-volatile memory into SRAM
 *
 * On MSP430, FRAM is memory-mapped so this is a memcpy. Abstracted
 * through the arch layer because other platforms may require special
 * bus configuration, wait states, or non-memory-mapped NVM access.
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
 * On MSP430, FRAM is memory-mapped so this is a memcpy. The caller
 * is responsible for unlocking the MPU before calling this function.
 * Abstracted through the arch layer because other NVM technologies
 * (Flash, EEPROM) may require erase-before-write or page alignment.
 *
 * @param dst   NVM destination address
 * @param src   SRAM source buffer
 * @param len   Number of bytes to write
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

#endif /* TIKU_MEM_ARCH_H_ */
