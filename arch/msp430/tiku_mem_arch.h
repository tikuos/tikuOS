/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
 * @brief Write from SRAM into non-volatile memory.
 *
 * A memcpy here, since FRAM is memory-mapped, with the caller owning the
 * unlock.  It goes through the arch layer because other NVM technologies need
 * erase-before-write or page alignment.
 *
 * @param dst   NVM destination address
 * @param src   SRAM source buffer
 * @param len   Number of bytes to write
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

/**
 * @brief Flush in-RAM NVM modifications to non-volatile storage.
 *
 * A no-op here: FRAM writes are durable as soon as the bus cycle completes.  It
 * exists for parity with ports whose .persistent state is mirrored to a flash
 * sector needing an explicit erase and program per unlock window.
 */
static inline void tiku_mem_arch_nvm_flush(void) { /* no-op */ }

#endif /* TIKU_MEM_ARCH_H_ */
