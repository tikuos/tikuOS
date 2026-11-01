/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_save.h - Publish a complete memory buffer on mounted native media.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_SAVE_H_
#define TIKU_MEDIA_SAVE_H_
#include <stdint.h>
/** @brief Upload ID, checkpointed byte count and expected SHA-256 digest. */
typedef struct {
    uint64_t id;
    uint32_t bytes;
    uint8_t digest[32];
} tiku_media_save_result_t;
/**
 * @brief Hash and atomically save a nonempty buffer under an unused name.
 * @note Foreground, serialized call; the caller keeps the buffer immutable until
 * return. Requires mounted media. Failure may leave a private upload, listed by
 * media uploads and deletable by ID. The visible name appears after verification.
 * @return Zero on completion or a native media error; existing names return BUSY.
 */
int tiku_media_save_buffer(const char *name,const void *buffer,uint32_t bytes,
                           tiku_media_save_result_t *result);
#endif
