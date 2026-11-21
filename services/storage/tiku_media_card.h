/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_card.h - Whole-card native media sessions on supported SD ports.
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
/** @brief Size of caller-owned bounded-read state for the mounted card service. */
size_t tiku_media_card_read_size(void);
/** @brief Initialize unused, eight-byte-aligned bounded-read state without I/O. */
void tiku_media_card_read_init(void *);
/** @brief Advance one read phase; unsupported queue heads stay queued for their owner. */
int tiku_media_card_read_step(void *,tiku_media_store_t *);
/** @brief Report whether caller state owns no active transport or query. */
int tiku_media_card_read_idle(const void *);
/** @brief Cancel one queued request after releasing its transport, retaining ownership on BUSY. */
int tiku_media_card_read_cancel(void *,tiku_media_store_t *,tiku_media_request_t *);
/** @brief Required eight-byte-aligned caller storage for polled file mutations. */
size_t tiku_media_card_mutation_size(void);
/** @brief Initialize unused mutation state without accessing or writing the card. */
void tiku_media_card_mutation_init(void *);
/** @brief Advance one rename/delete phase under shared queue serialization. */
int tiku_media_card_mutation_step(void *,tiku_media_store_t *);
/** @brief Report whether caller state owns no mutation or transport. */
int tiku_media_card_mutation_idle(const void *);
/** @brief Cancel after retiring borrowed storage; BUSY requires retaining caller memory. */
int tiku_media_card_mutation_cancel(void *,tiku_media_store_t *,tiku_media_request_t *);
#endif
