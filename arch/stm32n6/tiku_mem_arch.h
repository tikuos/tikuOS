/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - STM32N6 memory helpers and the durable mirror.
 *
 * Durable state lives in SRAM and is mirrored to the last four sectors of the
 * external NOR; what was last flushed survives resets and power cycles.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_MEM_ARCH_H_
#define TIKU_STM32N6_MEM_ARCH_H_

#include <stdint.h>
#include <stddef.h>   /* NULL, for files that reach it through the mem HAL */

/** @brief Word alignment the allocator rounds to. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** @brief Size type for arch memory calls. */
#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#endif

/**
 * @brief Restore the durable region from the NOR mirror.
 *
 * Copies the mirror's image over the region when its header and CRC check
 * out; tiku_mem_arch_nvm_restore_status() reports what was found.
 *
 * @note Call after tiku_xspi_init(); with the XSPI not ready the region keeps
 *       its reset contents and the status reads virgin.
 */
void tiku_mem_arch_init(void);

/**
 * @brief Zero a buffer with writes the compiler cannot remove.
 *
 * @param buf  Buffer to wipe; NULL is ignored
 * @param len  Length in bytes
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len);

/**
 * @brief Read from the durable region.
 *
 * @param dst  Destination
 * @param src  Source inside the durable region
 * @param len  Length in bytes
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len);

/**
 * @brief Write to the durable region.
 *
 * @param dst  Destination inside the durable region
 * @param src  Source
 * @param len  Length in bytes
 * @note Reaches flash only at the next tiku_mem_arch_nvm_flush().
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/**
 * @brief Commit the durable SRAM region to the NOR mirror.
 *
 * tiku_mem_arch_nvm_flush_status() with its result discarded.  A mirror that
 * already holds the region's length and CRC is not rewritten.
 *
 * @note After a failure the mirror is stale or erased, and
 *       tiku_mem_arch_nvm_program_count() has not advanced.
 */
void tiku_mem_arch_nvm_flush(void);

/**
 * @brief Commit the durable SRAM region to the NOR mirror.
 *
 * Returns at once when the mirror already holds the region's length and CRC;
 * otherwise erases the four mirror sectors and programs the image, then the
 * header.
 *
 * @return 0 when the mirror matches the region, -1 when the XSPI is not
 *         ready, the region is larger than the mirror, or a step fails
 */
int tiku_mem_arch_nvm_flush_status(void);

/**
 * @brief What the boot-time mirror restore found.
 *
 * @return One of the tiku_nvm_restore_t values
 */
int tiku_mem_arch_nvm_restore_status(void);

/**
 * @brief Count of mirror commits since boot.
 *
 * @return Number of erase/program cycles this boot has spent
 */
uint32_t tiku_mem_arch_nvm_program_count(void);

#endif /* TIKU_STM32N6_MEM_ARCH_H_ */
