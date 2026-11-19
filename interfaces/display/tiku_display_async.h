/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_async.h - bounded publication and polled DMA ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DISPLAY_ASYNC_H_
#define TIKU_DISPLAY_ASYNC_H_
#include "tiku_display.h"

/** @brief Serialized backend operations; poll callbacks return PENDING, OK or error. */
typedef struct {
    int (*publish)(void *buffer, uint32_t offset, uint32_t bytes);
    void (*switch_start)(void *buffer);
    int (*switch_poll)(void *buffer);
    void (*stop_start)(void);
    int (*stop_poll)(void);
    uint32_t (*now_us)(void);
    void (*fail_stop)(void); /**< must not return; DMA retirement failed */
} tiku_display_async_ops_t;

/** @brief Zero-initialize once; reset after hardware close preserves token history. */
typedef struct {
    tiku_display_t *display;
    void *buffer, *previous;
    uint64_t last_token;
    uint32_t offset, end, row_start, row_bytes, deadline;
    uint16_t row, rows;
    uint8_t phase;
} tiku_display_async_t;

/** @brief Accept a disjoint RGB565 pair with 64-byte-aligned bases and strides. */
int tiku_display_async_begin(tiku_display_async_t *state, tiku_display_t *display,
    uint64_t token, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
/** @brief Publish at most 4096 bytes or poll one handoff/stop; 500 ms wait deadlines. */
int tiku_display_async_step(tiku_display_async_t *state, tiku_display_t *display,
    uint64_t token, const tiku_display_async_ops_t *ops);
/** @brief Forget a retired request after the backend has stopped DMA. */
void tiku_display_async_reset(tiku_display_async_t *state);
#endif
