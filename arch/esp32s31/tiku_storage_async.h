/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_storage_async.h - Polled region writes and automatic flash suspension.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_STORAGE_ASYNC_H_
#define TIKU_S31_STORAGE_ASYNC_H_
#include "tiku_storage_arch.h"
#include <kernel/fs/tiku_nvm_backend.h>
#ifndef TIKU_S31_STORAGE_ASYNC_ENABLE
/** @brief Enable the polled S31 storage implementation. */
#define TIKU_S31_STORAGE_ASYNC_ENABLE 0
#endif
/** @brief Controller configuration retained in PSRAM until an automatic flash operation retires. */
typedef struct {
    uint32_t saved[17], address, started;
    unsigned operation;
} tiku_s31_flash_t;
/** @brief Caller-owned region transaction; retain it and immutable input until poll retires it. */
typedef struct {
    tiku_s31_flash_t flash;
    const uint8_t *data;
    size_t offset, left, count, within;
    uint32_t sector_offset, cursor, crc;
    unsigned phase, after_mirror, target, pending, verify;
} tiku_s31_storage_write_t;
/** @brief Begin a write within a mapped region view: 1 accepted, -2 busy, -1 invalid/unavailable.
 * @note Requires an established durable mirror. Foreground calls are serialized. The input cannot overlap affected shadow sectors.
 * The transaction owns region writes and mirror flushes until retirement; other flushes fail.
 */
int tiku_s31_storage_write_begin(tiku_s31_storage_write_t *, tiku_nvm_backend_t *,
                                  size_t offset, const void *, size_t length);
/** @brief Advance an accepted write: 1 pending, 0 durable, -1 failed and retired.
 * @note Failure latches storage unavailable until boot recovery. Pending operations must
 * be drained even when their caller cancels. Mapped region bytes publish after commit.
 * Calls outside the owning foreground context return -1 without changing ownership.
 */
int tiku_s31_storage_write_poll(tiku_s31_storage_write_t *);
/** @brief Return nonzero while an automatic flash operation owns the controller. */
int tiku_s31_storage_io_busy(void);
/** @brief Start or poll an automatic erase/program operation: 1 pending, 0 done, -1 refused/failed.
 * @note The caller retains zero-initialized controller state until completion. Operation 1
 * erases an aligned sector; operation 2 programs 4..64 aligned bytes within one page.
 * Caches remain enabled; hardware suspend/resume protects mapped reads outside the sector.
 * Resume must name the accepted state, operation and address. A progress timeout halts.
 */
int tiku_s31_storage_slice(tiku_s31_flash_t *, unsigned operation, uint32_t address,
                            void *buffer, size_t length, int resume);
#endif
