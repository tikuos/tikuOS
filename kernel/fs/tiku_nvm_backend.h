/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_backend.h - the NVM "region" substrate (the B layer).
 *
 * A board-sized, memory-mapped non-volatile region plus a thin write/erase
 * backend.  This is the ONLY thing that differs across parts: reads are a
 * plain pointer dereference into `base` (FRAM, MRAM and Flash are all
 * memory-mapped), while writes go through the backend --
 *
 *   FRAM  (MSP430)  : store in place
 *   MRAM  (Apollo)  : bootrom program (nv_program)
 *   Flash (RP2350)  : erase block, then program
 *
 * Consumers (the file store, the NVM tier, the persist store) ride this
 * substrate without caring which technology backs it.  The file store
 * (tiku_tfs) depends ONLY on this interface -- no kernel, VFS or tier
 * dependency -- so it is portable and host-unit-testable.
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
 * The data is durable once this returns 0 (the backend completes the program
 * synchronously, or buffers and commits on erase/flush for log-structured
 * Flash).  Writes are expected to occur inside the platform's NVM write window
 * (tiku_mpu_unlock_nvm()/lock_nvm()); the backend does not open it itself.
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
