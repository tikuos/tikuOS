/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - Ambiq memory architecture interface.
 *
 * Shared by the Apollo510 (tiku_mem_arch.c) and Apollo4 Lite
 * (tiku_mem_apollo4l.c) backends: durable state in .uninit, mirrored to MRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_MEM_ARCH_H_
#define TIKU_AMBIQ_MEM_ARCH_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Native word alignment: 4 bytes on the Cortex-M55 and Cortex-M4F.
 */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
/**
 * @brief Platform memory size type.
 *
 * 32-bit unsigned integer matching the native word width.  Used throughout
 * the memory subsystem for buffer sizes, offsets, and allocation counts.
 */
typedef uint32_t tiku_mem_arch_size_t;
#endif

/**
 * @brief Restore .uninit from the MRAM mirror, or zero it.
 *
 * Restores a mirror that tiku_nvm_mirror_image() accepts, or a legacy V1
 * mirror; otherwise zeroes .uninit so every persist cell primes its default.
 * The outcome is kept for tiku_mem_arch_nvm_restore_status().
 *
 * @note Called from tiku_mem_init() before any arena or pool is created.
 */
void tiku_mem_arch_init(void);

/**
 * @brief Overwrite a buffer with zeros using a volatile store loop.
 *
 * The volatile pointer prevents the compiler from optimizing out the
 * zeroing operation — a known hazard when clearing sensitive data
 * (keys, credentials) that is never read after zeroing.
 *
 * @param buf  Buffer to wipe.
 * @param len  Number of bytes to zero.
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes from the NVM working copy (.uninit) into a buffer.
 *
 * .uninit is memory-mapped SRAM, so this is a plain memcpy.
 *
 * @param dst  Destination SRAM buffer.
 * @param src  Source address in .uninit.
 * @param len  Number of bytes to copy.
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes into the NVM working copy (.uninit).
 *
 * Writes RAM only (on Apollo510 it also cleans the D-cache over @p dst); the
 * MRAM commit happens when the matching tiku_mpu_lock_nvm() relock calls
 * tiku_mem_arch_nvm_flush_status().
 *
 * @note The caller must have the MPU NVM window unlocked.
 * @param dst  Destination address in .uninit.
 * @param src  Source SRAM buffer.
 * @param len  Number of bytes to write.
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len);

/**
 * @brief Commit .uninit to the MRAM mirror; tiku_mem_arch_nvm_flush_status()
 *        with the result discarded.
 */
void tiku_mem_arch_nvm_flush(void);

/**
 * @brief Commit .uninit to the reserved MRAM mirror page.
 *
 * Composes the mirror header and image and programs the page through the
 * bootrom, unless the mirror already holds the same image.
 *
 * @return 0 on success or when nothing changed, -1 when .uninit is larger
 *         than the page or the bootrom reports a failure
 */
int tiku_mem_arch_nvm_flush_status(void);

/**
 * @brief Program an arbitrary MRAM span via the on-chip bootrom.
 *
 * Read-modify-programs 16-byte-aligned chunks through a staging buffer, so a
 * sub-16-byte edge keeps the MRAM bytes beside it, then drops cached copies.
 * Defined in tiku_nvm_region_apollo510.c and tiku_nvm_region_apollo4l.c.
 *
 * @note Spans below user MRAM (0x00410000 on Apollo510, 0x00018000 on Apollo4
 *       Lite) or past the end of MRAM are refused.
 * @param dst  Absolute destination address in MRAM.
 * @param src  Source bytes (any address space; staged first).
 * @param len  Number of bytes to program.
 * @return 0 on success, -1 on bounds violation or bootrom failure.
 */
int tiku_nvm_mram_program(uintptr_t dst, const void *src, size_t len);

#endif /* TIKU_AMBIQ_MEM_ARCH_H_ */
