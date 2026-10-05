/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - RP2350 memory architecture constants and NVM interface.
 *
 * Alignment and size type, and the calls that keep the durable .uninit SRAM
 * region and its flash mirror in step.
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

/** @brief Allocation alignment: the Cortex-M33's 32-bit word. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** @brief Size type covering the 32-bit address space. */
#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#endif

/** @} */ /* End of TIKU_MEM_ARCH_CONSTANTS group */

/**
 * @brief Initialize the RP2350 memory subsystem.
 *
 * Resolves the boot-ROM flash helpers and restores the durable .uninit
 * region from its flash mirror when the mirror's image checks out; with no
 * such image .uninit is zeroed.  tiku_mem_init() calls it at boot.
 */
void tiku_mem_arch_init(void);

/**
 * @brief Overwrite a buffer with zeros using a volatile loop.
 *
 * The volatile pointer stops the compiler eliding the zeroing when the buffer
 * is never read afterwards.  Use it for keys, nonces and credentials before
 * releasing the buffer.
 *
 * @param buf  Buffer to wipe.
 * @param len  Number of bytes to zero.
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes from the durable SRAM working copy.
 *
 * .persistent variables live in the .uninit SRAM region, which is mirrored
 * to flash.  This reads @p len bytes at @p src in that SRAM region, not the
 * flash copy, into @p dst.
 *
 * @param dst  Destination buffer.
 * @param src  Source address within the .uninit region.
 * @param len  Number of bytes to copy.
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes into the durable SRAM working copy.
 *
 * Writes @p len bytes into the .uninit SRAM region at @p dst; flash changes
 * only at the next tiku_mem_arch_nvm_flush_status().
 *
 * @note The caller must hold an MPU unlock window; this function does not
 *       manage the MPU itself.
 * @param dst  Destination address within the .uninit region.
 * @param src  Source buffer.
 * @param len  Number of bytes to write.
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

/**
 * @brief Flush the durable SRAM region to flash.
 *
 * Calls tiku_mem_arch_nvm_flush_status() and discards its result.
 */
void tiku_mem_arch_nvm_flush(void);

/**
 * @brief Commit the .uninit region to its flash mirror and report the result.
 *
 * Writes the region, behind a 16-byte CRC header, to the mirror sectors so it
 * survives a power cycle; an unchanged image writes nothing.
 *
 * @note tiku_mpu_lock_nvm() calls it at the end of every unlock window.
 * @return 0 when the mirror holds .uninit, -1 when the commit failed.
 */
int tiku_mem_arch_nvm_flush_status(void);

#endif /* TIKU_RP2350_MEM_ARCH_H_ */
