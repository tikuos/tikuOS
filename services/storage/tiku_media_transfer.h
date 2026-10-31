/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_transfer.h - Checked, resumable object transfers over message transports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_H_
#define TIKU_MEDIA_TRANSFER_H_
#include "tiku_media_store.h"
/** @brief Wire framing and bounded request payload. */
#define TIKU_MEDIA_TRANSFER_HEADER 32u
#define TIKU_MEDIA_TRANSFER_CHUNK 1024u
#define TIKU_MEDIA_TRANSFER_PACKET (TIKU_MEDIA_TRANSFER_HEADER+TIKU_MEDIA_TRANSFER_CHUNK)
/** @brief Protocol operations; requests execute in order with one outstanding reply. */
enum {
    TIKU_MT_HELLO=1,TIKU_MT_BEGIN,TIKU_MT_RESUME,TIKU_MT_DATA,TIKU_MT_FINISH,
    TIKU_MT_RELEASE,TIKU_MT_OPEN,TIKU_MT_READ,TIKU_MT_CLOSE,TIKU_MT_LIST,TIKU_MT_NEXT
};
/** @brief Caller-owned session; discard through close before reusing its memory. */
typedef struct {
    tiku_media_store_t *store;
    uint32_t client,last_sequence;
    uint64_t upload,reader,accepted,committed,expected;
    tiku_mfs_cursor_t cursor;
    char prefix[TIKU_MFS_NAME];
    uint8_t key[32],authenticated,listing,fault;
    uint32_t staged,last_size,reply_size;
    uint8_t stage[TIKU_MFS_CHUNK];
    uint8_t last[TIKU_MEDIA_TRANSFER_PACKET],reply[TIKU_MEDIA_TRANSFER_PACKET];
} tiku_media_transfer_t;
/** @brief Bind an unused session to a mounted service, owner ID and optional TCP key. */
void tiku_media_transfer_init(tiku_media_transfer_t *,tiku_media_store_t *,uint32_t client,const uint8_t *key);
/** @brief Release this session's writer and reader; committed uploads remain resumable. */
int tiku_media_transfer_close(tiku_media_transfer_t *);
/**
 * @brief Validate and execute one packet, retaining its reply until the next call.
 * @return Reply length, or zero for an invalid checksum/framing or conflicting retry.
 * @note Exact retransmissions of the last request return its cached reply. A HELLO
 * starts a session; other requests need the next sequence. No formatting operation exists.
 */
size_t tiku_media_transfer_handle(tiku_media_transfer_t *,const void *,size_t);
#endif
