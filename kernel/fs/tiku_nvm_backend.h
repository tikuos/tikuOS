/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_backend.h - the NVM "region" substrate (the B layer).
 *
 * A memory-mapped non-volatile region plus a thin write/erase backend: reads are
 * a pointer dereference into `base`, writes go through the backend, which is the
 * only thing that differs across FRAM, MRAM and Flash.
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
 * The data is durable once this returns 0.  Writes must occur inside the
 * platform's NVM write window (tiku_mpu_unlock_nvm()/lock_nvm()); the backend
 * does not open it itself.
 *
 * @return 0 on success, negative on failure.
 */
typedef int (*tiku_nvm_write_fn)(struct tiku_nvm_backend *be,
                                 size_t off, const void *src, size_t len);

/**
 * @brief Erase @p len bytes at @p off (block-granular).
 *
 * NULL for byte-writable backends (FRAM, MRAM) that need no erase.
 *
 * @return 0 on success, negative on failure.
 */
typedef int (*tiku_nvm_erase_fn)(struct tiku_nvm_backend *be,
                                 size_t off, size_t len);

/**
 * @brief A reserved, memory-mapped NVM region + its write/erase backend.
 */
typedef struct tiku_nvm_backend {
    uint8_t          *base;   /**< memory-mapped region base (read by pointer) */
    size_t            size;   /**< region size in bytes                        */
    tiku_nvm_write_fn write;  /**< program bytes (required)                    */
    tiku_nvm_erase_fn erase;  /**< erase block (NULL on FRAM/MRAM)             */
    void             *ctx;    /**< backend-private state                       */
} tiku_nvm_backend_t;

#endif /* TIKU_NVM_BACKEND_H_ */
