/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_block.h - Ordered 512-byte block storage contract.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BLOCK_H_
#define TIKU_BLOCK_H_
#include <stdint.h>
/**
 * @brief Synchronous, serialized access to one explicitly selected extent.
 * @note LBAs are extent-relative. Callbacks return zero on success and retain no
 * buffers. sync makes prior writes durable and ordered before subsequent writes.
 * A failed operation has uncertain completion; quiesce before remount or release.
 * @note The backend rejects a changed medium and out-of-extent requests. Completed
 * barriers must preserve durable sectors outside later interrupted writes.
 */
typedef struct {
    void *context;
    uint64_t sectors;
    int (*read)(void *, uint64_t, uint32_t, void *);
    int (*write)(void *, uint64_t, uint32_t, const void *);
    int (*sync)(void *);
} tiku_block_t;
#endif
