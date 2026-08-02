/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - STM32N6 memory helpers.
 *
 * The part has no internal NVM, so the durable calls act on ordinary SRAM and
 * the flush is a barrier rather than a program cycle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_MEM_ARCH_H_
#define TIKU_STM32N6_MEM_ARCH_H_

#include <stdint.h>
#include <stddef.h>   /* NULL -- kept in the mem-HAL chain like the other ports */

/** @brief Word alignment the allocator rounds to. */
#define TIKU_MEM_ARCH_ALIGNMENT  4U

/** @brief Size type for arch memory calls. */
typedef uint32_t tiku_mem_arch_size_t;

/** @brief Prepare arch-level memory state; nothing to unlock on this part. */
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
 * @note SRAM-backed here, so this does not survive a power cycle.
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len);

/** @brief Make prior durable writes visible; a barrier on this part. */
void tiku_mem_arch_nvm_flush(void);

#endif /* TIKU_STM32N6_MEM_ARCH_H_ */
