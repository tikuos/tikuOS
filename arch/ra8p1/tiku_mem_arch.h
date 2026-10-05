/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - RA8P1 memory arch hooks.
 *
 * The durable region is MRAM, byte-writable in place.  Writes land in the
 * controller's 32-byte buffer and are committed by tiku_mem_arch_nvm_flush().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_MEM_ARCH_H_
#define TIKU_RA8P1_MEM_ARCH_H_

#include <stdint.h>
#include <stddef.h>   /* NULL */

/** @brief Word alignment the allocator rounds to. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** @brief Size type for arch memory calls. */
typedef uint32_t tiku_mem_arch_size_t;

/** @brief Arch memory init; does nothing on this part, where the MRAM
 *         programming gate opens per write window. */
void tiku_mem_arch_init(void);

/**
 * @brief Zero a buffer with volatile stores, which the compiler keeps.
 *
 * @param buf  Buffer to wipe; NULL is ignored
 * @param len  Length in bytes
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes out of the durable region; a NULL pointer does nothing.
 *
 * @param dst  Destination
 * @param src  Source inside the durable region
 * @param len  Length in bytes
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len);

/**
 * @brief Copy bytes into the durable region; a NULL pointer does nothing.
 *
 * @note Call inside a tiku_mpu_unlock_nvm() window; the bytes wait in the
 *       MRAM write buffer until tiku_mem_arch_nvm_flush().
 *
 * @param dst  Destination inside the durable region
 * @param src  Source
 * @param len  Length in bytes
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Commit buffered durable writes into the MRAM array, ignoring
 *        errors.
 *
 * A store waits in the controller's 32-byte write buffer, and reads return
 * the buffered value; a power cut before the flush loses it.
 */
void tiku_mem_arch_nvm_flush(void);

/**
 * @brief Commit buffered durable writes into the MRAM array.
 *
 * @return 0 on success, -1 when the MRAM flush reports an error
 */
int tiku_mem_arch_nvm_flush_status(void);

/**
 * @brief Count of successful MRAM flushes since boot.
 *
 * @return Successful tiku_mem_arch_nvm_flush_status() calls since boot
 */
uint32_t tiku_mem_arch_nvm_program_count(void);

#endif /* TIKU_RA8P1_MEM_ARCH_H_ */
