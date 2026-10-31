/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_card.h - Whole-card native media sessions on FireBeetle 2.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_CARD_H_
#define TIKU_MEDIA_CARD_H_
#include "tiku_media_store.h"
/**
 * @brief Mount a whole-card volume, or explicitly format the matching CID.
 * @note Format requires both expected_cid and a fresh nonzero UUID. Mount
 * discovers the UUID without writing. Refuses active writers, readers or GC.
 */
int tiku_media_card_mount(const uint32_t *expected_cid,const uint8_t *format_uuid,uint32_t hz);
/** @brief Return the mounted application service, or NULL when unavailable. */
tiku_media_store_t *tiku_media_card_store(void);
/** @brief Execute a serialized request, advancing pending C6 transfers on contention. */
int tiku_media_card_request(tiku_media_request_t *request);
#endif
