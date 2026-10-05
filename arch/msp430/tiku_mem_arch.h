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
 * A word access to an odd address uses the even address below it (the
 * CPU ignores the low bit), so every allocation starts on an even address.
 */
#define TIKU_MEM_ARCH_ALIGNMENT  2U

/*---------------------------------------------------------------------------*/
/* SIZE TYPE                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Architecture-specific size type for memory operations
 *
 * 16 bits wide: MSP430 SRAM is far below 64 KB.
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
 * Does nothing on MSP430.
 *
 * @note Called once during boot from tiku_mem_init().
 */
void tiku_mem_arch_init(void);

/**
 * @brief Securely wipe a memory region using a volatile byte loop
 *
 * Overwrites @p len bytes starting at @p buf with zeros, through a
 * volatile pointer so the compiler keeps every store.
 *
 * @param buf   Start of the region to wipe
 * @param len   Number of bytes to zero
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Read from non-volatile memory into SRAM
 *
 * FRAM is memory-mapped, so this is a byte copy.
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
 * A byte copy: FRAM is memory-mapped and written in place.
 *
 * @note The caller holds the MPU write window (tiku_mpu_arch_unlock_nvm());
 *       a write outside it is dropped.
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
 * Does nothing and returns 0: FRAM writes are durable as soon as the bus
 * cycle completes.
 */
static inline int tiku_mem_arch_nvm_flush_status(void) { return 0; }
/** @brief Flush without a status: a no-op on FRAM, which writes in place. */
static inline void tiku_mem_arch_nvm_flush(void)
{
    (void)tiku_mem_arch_nvm_flush_status();
}

#endif /* TIKU_MEM_ARCH_H_ */
