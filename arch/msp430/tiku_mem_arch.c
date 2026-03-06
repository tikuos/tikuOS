/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - MSP430 memory architecture implementation
 *
 * Implements platform-specific memory operations for the MSP430 family.
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

#include "tiku_mem_arch.h"

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_init                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize MSP430-specific memory hardware
 *
 * Currently a no-op. Future implementations may configure:
 *   - FRAM wait states for high-frequency operation
 *   - Memory Protection Unit (MPU) regions
 *   - DMA controller defaults for bulk transfers
 */
void tiku_mem_arch_init(void)
{
    /* Nothing to do yet on MSP430. */
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_secure_wipe                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Securely wipe a memory region with zeros
 *
 * Overwrites each byte through a volatile pointer to prevent the
 * compiler from optimizing away the zeroing — without the volatile
 * qualifier, GCC -O2 and LLVM see that the memory is never read
 * afterward and may elide the entire loop.
 *
 * MSP430 cycle cost (16-bit RISC, single-cycle SRAM writes):
 *   The inner loop compiles to roughly:
 *       MOV.B #0, 0(Rn)    ; 4 cycles (indexed mode)
 *       INC   Rn            ; 1 cycle
 *       CMP   Rn, Rm        ; 1 cycle
 *       JNZ   loop          ; 2 cycles (taken)
 *   Total: ~8 cycles per byte on MSP430, ~5 cycles per byte on MSP430X.
 *
 *   Concrete examples at 16 MHz MCLK:
 *     64 B   →   ~512 cycles →   32 us
 *    256 B   →  ~2048 cycles →  128 us
 *   2048 B   → ~16384 cycles → 1024 us  (1 ms)
 *
 * @param buf   Start of the region to wipe
 * @param len   Number of bytes to zero
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        p[i] = 0;
    }
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_nvm_read                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read from FRAM into SRAM
 *
 * FRAM on MSP430 is memory-mapped, so this is a straight memcpy.
 * The abstraction exists because other NVM technologies (Flash, EEPROM)
 * may not be memory-mapped and require special read sequences.
 *
 * @param dst   SRAM destination
 * @param src   FRAM source
 * @param len   Bytes to copy
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/*---------------------------------------------------------------------------*/
/* tiku_mem_arch_nvm_write                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write from SRAM into FRAM
 *
 * FRAM on MSP430 is memory-mapped, so this is a straight memcpy.
 * The caller must unlock the MPU before calling — this function does
 * not manage MPU state, allowing the caller to batch multiple writes
 * in a single MPU-unlocked critical section.
 *
 * @param dst   FRAM destination
 * @param src   SRAM source
 * @param len   Bytes to copy
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len)
{
    tiku_mem_arch_size_t i;

    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}
