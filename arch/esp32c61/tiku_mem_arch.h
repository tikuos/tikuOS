/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - ESP32-C61 memory helpers and the durable mirror.
 *
 * Durable state lives in SRAM and is mirrored to two four-sector slots at the
 * top of the external flash, which carry it across a power cycle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_MEM_ARCH_H_
#define TIKU_ESP32C61_MEM_ARCH_H_

#include <stdint.h>
#include <stddef.h>   /* NULL, for files that reach it through the mem HAL */

/** @brief Word alignment the allocator rounds to. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** @brief Size type for arch memory calls. */
typedef uint32_t tiku_mem_arch_size_t;

/**
 * @brief Restore the durable region from the newest flash mirror slot that
 *        checks.
 *
 * @note Call after tiku_flash_init(); with the flash down the region keeps
 *       its reset contents.
 */
void tiku_mem_arch_init(void);

/**
 * @brief Overwrite a buffer so its contents cannot be recovered.
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
 * @brief Commit the durable region to the flash mirror, discarding the
 *        result.
 *
 * @note Failure is silent: a flash that is not ready, a failed erase or a
 *       failed program leaves the mirror stale, and the only signal is that
 *       tiku_mem_arch_nvm_program_count() did not advance.
 */
void tiku_mem_arch_nvm_flush(void);

/**
 * @brief Commit the durable SRAM region to the flash mirror's other slot.
 *
 * The state survives a power cycle once the mirror carries it.  A mirror
 * whose length and CRC already match is left as it is.
 *
 * @return 0 when the mirror matches the region, -1 when the flash is down,
 *         the region is larger than a slot, or an erase or program fails
 */
int tiku_mem_arch_nvm_flush_status(void);

/**
 * @brief What the boot-time mirror restore found.
 *
 * @return One of the tiku_nvm_restore_t values
 */
int tiku_mem_arch_nvm_restore_status(void);

/**
 * @brief Mirror slots written since boot.
 *
 * @return The count; a flush that finds the mirror current adds nothing
 */
uint32_t tiku_mem_arch_nvm_program_count(void);

#endif /* TIKU_ESP32C61_MEM_ARCH_H_ */
