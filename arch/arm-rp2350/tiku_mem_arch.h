/*
 * Tiku Operating System v0.06
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

/**
 * @defgroup TIKU_MEM_ARCH_CONSTANTS RP2350 memory architecture constants
 * @brief Platform word size and size-type definitions for the RP2350.
 *
 * The Cortex-M33 is a 32-bit core; the natural alignment for
 * allocations is 4 bytes to avoid the unaligned-access penalty.
 * The size type is uint32_t to cover the full 32-bit address space.
 * @{
 */

/** Cortex-M33 32-bit native word — most allocations should be 4-byte
 *  aligned to avoid the unaligned-access penalty. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** 32-bit address space → 32-bit size type. */
#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#endif

/** @} */ /* End of TIKU_MEM_ARCH_CONSTANTS group */

/**
 * @brief Initialize the RP2350 memory subsystem.
 *
 * Called once at early boot.  Sets up the flash-mirror sector used as
 * the NVM backing store for .persistent variables and registers the
 * platform memory regions with tiku_region_init().
 */
void tiku_mem_arch_init(void);

/**
 * @brief Overwrite a buffer with zeros using a volatile loop.
 *
 * The volatile pointer prevents the compiler from eliding the zeroing
 * when the buffer is never read after the call — a known pitfall in
 * security-sensitive code.  Use for wiping keys, nonces, and
 * credentials from SRAM before releasing the buffer.
 *
 * @param buf  Buffer to wipe.
 * @param len  Number of bytes to zero.
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes from NVM (flash mirror) into SRAM.
 *
 * On RP2350, .persistent variables reside in a NOLOAD SRAM section
 * that is mirrored to flash.  This function reads @p len bytes from
 * the flash-side copy at @p src into the SRAM buffer at @p dst.
 *
 * @param dst  SRAM destination.
 * @param src  NVM source address.
 * @param len  Number of bytes to copy.
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes from SRAM into NVM (flash mirror).
 *
 * Writes @p len bytes from @p src into the flash-mirror sector at
 * @p dst.  The caller must hold an MPU unlock window; this function
 * does not manage the MPU itself.
 *
 * @param dst  NVM destination address (flash mirror sector).
 * @param src  SRAM source.
 * @param len  Number of bytes to write.
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

/**
 * @brief Flush any in-RAM NVM modifications to non-volatile storage.
 *
 * On RP2350 this snapshots the SRAM .uninit region (which holds all
 * .persistent placements) into the dedicated 4 KB flash mirror sector,
 * making the data durable across full power cycles instead of just
 * warm resets.  Called automatically by the kernel-level
 * tiku_mpu_lock_nvm() at the end of every unlock window.
 *
 * On platforms where NVM writes are already durable (MSP430 FRAM),
 * this is a no-op.
 */
void tiku_mem_arch_nvm_flush(void);

#endif /* TIKU_RP2350_MEM_ARCH_H_ */
