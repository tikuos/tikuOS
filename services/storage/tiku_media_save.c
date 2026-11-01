/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_save.c - Checked atomic publication of captured media buffers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_save.h"
#include "tiku_media_card.h"
#include <kernel/cpu/tiku_watchdog.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#include <string.h>
#define SAVE_CLIENT 0x4d465405u
/** @brief Submit a storage operation with the snapshot owner's permissions. */
static int request(tiku_media_request_t *r)
{
    r->client=SAVE_CLIENT;r->permissions=TIKU_MEDIA_READ|TIKU_MEDIA_WRITE;
    return tiku_media_card_request(r);
}
int tiku_media_save_buffer(const char *name,const void *buffer,uint32_t bytes,
                           tiku_media_save_result_t *result)
{
    if(!result)return TIKU_MFS_ARG;
    memset(result,0,sizeof *result);
    if(!name||!*name||strlen(name)>=TIKU_MFS_NAME||!buffer||!bytes)return TIKU_MFS_ARG;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)if(*p<32||*p==127)return TIKU_MFS_ARG;
    tiku_media_store_t *store=tiku_media_card_store();if(!store)return TIKU_MFS_NOFS;
    if(store->writer||store->collector)return TIKU_MFS_BUSY;
    tiku_mfs_object_t info;tiku_media_request_t r={0};r.op=TIKU_MEDIA_STAT;r.name=name;r.info=&info;
    int rc=request(&r);if(!rc)return TIKU_MFS_BUSY;if(rc!=TIKU_MFS_NOENT)return rc;
    tiku_kits_crypto_sha256_ctx_t hash;
    if(tiku_kits_crypto_sha256_init(&hash))return TIKU_MFS_IO;
    for(uint32_t at=0;at<bytes;){
        uint32_t n=bytes-at;if(n>TIKU_MFS_CHUNK)n=TIKU_MFS_CHUNK;
        tiku_watchdog_kick();
        if(tiku_kits_crypto_sha256_update(&hash,(const uint8_t *)buffer+at,n))return TIKU_MFS_IO;
        at+=n;
    }
    if(tiku_kits_crypto_sha256_final(&hash,result->digest))return TIKU_MFS_IO;
    r.op=TIKU_MEDIA_BEGIN;r.policy=TIKU_MFS_ATOMIC;r.offset=bytes;r.info=NULL;
    memcpy(r.digest,result->digest,32);
    rc=request(&r);if(rc)return rc;
    result->id=r.id;
    for(uint32_t at=0;at<bytes;){
        uint32_t n=bytes-at;if(n>TIKU_MFS_CHUNK)n=TIKU_MFS_CHUNK;
        tiku_watchdog_kick();r.op=TIKU_MEDIA_APPEND;r.buffer=(uint8_t *)buffer+at;r.length=n;
        rc=request(&r);if(rc)break;
        r.op=at+n==bytes?TIKU_MEDIA_FINALIZE:TIKU_MEDIA_SYNC;
        rc=request(&r);if(rc)break;
        at+=n;result->bytes=at;
    }
    if(rc){r.op=TIKU_MEDIA_RELEASE;(void)request(&r);}
    return rc;
}
