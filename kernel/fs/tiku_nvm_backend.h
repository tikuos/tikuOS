/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_backend.h - memory-mapped NVM region and its write/erase backend.
 *
 * Reads are a pointer dereference into `base`; writes go through the backend,
 * the only part that differs across FRAM, MRAM, RRAM and flash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_BACKEND_H_
#define TIKU_NVM_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

struct tiku_nvm_backend;

/**
 * @brief Program @p len bytes at byte offset @p off within the region.
 *
 * The data is durable once this returns 0.
 *
 * @note The caller holds the platform's NVM write window
 *       (tiku_mpu_unlock_nvm()/lock_nvm()) where the region needs one; a
 *       backend may rely on the window being open.
 * @return 0 on success, negative on failure.
 */
typedef int (*tiku_nvm_write_fn)(struct tiku_nvm_backend *be,
                                 size_t off, const void *src, size_t len);

/**
 * @brief Erase @p len bytes at @p off (block-granular).
 *
 * A backend leaves erase NULL when write() needs no separate erase: on
 * byte-writable media, and on flash backends that erase inside write().
 *
 * @return 0 on success, negative on failure.
 */
typedef int (*tiku_nvm_erase_fn)(struct tiku_nvm_backend *be,
                                 size_t off, size_t len);

/**
 * @brief A reserved, memory-mapped NVM region + its write/erase backend.
 */
typedef struct tiku_nvm_backend {
    uint8_t          *base;   /**< mapped region base, read by pointer      */
    size_t            size;   /**< region size in bytes                     */
    tiku_nvm_write_fn write;  /**< program bytes (required)                 */
    tiku_nvm_erase_fn erase;  /**< erase blocks; NULL if write() needs none */
    void             *ctx;    /**< backend-private state                    */
} tiku_nvm_backend_t;

#endif /* TIKU_NVM_BACKEND_H_ */
