/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_local.h - bounded whole messages in caller-owned slots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_LOCAL_H
#define TIKU_LINK_LOCAL_H
#include "tiku_link.h"

typedef struct {
    tiku_link_t link;
    uint8_t *storage;
    unsigned slots, stride, first, count;
    unsigned full, oversize;
    void (*wake)(void);
} tiku_link_local_t;

/** @brief Open a queue over slots times stride bytes. Operations serialize
 * in the calling owner's context. Each slot holds a length and one message. */
int tiku_link_local_open(tiku_link_local_t *q, uint8_t *storage,
                         unsigned slots, unsigned stride);
/** @brief Take one message, returning its size, zero for empty, or -1 for
 * a closed queue or insufficient capacity. A short read retains the head. */
const uint8_t *tiku_link_local_front(const tiku_link_local_t *q, uint16_t *length);
int tiku_link_local_read(tiku_link_local_t *q, void *out, size_t capacity);
#endif
